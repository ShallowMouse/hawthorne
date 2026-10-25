#include "NetReceiver.h"
#include "../Engine/Varispeed.h"

namespace hawthorne
{

namespace
{
    struct LambdaThread : juce::Thread
    {
        LambdaThread (const juce::String& name, std::function<void()> f) : juce::Thread (name), fn (std::move (f)) {}
        void run() override { fn(); }
        std::function<void()> fn;
    };

    inline uint64_t makeTag (uint32_t session, juce::int64 idx) noexcept
    {
        return ((uint64_t) ((session % 0xffffffu) + 1u) << 40) | ((uint64_t) idx & 0xffffffffffull);
    }

    constexpr int hs = SincKernel::halfWidth;   // resampling at ~unity ratio
    constexpr int taps = 2 * hs;
}

//==============================================================================
struct NetReceiver::Slot
{
    // Message thread
    juce::String name;

    // Shared
    std::atomic<bool>     active { false };
    std::atomic<uint32_t> senderId { 0 };
    std::atomic<uint32_t> session { 0 };      // 0 = no stream yet
    std::atomic<int>      channels { 0 };
    std::atomic<double>   senderRate { 0.0 };
    std::atomic<juce::int64> latest { -1 };   // one past the newest frame received
    std::atomic<juce::int64> packets { 0 };

    std::vector<float> ring;                  // channel-major, each channel mirrored (2 x ringFrames)
    std::unique_ptr<std::atomic<uint64_t>[]> stamps;

    // Audio thread
    uint32_t aSession = 0;
    bool started = false;
    juce::int64 readPos = 0, checkedUpTo = 0;
    double frac = 0.0, fillAvg = 0.0, t = 0.0, lockTimer = 0.0;
    double clock = 0.0, mx = 0.0, my = 0.0, cxx = 0.0, cxy = 0.0, slope = 1.0;   // rate estimate
    bool locked = false;
    bool gliding = false;         // easing onto a whole sample before locking
    int glideLeft = 0;
    double glideRate = 1.0;
    double lastPos0 = 0.0, lastRate = 1.0;   // for tests
    // Long-baseline rate: (read position + buffer) sampled every 10 s, up to 2 minutes back.
    double fillSlow = 0.0, snapTimer = 0.0, longRate = 0.0;     // 0 = not known yet
    double baseRate = 1.0;
    // Buffer sizing: how bursty arrival is (sender's audio buffer, network jitter,
    // our own audio buffer), measured for a second before playing and grown on any underrun.
    bool measured = false;
    double measureT = 0.0, wclock = 0.0, dMin = 0.0, dMax = 0.0, swing = 0.0;
    std::atomic<double> targetMs { 0.0 };
    std::atomic<int> senderLatency { 0 };
    std::array<std::pair<double, double>, 13> snaps {};
    int numSnaps = 0;
    juce::AudioBuffer<float> out { net::maxChannels, maxBlock };

    // Published to the UI
    std::atomic<int> state { (int) State::off };
    std::atomic<bool> live { false };
    std::atomic<double> ppm { 0.0 }, bufferMs { 0.0 };
    std::atomic<juce::int64> dropouts { 0 };

    void allocate()
    {
        if (ring.empty())
        {
            ring.assign ((size_t) net::maxChannels * 2 * ringFrames, 0.0f);
            stamps.reset (new std::atomic<uint64_t>[(size_t) ringFrames]());
        }
    }

    float* channelRing (int ch) noexcept { return ring.data() + (size_t) ch * 2 * ringFrames; }
};

//==============================================================================
NetReceiver::NetReceiver() : receiverId ([] { juce::Random r; r.setSeedRandomly(); return (uint32_t) r.nextInt64() | 1u; }())
{
    for (auto& s : slots)
        s = std::make_unique<Slot>();
    weights.resize ((size_t) maxBlock * taps);
    bases.resize ((size_t) maxBlock);
}

NetReceiver::~NetReceiver()
{
    stop();
}

bool NetReceiver::start (juce::String& error)
{
    if (running)
        return true;

    dataSocket = std::make_unique<juce::DatagramSocket> (true);
    if (! dataSocket->bindToPort (net::dataPort) && ! dataSocket->bindToPort (0))
    {
        error = "Could not open a network port for Hawthorne Link.";
        dataSocket.reset();
        return false;
    }
    boundDataPort = dataSocket->getBoundPort();
    net::setSocketBuffers (dataSocket->getRawSocketHandle(), 8 << 20, 1 << 20);   // ride out a stalled thread without losing packets

    announceSocket = std::make_unique<juce::DatagramSocket> (true);
    announceSocket->setEnablePortReuse (true);
    if (! announceSocket->bindToPort (net::announcePort))
        announceSocket.reset();   // discovery by broadcast unavailable; manual addresses still work

    running = true;
    dataThread = std::make_unique<LambdaThread> ("Hawthorne Link RX", [this] { networkLoop(); });
    controlThread = std::make_unique<LambdaThread> ("Hawthorne Link control", [this] { controlLoop(); });
    dataThread->startThread (juce::Thread::Priority::highest);
    controlThread->startThread();
    return true;
}

void NetReceiver::stop()
{
    if (! running)
        return;

    for (int s = 0; s < maxSlots; ++s)
        if (slots[(size_t) s]->active)
            disconnect (s);

    // let the control thread send the goodbyes
    for (int i = 0; i < 20 && ! outbox.empty(); ++i)
        juce::Thread::sleep (10);

    running = false;
    if (dataSocket != nullptr)     dataSocket->shutdown();
    if (announceSocket != nullptr) announceSocket->shutdown();
    if (dataThread != nullptr)     dataThread->stopThread (2000);
    if (controlThread != nullptr)  controlThread->stopThread (2000);
    dataThread.reset();
    controlThread.reset();
    dataSocket.reset();
    announceSocket.reset();
}

//==============================================================================
std::vector<NetReceiver::SenderInfo> NetReceiver::getSenders() const
{
    const juce::ScopedLock sl (lock);
    return senders;
}

void NetReceiver::setManualAddresses (const juce::StringArray& ips)
{
    const juce::ScopedLock sl (lock);
    manualAddresses = ips;
}

juce::StringArray NetReceiver::getManualAddresses() const
{
    const juce::ScopedLock sl (lock);
    return manualAddresses;
}

void NetReceiver::connect (int slot, const juce::String& senderName)
{
    if (! juce::isPositiveAndBelow (slot, maxSlots))
        return;

    auto& s = *slots[(size_t) slot];
    {
        const juce::ScopedLock sl (lock);
        s.name = senderName;
    }
    s.allocate();
    s.senderId = 0;
    s.session = 0;
    s.active = true;
    subscribeNow = true;
}

void NetReceiver::connectToSenderId (int slot, uint32_t id, const juce::String& name)
{
    auto& s = *slots[(size_t) slot];
    {
        const juce::ScopedLock sl (lock);
        s.name = name;
    }
    s.allocate();
    s.session = 0;
    s.senderId = id;
    s.active = true;
}

void NetReceiver::disconnect (int slot)
{
    if (! juce::isPositiveAndBelow (slot, maxSlots))
        return;

    auto& s = *slots[(size_t) slot];
    const auto id = s.senderId.load();
    s.active = false;
    s.senderId = 0;
    s.session = 0;

    const juce::ScopedLock sl (lock);
    for (auto& info : senders)
        if (info.senderId == id && id != 0)
            outbox.push_back ({ info.address, info.controlPort, net::makeBye (id) });
    s.name.clear();
}

juce::String NetReceiver::getSlotName (int slot) const
{
    const juce::ScopedLock sl (lock);
    return juce::isPositiveAndBelow (slot, maxSlots) && slots[(size_t) slot]->active ? slots[(size_t) slot]->name : juce::String();
}

int NetReceiver::findSlot (const juce::String& senderName) const
{
    const juce::ScopedLock sl (lock);
    for (int i = 0; i < maxSlots; ++i)
        if (slots[(size_t) i]->active && slots[(size_t) i]->name == senderName)
            return i;
    return -1;
}

int NetReceiver::firstFreeSlot() const
{
    for (int i = 0; i < maxSlots; ++i)
        if (! slots[(size_t) i]->active)
            return i;
    return -1;
}

NetReceiver::Status NetReceiver::getStatus (int slot) const
{
    Status st;
    if (! juce::isPositiveAndBelow (slot, maxSlots))
        return st;

    auto& s = *slots[(size_t) slot];
    {
        const juce::ScopedLock sl (lock);
        st.name = s.name;
    }
    st.state      = s.active ? (State) s.state.load() : State::off;
    st.channels   = s.channels;
    st.senderRate = s.senderRate;
    st.ppm        = s.ppm;
    st.bufferMs   = s.bufferMs;
    st.dropouts   = s.dropouts;
    st.targetMs   = s.targetMs;
    st.senderLatencyMs = s.senderRate.load() > 0 ? s.senderLatency.load() / s.senderRate.load() * 1000.0 : 0.0;
    st.packets    = s.packets;
    return st;
}

juce::String NetReceiver::describe (State s)
{
    switch (s)
    {
        case State::off:          return "off";
        case State::searching:    return "looking for the sender";
        case State::waiting:      return "waiting for audio";
        case State::rateMismatch: return "SAMPLE RATE DOESN'T MATCH";
        case State::syncing:      return "syncing";
        case State::corrected:    return "drift corrected";
        case State::locked:       return "LOCKED - bit-perfect";
    }
    return {};
}

//==============================================================================
// Network threads

void NetReceiver::networkLoop()
{
    std::vector<uint8_t> buf (2048);
    auto* thread = juce::Thread::getCurrentThread();

    while (running && thread != nullptr && ! thread->threadShouldExit())
    {
        if (dataSocket->waitUntilReady (true, 200) != 1)
            continue;

        juce::String from;
        int port = 0;
        const int n = dataSocket->read (buf.data(), (int) buf.size(), false, from, port);
        if (n > 0)
            handleDatagram (buf.data(), n, from);
    }
}

void NetReceiver::controlLoop()
{
    std::vector<uint8_t> buf (2048);
    auto* thread = juce::Thread::getCurrentThread();
    juce::uint32 lastSub = 0;

    while (running && thread != nullptr && ! thread->threadShouldExit())
    {
        if (announceSocket != nullptr && announceSocket->waitUntilReady (true, 100) == 1)
        {
            juce::String from;
            int port = 0;
            const int n = announceSocket->read (buf.data(), (int) buf.size(), false, from, port);
            if (n > 0)
                handleDatagram (buf.data(), n, from);
        }
        else if (announceSocket == nullptr)
        {
            juce::Thread::sleep (100);
        }

        const auto now = juce::Time::getMillisecondCounter();
        if (now - lastSub >= 1000 || subscribeNow.exchange (false))
        {
            lastSub = now;
            sendSubscriptions();
        }

        std::vector<Outgoing> toSend;
        {
            const juce::ScopedLock sl (lock);
            toSend.swap (outbox);
        }
        for (auto& o : toSend)
            dataSocket->write (o.address, o.port, o.msg.toRawUTF8(), (int) o.msg.getNumBytesAsUTF8());
    }
}

void NetReceiver::sendSubscriptions()
{
    if (dataSocket == nullptr)
        return;

    std::vector<Outgoing> outgoing;
    const auto now = juce::Time::getMillisecondCounter();

    {
        const juce::ScopedLock sl (lock);

        // forget senders not heard from for a while
        senders.erase (std::remove_if (senders.begin(), senders.end(),
                                       [now] (const SenderInfo& i) { return now - i.lastSeenMs > 10000; }),
                       senders.end());

        for (auto& slot : slots)
        {
            if (! slot->active || slot->name.isEmpty())
                continue;

            const SenderInfo* best = nullptr;
            for (auto& info : senders)
                if (info.name == slot->name && (best == nullptr || info.lastSeenMs > best->lastSeenMs))
                    best = &info;

            if (best == nullptr)
                continue;

            if (slot->senderId.load() != best->senderId)
            {
                slot->session = 0;          // a different sender (or it restarted): start afresh
                slot->senderId = best->senderId;
            }
            outgoing.push_back ({ best->address, best->controlPort, net::makeSubscribe (best->senderId, receiverId) });
        }

        for (auto& ip : manualAddresses)
            outgoing.push_back ({ ip, net::controlPort, net::makeQuery() });
    }

    for (auto& o : outgoing)
        dataSocket->write (o.address, o.port, o.msg.toRawUTF8(), (int) o.msg.getNumBytesAsUTF8());
}

void NetReceiver::handleDatagram (const void* data, int size, const juce::String& from) noexcept
{
    auto* bytes = static_cast<const uint8_t*> (data);

    if (size >= 6 && std::memcmp (bytes, "HWNA1 ", 6) == 0)
    {
        handleControl (juce::String::fromUTF8 ((const char*) bytes, size), from);
        return;
    }

    if (size < net::headerSize)
        return;

    net::PacketHeader h;
    std::memcpy (&h, bytes, sizeof (h));
    if (h.magic != net::magic || h.version != net::version || h.channels == 0 || h.channels > net::maxChannels || h.frames == 0)
        return;

    const bool hasB = (h.flags & 1) != 0;
    const int blockBytes = (int) h.frames * h.channels * 3;
    if (size < net::headerSize + blockBytes * (hasB ? 2 : 1))
        return;

    handleAudio (h, bytes + net::headerSize);
}

void NetReceiver::handleControl (const juce::String& msg, const juce::String& from)
{
    net::Announce a;
    if (! net::parseAnnounce (msg, a))
        return;

    const juce::ScopedLock sl (lock);
    SenderInfo* found = nullptr;
    for (auto& i : senders)
        if (i.senderId == a.senderId) found = &i;

    if (found == nullptr)
    {
        senders.push_back ({});
        found = &senders.back();
    }

    found->senderId    = a.senderId;
    found->name        = a.name.isNotEmpty() ? a.name : from;
    found->address     = from;
    found->controlPort = a.controlPort;
    found->channels    = a.channels;
    found->sampleRate  = a.sampleRate;
    found->lastSeenMs  = juce::Time::getMillisecondCounter();
}

void NetReceiver::handleAudio (const net::PacketHeader& h, const uint8_t* payload) noexcept
{
    Slot* s = nullptr;
    for (auto& slot : slots)
        if (slot->active.load() && slot->senderId.load() == h.senderId)
            s = slot.get();

    if (s == nullptr || s->ring.empty())
        return;

    if (s->session.load() != h.session)
    {
        // New stream: publish its format before the session that makes it valid.
        s->session.store (0);
        s->latest.store (-1);
        s->channels.store (h.channels);
        s->senderRate.store ((double) h.sampleRate);
        s->session.store (h.session, std::memory_order_release);
    }

    const int blockBytes = (int) h.frames * h.channels * 3;
    writeBlock (*s, h.session, h.firstFrame, h.frames, h.channels, payload);
    if (h.flags & 1)
        writeBlock (*s, h.session, h.prevFirstFrame, h.frames, h.channels, payload + blockBytes);

    const auto end = (juce::int64) (h.firstFrame + h.frames);
    if (end > s->latest.load())
        s->latest.store (end, std::memory_order_release);
    s->packets.fetch_add (1, std::memory_order_relaxed);
    s->senderLatency.store (h.latency, std::memory_order_relaxed);
}

void NetReceiver::writeBlock (Slot& s, uint32_t session, juce::uint64 first, int frames, int channels, const uint8_t* pcm) noexcept
{
    const auto latest = s.latest.load();
    if (latest >= 0 && (juce::int64) first + frames + ringFrames / 2 < latest)
        return;   // far too old

    for (int f = 0; f < frames; ++f)
    {
        const auto idx = (juce::int64) first + f;
        const int pos = (int) (idx & (ringFrames - 1));
        const auto tag = makeTag (session, idx);
        if (s.stamps[(size_t) pos].load (std::memory_order_relaxed) == tag)
            continue;   // already have it (redundant copy)

        const uint8_t* p = pcm + (size_t) f * (size_t) channels * 3;
        for (int c = 0; c < channels; ++c)
        {
            const float v = net::readInt24 (p + c * 3);
            float* r = s.channelRing (c);
            r[pos] = v;
            r[pos + ringFrames] = v;
        }
        s.stamps[(size_t) pos].store (tag, std::memory_order_release);
    }
}

//==============================================================================
// Audio thread

void NetReceiver::prepare (double sr) noexcept
{
    sampleRate = sr;
    for (auto& s : slots)
        s->started = false;
}

void NetReceiver::process (int n) noexcept
{
    n = juce::jlimit (0, maxBlock, n);
    for (auto& s : slots)
        processSlot (*s, n);
}

const float* NetReceiver::getChannel (int slot, int ch) const noexcept
{
    if (! juce::isPositiveAndBelow (slot, maxSlots))
        return nullptr;
    auto& s = *slots[(size_t) slot];
    if (! s.live.load (std::memory_order_relaxed) || ch >= s.channels.load (std::memory_order_relaxed))
        return nullptr;
    return s.out.getReadPointer (ch);
}

void NetReceiver::getReadPosition (int slot, double& pos0, double& rate) const noexcept
{
    auto& s = *slots[(size_t) slot];
    pos0 = s.lastPos0;
    rate = s.lastRate;
}

bool NetReceiver::isLive (int slot) const noexcept
{
    return juce::isPositiveAndBelow (slot, maxSlots) && slots[(size_t) slot]->live.load();
}

void NetReceiver::ensureChecked (Slot& s, juce::int64 upTo) noexcept
{
    // Every frame is checked once, just before it is first needed. Anything that
    // never arrived (or is a stale frame from a previous lap) is silenced.
    const int ch = s.channels.load (std::memory_order_relaxed);
    bool missing = false;

    for (auto idx = s.checkedUpTo + 1; idx <= upTo; ++idx)
    {
        const int pos = (int) (idx & (ringFrames - 1));
        if (s.stamps[(size_t) pos].load (std::memory_order_acquire) != makeTag (s.aSession, idx))
        {
            for (int c = 0; c < ch; ++c)
            {
                float* r = s.channelRing (c);
                r[pos] = 0.0f;
                r[pos + ringFrames] = 0.0f;
            }
            missing = true;
        }
    }

    s.checkedUpTo = juce::jmax (s.checkedUpTo, upTo);
    if (missing)
        s.dropouts.fetch_add (1, std::memory_order_relaxed);
}

void NetReceiver::processSlot (Slot& s, int n) noexcept
{
    auto silence = [&] (State st)
    {
        s.live.store (false, std::memory_order_relaxed);
        s.state.store ((int) st, std::memory_order_relaxed);
    };

    if (! s.active.load (std::memory_order_relaxed))
        return silence (State::off);

    const auto sess = s.session.load (std::memory_order_acquire);
    if (sess == 0)
        return silence (s.senderId.load() == 0 ? State::searching : State::waiting);

    if (sess != s.aSession)
    {
        s.aSession = sess;
        s.started = false;
        s.measured = false;
        s.measureT = 0.0;
        s.swing = 0.0;
    }

    const double sr = sampleRate.load (std::memory_order_relaxed);
    if (std::abs (s.senderRate.load (std::memory_order_relaxed) - sr) > 0.5)
        return silence (State::rateMismatch);

    const int ch = s.channels.load (std::memory_order_relaxed);
    const auto latest = s.latest.load (std::memory_order_acquire);
    if (! s.started && ! s.measured)
    {
        // Watch how bursty the arrivals are before playing anything.
        if (latest < 0)
            return silence (State::waiting);
        const double d = (double) latest - s.wclock;
        if (s.measureT == 0.0) s.dMin = s.dMax = d;
        s.dMin = juce::jmin (s.dMin, d);
        s.dMax = juce::jmax (s.dMax, d);
        s.wclock += n;
        s.measureT += (double) n / sr;
        if (s.measureT < 1.0)
            return silence (State::waiting);
        s.swing = juce::jmax (s.swing, s.dMax - s.dMin);
        s.measured = true;
    }

    // Smallest safe buffer: the burst size + the interpolator's look-ahead + a margin.
    // (The BUFFER setting is a minimum.)
    const double target = juce::jmax (targetLatencyMs.load (std::memory_order_relaxed) * sr / 1000.0,
                                      s.swing + (double) (hs + 2 * n) + 0.0015 * sr);
    s.targetMs.store (target / sr * 1000.0, std::memory_order_relaxed);

    if (! s.started)
    {
        if (latest < (juce::int64) target + hs)
            return silence (State::waiting);

        s.readPos = latest - (juce::int64) target;
        s.frac = 0.0;
        s.fillAvg = target;
        s.t = 0.0;
        s.lockTimer = 0.0;
        s.locked = false;
        s.glideLeft = 0;
        s.gliding = false;
        s.clock = 0.0;
        s.mx = 0.0;
        s.my = (double) latest;
        s.cxx = s.cxy = 0.0;
        s.slope = 1.0;
        s.fillSlow = target;
        s.snapTimer = 0.0;
        s.longRate = 0.0;
        s.baseRate = 1.0;
        s.numSnaps = 0;
        s.checkedUpTo = s.readPos - hs - 1;
        s.started = true;
    }

    const double pos = (double) s.readPos + s.frac;
    const double fill = (double) latest - pos;

    // Under- or overrun (sender stopped, network stalled, machine hiccup): start afresh.
    if (fill < (double) (n + hs + 2) || fill > (double) (ringFrames - 4 * maxBlock - hs))
    {
        if (fill < (double) (n + hs + 2))
            s.swing += juce::jmax ((double) maxBlock, target * 0.25);    // ran dry: give it more room from now on
        s.started = false;
        s.dropouts.fetch_add (1, std::memory_order_relaxed);
        return silence (State::waiting);
    }

    // Clock recovery.
    //  - The sender's rate relative to ours is measured by a running regression of
    //    "newest frame received" against our own sample clock (jitter averages out).
    //  - A gentle correction (20 s time constant) holds the buffer on its target.
    //  - If the measured rate is 1 (shared word clock), the stream eases onto a
    //    whole-sample position and is then copied straight through: bit-perfect.
    const double dt = (double) n / sr;
    s.t += dt;
    {
        const double tau = juce::jmin (60.0, 2.0 + s.t);
        const double a = 1.0 - std::exp (-dt / tau);
        const double dx = s.clock - s.mx, dy = (double) latest - s.my;
        s.mx += a * dx;
        s.my += a * dy;
        s.cxx = (1.0 - a) * (s.cxx + a * dx * dx);
        s.cxy = (1.0 - a) * (s.cxy + a * dx * dy);
        if (s.t > 1.0 && s.cxx > 0.0)
            s.slope = juce::jlimit (0.995, 1.005, s.cxy / s.cxx);
        s.clock += n;
    }
    s.fillAvg += (fill - s.fillAvg) * (1.0 - std::exp (-dt / 2.0));
    s.fillSlow += (fill - s.fillSlow) * (1.0 - std::exp (-dt / 8.0));
    const double e = s.fillAvg - target;

    // The sender's frames per our frame = d(read position + buffer) / d(our clock),
    // over a minute or more: accurate to a fraction of a ppm.
    s.snapTimer += dt;
    if (s.t > 25.0 && s.snapTimer >= 10.0)    // once the buffer has settled
    {
        s.snapTimer = 0.0;
        if (s.numSnaps == (int) s.snaps.size())
        {
            std::rotate (s.snaps.begin(), s.snaps.begin() + 1, s.snaps.end());
            --s.numSnaps;
        }
        s.snaps[(size_t) s.numSnaps++] = { s.clock, pos + s.fillSlow };
        const auto& oldest = s.snaps[0];
        if (s.clock - oldest.first >= 60.0 * sr)
            s.longRate = (pos + s.fillSlow - oldest.second) / (s.clock - oldest.first);
    }
    double rate = 1.0;

    if (s.locked && std::abs (e) > juce::jmax (48.0, target * 0.3))
    {
        s.locked = false;       // the clocks have parted: drift-correct from here
        s.lockTimer = 0.0;
    }

    if (s.gliding && std::abs (s.longRate - 1.0) > 2.5e-6)
        s.gliding = false;

    if (! s.locked)
    {
        const double tauC = s.t < 10.0 ? 4.0 : 30.0;
        // the base rate eases between estimates (10 s) so the pitch never steps
        const double est = s.longRate > 0.0 ? s.longRate : s.slope;
        s.baseRate = s.t < 10.0 ? est : s.baseRate + (est - s.baseRate) * (1.0 - std::exp (-dt / 10.0));
        rate = juce::jlimit (0.995, 1.005, s.baseRate + juce::jlimit (-100.0e-6, 100.0e-6, e / (tauC * sr)));

        if (s.gliding && s.glideLeft <= 0)
        {
            // landed on a whole sample (to within rounding): lock
            s.readPos = (juce::int64) std::llround (pos);
            s.frac = 0.0;
            s.locked = true;
            s.gliding = false;
            rate = 1.0;
        }
        else if (s.gliding)
        {
            rate = s.glideRate;
            s.glideLeft -= n;
        }
        else
        {
            // Shared clock: over a minute or more the rate is 1 to within the measurement (~2 ppm).
            // (If two free-running clocks happen to be that close, the lock simply lets go
            //  smoothly when the buffer drifts, and the stream carries on drift-corrected.)
            if (s.longRate > 0.0 && std::abs (s.longRate - 1.0) < 2.5e-6 && std::abs (e) < 32.0)
            {
                // Ease the fractional position to a whole sample over ~1 s, then lock.
                const double d = s.frac < 0.5 ? -s.frac : 1.0 - s.frac;
                s.glideLeft = juce::jmax (n, ((int) sr / n) * n);
                s.glideRate = 1.0 + d / (double) s.glideLeft;
                s.gliding = true;
                rate = s.glideRate;
                s.glideLeft -= n;
            }
        }
    }

    s.lastPos0 = (double) s.readPos + s.frac;
    s.lastRate = s.locked ? 1.0 : rate;

    if (s.locked)
    {
        // Bit-perfect: straight copy.
        ensureChecked (s, s.readPos + n - 1);
        const int start = (int) (s.readPos & (ringFrames - 1));
        for (int c = 0; c < ch; ++c)
            juce::FloatVectorOperations::copy (s.out.getWritePointer (c), s.channelRing (c) + start, n);
        s.readPos += n;
    }
    else
    {
        const auto& kernel = SincKernel::get();
        double f = s.frac;
        for (int k = 0; k < n; ++k)
        {
            const double fl = std::floor (f);
            const auto first = s.readPos + (juce::int64) fl - hs + 1;
            bases[(size_t) k] = (int) (first & (ringFrames - 1));
            kernel.weights (f - fl, 1.0, hs, weights.data() + (size_t) k * taps);
            if (k == n - 1)
                ensureChecked (s, first + taps - 1);
            f += rate;
        }

        for (int c = 0; c < ch; ++c)
        {
            const float* r = s.channelRing (c);
            float* o = s.out.getWritePointer (c);
            for (int k = 0; k < n; ++k)
                o[k] = dotProduct (weights.data() + (size_t) k * taps, r + bases[(size_t) k], taps);
        }

        const double whole = std::floor (f);
        s.readPos += (juce::int64) whole;
        s.frac = f - whole;
    }

    s.ppm.store (s.locked ? 0.0 : ((s.longRate > 0.0 ? s.longRate : s.slope) - 1.0) * 1.0e6, std::memory_order_relaxed);
    s.bufferMs.store (s.fillAvg / sr * 1000.0, std::memory_order_relaxed);
    s.state.store ((int) (s.locked ? State::locked : (s.t < 15.0 ? State::syncing : State::corrected)), std::memory_order_relaxed);
    s.live.store (true, std::memory_order_relaxed);
}

} // namespace hawthorne
