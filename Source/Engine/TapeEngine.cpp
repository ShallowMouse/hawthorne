#include "TapeEngine.h"

namespace hawthorne
{

namespace
{
    inline float peakOf (const float* d, int n) noexcept
    {
        const auto r = juce::FloatVectorOperations::findMinAndMax (d, n);
        return juce::jmax (-r.getStart(), r.getEnd());
    }

    inline void raisePeak (std::atomic<float>& a, float v) noexcept
    {
        if (v > a.load (std::memory_order_relaxed))
            a.store (v, std::memory_order_relaxed);
    }
}

//==============================================================================
TapeEngine::TapeEngine()
{
    formatManager.registerBasicFormats();
    prefetchThread.startThread (juce::Thread::Priority::high);
    absToActive.fill (-1);

    for (int t = 0; t < kMaxTracks; ++t)
    {
        armed[(size_t) t] = false;
        trackPeak[(size_t) t] = 0.0f;
    }

    masterPeak[0] = masterPeak[1] = 0.0f;
    absToActiveOut.fill (-1);
    rebuildRouting();
}

TapeEngine::~TapeEngine()
{
    stop();
    auto old = std::move (ownedGraph);
    graph.store (nullptr);
    retire (std::move (old));
    auto oldRouting = std::move (ownedRouting);
    routing.store (nullptr);
    retire (std::move (oldRouting));
    readerCache.clear();
    prefetchThread.stopThread (2000);
}

//==============================================================================
void TapeEngine::setReel (Reel* s)
{
    stop();
    reel = s;
    position = 0;
    droppedTotal = 0;
    droppedBeforePass = 0;
    playbackUnderruns = 0;

    numTracks = s != nullptr ? s->numTracks : kDefaultTracks;

    for (int t = 0; t < kMaxTracks; ++t)
        armed[(size_t) t] = s != nullptr && t < s->numTracks && s->tracks[(size_t) t].armed;

    if (s != nullptr && ! juce::isPositiveAndBelow (s->activeMaster, (int) s->masters.size()))
        s->activeMaster = s->masters.empty() ? -1 : 0;
    masterArmedFlag   = s != nullptr && s->masterArmed && s->activeMaster >= 0;
    monitorMasterFlag = s != nullptr && s->monitorMaster;

    rebuildRouting();
    rebuildPlaybackGraph();
}

//==============================================================================
// Device

void TapeEngine::prepare (double sampleRate, int maxBlockSize,
                          const juce::BigInteger& activeInputs, const juce::StringArray& inNames,
                          const juce::BigInteger& activeOutputs, const juce::StringArray& outNames,
                          int inputLatency, int outputLatency)
{
    maxBlock = juce::jmax (16, maxBlockSize);
    trackBuffers.setSize (kMaxTracks, maxBlock);
    trackBuffers.clear();
    mixBus.setSize (2, maxBlock);
    mixBus.clear();
    returnBus.setSize (2, maxBlock);
    masterTapeBus.setSize (2, maxBlock);
    for (auto& d : returnsDelayLine)
        d.prepare ((int) juce::jmax (48000.0, sampleRate)); // up to 1 s of outboard / converter delay
    zeros.setSize (1, maxBlock);
    zeros.clear();

    auto buildMap = [] (std::array<int, 256>& map, const juce::BigInteger& active)
    {
        map.fill (-1);
        int idx = 0;
        for (int ch = 0; ch < (int) map.size(); ++ch)
            if (active[ch])
                map[(size_t) ch] = idx++;
    };
    buildMap (absToActive, activeInputs);
    buildMap (absToActiveOut, activeOutputs);

    inputNames = inNames;
    outputNames = outNames;
    deviceSampleRate = sampleRate;
    blockSize = maxBlockSize;
    latencySamples = juce::jmax (0, inputLatency) + juce::jmax (0, outputLatency);
    deviceLost = false;
    deviceRunning = true;
}

void TapeEngine::release()
{
    deviceRunning = false;
}

void TapeEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    prepare (device->getCurrentSampleRate(),
             device->getCurrentBufferSizeSamples(),
             device->getActiveInputChannels(),
             device->getInputChannelNames(),
             device->getActiveOutputChannels(),
             device->getOutputChannelNames(),
             device->getInputLatencyInSamples(),
             device->getOutputLatencyInSamples());
}

void TapeEngine::audioDeviceStopped()
{
    if (playing || recording)
        deviceLost = true;

    release();
}

void TapeEngine::audioDeviceError (const juce::String& errorMessage)
{
    {
        const juce::ScopedLock sl (deviceErrorLock);
        pendingDeviceError = errorMessage;
    }

    if (playing || recording)
        deviceLost = true;
}

void TapeEngine::audioDeviceIOCallbackWithContext (const float* const* in, int numIn,
                                                   float* const* out, int numOut,
                                                   int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    process (in, numIn, out, numOut, numSamples);
}

//==============================================================================
// Audio thread

void TapeEngine::process (const float* const* inputs, int numInputs,
                          float* const* outputs, int numOutputs, int numSamples) noexcept
{
    numInputs  = juce::jlimit (0, 256, numInputs);
    numOutputs = juce::jlimit (0, 256, numOutputs);

    for (int o = 0; o < numOutputs; ++o)
        if (outputs[o] != nullptr)
            juce::FloatVectorOperations::clear (outputs[o], numSamples);

    if (maxBlock > 0)
    {
        std::array<const float*, 256> in {};
        std::array<float*, 256> out {};

        for (int done = 0; done < numSamples;)
        {
            const int n = juce::jmin (maxBlock, numSamples - done);

            for (int i = 0; i < numInputs; ++i)  in[(size_t) i]  = inputs[i]  != nullptr ? inputs[i]  + done : nullptr;
            for (int o = 0; o < numOutputs; ++o) out[(size_t) o] = outputs[o] != nullptr ? outputs[o] + done : nullptr;

            processBlock (in.data(), numInputs, out.data(), numOutputs, n);
            done += n;
        }
    }

    callbackCount.fetch_add (1);
}

void TapeEngine::processBlock (const float* const* inputs, int numInputs,
                               float* const* outputs, int numOutputs, int n) noexcept
{
    const bool isRolling = playing.load();
    const bool isRec     = isRolling && recording.load();
    auto* g  = graph.load();
    auto* rp = isRec ? pass.load() : nullptr;
    auto pos = position.load();

    if (rp != nullptr && rp->startPos.load() < 0)
        rp->startPos.store (pos);

    float* mixL = mixBus.getWritePointer (0);
    float* mixR = mixBus.getWritePointer (1);
    juce::FloatVectorOperations::clear (mixL, n);
    juce::FloatVectorOperations::clear (mixR, n);

    auto inputFor = [&] (int absCh) -> const float*
    {
        const int a = juce::isPositiveAndBelow (absCh, (int) absToActive.size()) ? absToActive[(size_t) absCh] : -1;
        return juce::isPositiveAndBelow (a, numInputs) ? inputs[a] : nullptr;
    };

    // Adds a signal to every device output set in a mask.
    auto sendToOutputs = [&] (const float* sig, const std::vector<int>& chans)
    {
        for (auto ch : chans)
        {
            const int a = juce::isPositiveAndBelow (ch, (int) absToActiveOut.size()) ? absToActiveOut[(size_t) ch] : -1;
            if (juce::isPositiveAndBelow (a, numOutputs) && outputs[a] != nullptr)
                juce::FloatVectorOperations::add (outputs[a], sig, n);
        }
    };

    auto* rt = routing.load();
    const int nt = numTracks.load();

    for (int t = 0; t < nt; ++t)
    {
        const float* src = rt != nullptr ? inputFor (rt->input[(size_t) t]) : nullptr;
        const bool isArmed = armed[(size_t) t].load (std::memory_order_relaxed);

        // --- Record: push every armed track, silence if its input vanished,
        //     so every take stays exactly the length of the pass.
        if (rp != nullptr)
            if (auto* stream = rp->streams[(size_t) t].get())
                stream->push (src != nullptr ? src : zeros.getReadPointer (0), n);

        // --- Playback from tape
        bool hasTape = false;
        float* tb = trackBuffers.getWritePointer (t);

        // (Tape is read even under a recording track so its reader stays warm
        //  for a seamless punch-out.)
        if (isRolling && g != nullptr)
        {
            for (const auto& seg : g->tracks[(size_t) t])
            {
                if (seg.end <= pos)      continue;
                if (seg.start >= pos + n) break;

                if (! hasTape)
                {
                    juce::FloatVectorOperations::clear (tb, n);
                    hasTape = true;
                }

                const auto s0 = juce::jmax (seg.start, pos);
                const auto s1 = juce::jmin (seg.end, pos + n);
                float* dest = tb + (s0 - pos);

                if (! seg.reader->read (&dest, 1, seg.fileStart + (s0 - seg.start), (int) (s1 - s0)))
                    playbackUnderruns.fetch_add (1, std::memory_order_relaxed);
            }
        }

        // --- Tape-style auto input: armed tracks hear the input when stopped or
        //     recording, and hear tape when just playing back.
        const float* mon = (isArmed && (! isRolling || isRec)) ? src : (hasTape ? tb : nullptr);

        trackEnergy[(size_t) t].add (mon, n);

        if (mon != nullptr)
        {
            raisePeak (trackPeak[(size_t) t], peakOf (mon, n));

            if (rt != nullptr && rt->toMix[(size_t) t])
            {
                juce::FloatVectorOperations::add (mixL, mon, n);
                juce::FloatVectorOperations::add (mixR, mon, n);
            }

            if (rt != nullptr)
                sendToOutputs (mon, rt->outputs[(size_t) t]);
        }
    }

    // --- Master bus
    //  1. internal mix of the tracks (in step with the tape)
    //  2. + patched-in returns. They come back one round trip late, so the
    //     internal mix is delayed by that much to meet them.
    //  3. optional insert: out to the outboard, back in (another round trip)
    //  4. recorded to the master with the total lag taken off, so it lands
    //     sample-aligned with the tracks.
    const int roundTrip = getRoundTrip();
    const bool recMaster = rp != nullptr && rp->masterStreams[0] != nullptr;
    const bool monitorTape = monitorMasterFlag.load (std::memory_order_relaxed) && ! recMaster;

    for (int side = 0; side < 2; ++side)
    {
        float* bus = side == 0 ? mixL : mixR;

        if (rt != nullptr && rt->hasReturns)
        {
            returnsDelayLine[(size_t) side].process (bus, n, roundTrip);
            for (auto ch : rt->masterIn[(size_t) side])
                if (auto* in = inputFor (ch))
                    juce::FloatVectorOperations::add (bus, in, n);
        }

        if (rt != nullptr && rt->insert)
        {
            const int sendCh = rt->insertSend[(size_t) side];
            const int a = juce::isPositiveAndBelow (sendCh, (int) absToActiveOut.size()) ? absToActiveOut[(size_t) sendCh] : -1;
            if (juce::isPositiveAndBelow (a, numOutputs) && outputs[a] != nullptr)
                juce::FloatVectorOperations::add (outputs[a], bus, n);

            if (auto* ret = inputFor (rt->insertReturn[(size_t) side]))
                juce::FloatVectorOperations::copy (bus, ret, n);
            else
                juce::FloatVectorOperations::clear (bus, n);
        }

        if (recMaster)
            rp->masterStreams[(size_t) side]->push (bus, n);

        // Master tape playback
        const float* outSig = bus;
        if (monitorTape && isRolling && g != nullptr && ! g->master[(size_t) side].empty())
        {
            float* tb = masterTapeBus.getWritePointer (side);
            juce::FloatVectorOperations::clear (tb, n);

            for (const auto& seg : g->master[(size_t) side])
            {
                if (seg.end <= pos)       continue;
                if (seg.start >= pos + n) break;
                const auto s0 = juce::jmax (seg.start, pos);
                const auto s1 = juce::jmin (seg.end, pos + n);
                float* dest = tb + (s0 - pos);
                if (! seg.reader->read (&dest, 1, seg.fileStart + (s0 - seg.start), (int) (s1 - s0)))
                    playbackUnderruns.fetch_add (1, std::memory_order_relaxed);
            }
            outSig = tb;
        }

        raisePeak (masterPeak[(size_t) side], peakOf (outSig, n));
        masterEnergy[(size_t) side].add (outSig, n);
        if (rt != nullptr)
            sendToOutputs (outSig, rt->masterOut[(size_t) side]);
    }

    // --- Latency ping: one click out, time it coming back
    if (pingState.load (std::memory_order_relaxed) == PingState::running)
    {
        if (pingRequested.exchange (false))
        {
            const int a = juce::isPositiveAndBelow (pingOut.load(), (int) absToActiveOut.size()) ? absToActiveOut[(size_t) pingOut.load()] : -1;
            if (juce::isPositiveAndBelow (a, numOutputs) && outputs[a] != nullptr)
            {
                outputs[a][0] += 0.5f;
                pingSentAt = deviceClock;
            }
            else
                pingState = PingState::failed;
        }
        else if (pingSentAt >= 0)
        {
            if (auto* in = inputFor (pingIn.load()))
            {
                for (int i = 0; i < n; ++i)
                    if (std::abs (in[i]) > 0.05f)
                    {
                        pingResult = (int) (deviceClock + i - pingSentAt);
                        pingState = PingState::done;
                        break;
                    }
            }

            if (pingState.load() == PingState::running && deviceClock - pingSentAt > (juce::int64) deviceSampleRate.load())
                pingState = PingState::failed; // nothing came back within a second
        }
    }

    deviceClock += n;

    // Only advance if nobody located us mid-block.
    if (isRolling)
        position.compare_exchange_strong (pos, pos + n);
}

//==============================================================================
// Message thread: transport

juce::String TapeEngine::checkCanRoll() const
{
    if (reel == nullptr)
        return "No reel is on the deck - load one from the Library.";

    if (! deviceRunning)
        return "The audio device is not running.\nOpen Audio Settings and choose a device.";

    if (std::abs (deviceSampleRate.load() - reel->sampleRate) > 0.5)
        return "The audio device is running at " + juce::String (deviceSampleRate.load() / 1000.0, 1)
             + " kHz but this reel is " + juce::String (reel->sampleRate / 1000.0, 1)
             + " kHz.\nChange the sample rate in Audio Settings.";

    return {};
}

juce::String TapeEngine::play()
{
    if (playing)
        return {};

    if (auto err = checkCanRoll(); err.isNotEmpty())
        return err;

    primeReaders (position.load());
    playing = true;
    return {};
}

juce::String TapeEngine::record()
{
    if (recording)
        return {};

    if (auto err = checkCanRoll(); err.isNotEmpty())
        return err;

    if (auto err = beginRecordPass(); err.isNotEmpty())
        return err;

    if (! playing)
        primeReaders (position.load());

    recording = true;
    playing = true;
    return {};
}

void TapeEngine::punchOut()
{
    if (recording || ownedPass != nullptr)
        endRecordPass();
}

void TapeEngine::stop()
{
    punchOut();
    playing = false;
}

void TapeEngine::locate (juce::int64 samplePos)
{
    if (recording)
        return;

    position.store (juce::jmax ((juce::int64) 0, samplePos));

    if (! playing)
        primeReaders (position.load());
}

//==============================================================================
// Message thread: tracks

void TapeEngine::setArmed (int t, bool shouldArm)
{
    if (! juce::isPositiveAndBelow (t, numTracks.load()) || recording)
        return;

    armed[(size_t) t] = shouldArm;

    if (reel != nullptr)
    {
        reel->tracks[(size_t) t].armed = shouldArm;
        saveReel();
    }
}

void TapeEngine::setInput (int t, int ch)
{
    if (reel == nullptr || ! juce::isPositiveAndBelow (t, numTracks.load()) || (recording && armed[(size_t) t]))
        return;

    reel->tracks[(size_t) t].input = ch;
    rebuildRouting();
    saveReel();
}

void TapeEngine::setTrackToMix (int t, bool on)
{
    if (reel == nullptr || ! juce::isPositiveAndBelow (t, kMaxTracks)) return;
    reel->tracks[(size_t) t].toMix = on;
    rebuildRouting();
    saveReel();
}

void TapeEngine::setTrackOutputs (int t, const ChannelList& outs)
{
    if (reel == nullptr || ! juce::isPositiveAndBelow (t, kMaxTracks)) return;
    reel->tracks[(size_t) t].outputs = outs;
    rebuildRouting();
    saveReel();
}

void TapeEngine::setMasterInputs (int side, const ChannelList& ins)
{
    if (reel == nullptr) return;
    reel->master.inputs[(size_t) (side & 1)] = ins;
    rebuildRouting();
    saveReel();
}

void TapeEngine::setMasterOutputs (int side, const ChannelList& outs)
{
    if (reel == nullptr) return;
    reel->master.outputs[(size_t) (side & 1)] = outs;
    rebuildRouting();
    saveReel();
}

void TapeEngine::setMasterInsert (bool enabled, std::array<int, 2> sends, std::array<int, 2> returns)
{
    if (reel == nullptr || (recording && masterArmedFlag)) return;
    reel->master.insertEnabled = enabled;
    reel->master.insertSend = sends;
    reel->master.insertReturn = returns;
    rebuildRouting();
    saveReel();
}

//==============================================================================
// Message thread: masters

void TapeEngine::setActiveMaster (int index)
{
    if (reel == nullptr || recording) return;
    reel->activeMaster = juce::isPositiveAndBelow (index, (int) reel->masters.size()) ? index : -1;
    if (reel->activeMaster < 0) setMasterArmed (false);
    saveReel();
    rebuildPlaybackGraph();
}

void TapeEngine::setMasterArmed (bool on)
{
    if (reel == nullptr || recording) return;
    on = on && reel->activeMaster >= 0;
    masterArmedFlag = on;
    reel->masterArmed = on;
    saveReel();
}

void TapeEngine::setMonitorMaster (bool on)
{
    monitorMasterFlag = on;
    if (reel != nullptr) { reel->monitorMaster = on; saveReel(); }
}

void TapeEngine::mastersChanged()
{
    if (reel != nullptr && ! juce::isPositiveAndBelow (reel->activeMaster, (int) reel->masters.size()))
        reel->activeMaster = reel->masters.empty() ? -1 : 0;
    if (reel == nullptr || reel->activeMaster < 0)
        masterArmedFlag = false;
    rebuildPlaybackGraph();
}

//==============================================================================
// Latency

int TapeEngine::getRoundTrip() const noexcept
{
    const int m = measuredRoundTrip.load();
    return m > 0 ? m : latencySamples.load();
}

int TapeEngine::getInsertRoundTrip() const noexcept
{
    const int m = measuredInsertRoundTrip.load();
    return m > 0 ? m : getRoundTrip();
}

int TapeEngine::getMasterBusLatency() const noexcept
{
    auto* rt = ownedRouting.get();
    if (rt == nullptr) return 0;
    return (rt->hasReturns ? getRoundTrip() : 0) + (rt->insert ? getInsertRoundTrip() : 0);
}

bool TapeEngine::startPing (int outputChannel, int inputChannel)
{
    if (! deviceRunning || recording || pingState.load() == PingState::running)
        return false;

    pingOut = outputChannel;
    pingIn = inputChannel;
    pingResult = 0;
    pingSentAt = -1;           // audio thread only touches it while running
    pingRequested = true;
    pingState = PingState::running;
    return true;
}

void TapeEngine::rebuildRouting()
{
    auto r = std::make_unique<RoutingTable>();

    for (int t = 0; t < kMaxTracks; ++t)
    {
        if (reel != nullptr)
        {
            const auto& tr = reel->tracks[(size_t) t];
            r->input[(size_t) t] = tr.input;
            r->toMix[(size_t) t] = tr.toMix;
            r->outputs[(size_t) t].assign (tr.outputs.begin(), tr.outputs.end());
        }
        else
        {
            r->input[(size_t) t] = t;
            r->toMix[(size_t) t] = true;
        }
    }

    for (int side = 0; side < 2; ++side)
    {
        if (reel != nullptr)
        {
            const auto& m = reel->master;
            r->masterIn[(size_t) side].assign (m.inputs[(size_t) side].begin(), m.inputs[(size_t) side].end());
            r->masterOut[(size_t) side].assign (m.outputs[(size_t) side].begin(), m.outputs[(size_t) side].end());
            r->hasReturns = r->hasReturns || ! m.inputs[(size_t) side].isEmpty();
        }
        else
        {
            r->masterOut[(size_t) side] = { side };
        }
    }

    if (reel != nullptr)
    {
        r->insert = reel->master.insertEnabled;
        r->insertSend = reel->master.insertSend;
        r->insertReturn = reel->master.insertReturn;
    }

    auto old = std::move (ownedRouting);
    ownedRouting = std::move (r);
    routing.store (ownedRouting.get());
    retire (std::move (old));
}

int TapeEngine::getNumArmedTracks() const noexcept
{
    int n = 0;
    for (int t = 0; t < numTracks.load(); ++t)
        n += armed[(size_t) t].load() ? 1 : 0;
    return n;
}

double TapeEngine::getRecordTimeRemainingSeconds() const
{
    if (reel == nullptr)
        return 0.0;

    const auto freeBytes = (double) reel->getFolder().getBytesFreeOnVolume();
    const int files = getNumArmedTracks() + (masterArmedFlag ? 2 : 0);
    const auto bytesPerSec = (double) juce::jmax (1, files) * reel->sampleRate * (reel->bitDepth / 8);
    return juce::jmax (0.0, freeBytes - (double) stopRecordingBelowBytes) / bytesPerSec;
}

//==============================================================================
// Message thread: record passes

juce::String TapeEngine::beginRecordPass()
{
    jassert (ownedPass == nullptr);

    int numArmed = 0;

    for (int t = 0; t < numTracks.load(); ++t)
    {
        if (! armed[(size_t) t])
            continue;

        ++numArmed;
        const int ch = reel->tracks[(size_t) t].input;

        if (! juce::isPositiveAndBelow (ch, (int) absToActive.size()) || absToActive[(size_t) ch] < 0)
            return reel->tracks[(size_t) t].name + " is armed but has no active input.\n"
                   "Choose an input for it, or enable more inputs in Audio Settings.";
    }

    const bool recMaster = masterArmedFlag && juce::isPositiveAndBelow (reel->activeMaster, (int) reel->masters.size());

    if (numArmed == 0 && ! recMaster)
        return "Nothing is armed.";

    if (reel->getFolder().getBytesFreeOnVolume() < minFreeBytesToRecord)
        return "Not enough free disk space to record safely.\nAt least 500 MB is required.";

    const auto sr = reel->sampleRate;
    // 20 s of buffer per track, less when very many tracks are armed (keeps RAM sane at 96 tracks).
    const double secs = juce::jlimit (5.0, fifoSeconds, fifoBudgetBytes / ((double) (numArmed + (recMaster ? 2 : 0)) * sr * sizeof (float)));
    const int fifoSize = fifoSizeOverride > 0 ? fifoSizeOverride : (int) (sr * secs);
    const int flushInterval = (int) (sr * flushIntervalSeconds);

    auto p = std::make_unique<RecordPass>();
    p->latency = getRoundTrip();
    p->masterLatency = getMasterBusLatency();

    auto abandon = [&p]
    {
        for (auto& s : p->streams)       if (s != nullptr) s->finish();
        for (auto& s : p->masterStreams) if (s != nullptr) s->finish();
    };

    auto openStream = [&] (const juce::File& file, int latency, juce::String& err) -> std::shared_ptr<RecordingStream>
    {
        if (! file.existsAsFile() && ! TapeFile::createEmpty (file, sr, err))
            return nullptr;

        auto tape = std::make_unique<TapeFile>();
        if (! tape->open (file, sr, err))
            return nullptr;

        return std::make_shared<RecordingStream> (std::move (tape), file, fifoSize, flushInterval, &p->startPos, latency);
    };

    for (int t = 0; t < numTracks.load(); ++t)
    {
        if (! armed[(size_t) t])
            continue;

        juce::String err;
        p->streams[(size_t) t] = openStream (reel->getTapeFile (t), p->latency, err);
        if (p->streams[(size_t) t] == nullptr) { abandon(); return err; }
    }

    if (recMaster)
    {
        p->masterName = reel->masters[(size_t) reel->activeMaster].name;
        for (int side = 0; side < 2; ++side)
        {
            juce::String err;
            p->masterStreams[(size_t) side] = openStream (reel->getMasterFile (reel->activeMaster, side), p->masterLatency, err);
            if (p->masterStreams[(size_t) side] == nullptr) { abandon(); return err; }
        }
    }

    for (auto& s : p->streams)
        if (s != nullptr)
            diskThread.add (s);
    for (auto& s : p->masterStreams)
        if (s != nullptr)
            diskThread.add (s);

    droppedBeforePass = droppedTotal.load();
    diskBufferPeak = 0.0f;

    ownedPass = std::move (p);
    pass.store (ownedPass.get());
    return {};
}

void TapeEngine::endRecordPass()
{
    recording = false;

    if (ownedPass == nullptr)
        return;

    pass.store (nullptr);
    const bool released = waitForAudioThread();

    std::unique_ptr<RecordPass> p = std::move (ownedPass);
    juce::StringArray problems;
    juce::int64 droppedThisPass = 0, passLength = 0;
    juce::Array<int> recorded;

    for (int t = 0; t < kMaxTracks; ++t)
    {
        auto& s = p->streams[(size_t) t];
        if (s == nullptr)
            continue;

        diskThread.remove (s.get());

        if (! s->finish())
            problems.add (reel->tracks[(size_t) t].name + ": writing to tape failed");

        droppedThisPass += s->getDroppedSamples();
        passLength = juce::jmax (passLength, s->getTimelineLength());
        ++tapeGeneration[reel->getTapeFile (t).getFullPathName()]; // tape changed: readers must reload
        recorded.add (t);
    }

    juce::int64 masterLength = 0;
    for (int side = 0; side < 2; ++side)
    {
        auto& s = p->masterStreams[(size_t) side];
        if (s == nullptr)
            continue;

        diskThread.remove (s.get());
        if (! s->finish())
            problems.add (p->masterName + ": writing to tape failed");

        droppedThisPass += s->getDroppedSamples();
        masterLength = juce::jmax (masterLength, s->getTimelineLength());
        ++tapeGeneration[s->getFile().getFullPathName()];
    }

    droppedTotal = droppedBeforePass.load() + droppedThisPass;

    // Every recorded track is kept the length of the reel (silence where unrecorded).
    juce::String padError;
    for (auto& f : reel->equaliseTapeLengths (padError))
        ++tapeGeneration[f.getFullPathName()];
    if (padError.isNotEmpty())
        problems.add (padError);

    // Recording sheet: one line per take
    const bool tracksRecorded = ! recorded.isEmpty();
    const int lat = tracksRecorded ? p->latency : p->masterLatency;
    if (! tracksRecorded) passLength = masterLength;

    if (const auto startPos = p->startPos.load(); startPos >= 0 && passLength > 0)
    {
        TakeEntry e;
        e.take   = reel->nextTake++;
        e.when   = (juce::Time::getCurrentTime() - juce::RelativeTime::seconds ((double) passLength / reel->sampleRate))
                       .formatted ("%d %b %Y  %H:%M");
        e.start  = juce::jmax ((juce::int64) 0, startPos - lat);
        e.end    = startPos - lat + passLength;

        // "1-4, 7" style track list
        juce::StringArray parts;
        for (int i = 0; i < recorded.size();)
        {
            int j = i;
            while (j + 1 < recorded.size() && recorded[j + 1] == recorded[j] + 1) ++j;
            parts.add (i == j ? juce::String (recorded[i] + 1)
                              : juce::String (recorded[i] + 1) + "-" + juce::String (recorded[j] + 1));
            i = j + 1;
        }
        e.tracks = parts.joinIntoString (", ");
        if (p->masterName.isNotEmpty())
            e.tracks = e.tracks.isEmpty() ? p->masterName : e.tracks + " + " + p->masterName;
        reel->takeLog.push_back (e);

        if (onTakeLogged)
            onTakeLogged (e);
    }

    if (! released)
        graveyard.push_back (std::shared_ptr<void> (p.release(), [] (void* x) { delete static_cast<RecordPass*> (x); }));

    saveReel();
    rebuildPlaybackGraph();

    if (droppedThisPass > 0 && reel != nullptr)
        alert ("Disk could not keep up",
               juce::String ((double) droppedThisPass / reel->sampleRate * 1000.0, 0)
               + " ms of audio was recorded as silence.\n"
                 "Timing is preserved, but use a faster drive or close other programs.");

    if (! problems.isEmpty())
        alert ("Recording problem", problems.joinIntoString ("\n"));
}

//==============================================================================
// Message thread: playback graph

std::shared_ptr<juce::BufferingAudioReader> TapeEngine::getReader (const juce::File& f, const juce::String& key, bool& isNew)
{
    isNew = false;

    if (auto it = readerCache.find (key); it != readerCache.end())
        return it->second;

    if (! f.existsAsFile())
        return nullptr;

    auto* source = formatManager.createReaderFor (f);
    if (source == nullptr)
        return nullptr;

    const auto sr = reel != nullptr ? reel->sampleRate : 48000.0;
    auto reader = std::make_shared<juce::BufferingAudioReader> (source, prefetchThread, (int) juce::jmax (65536.0, sr * 2.0));
    reader->setReadTimeout (0); // never block the audio thread
    readerCache[key] = reader;
    isNew = true;
    return reader;
}

void TapeEngine::rebuildPlaybackGraph()
{
    // One reader per tape track. A track's reader is only replaced when its
    // tape has been recorded on (generation bump); others keep playing untouched.
    auto g = std::make_unique<PlaybackGraph>();
    std::vector<Segment> fresh;

    auto addFile = [&] (const juce::File& file, std::vector<Segment>& dest)
    {
        const auto key = file.getFullPathName() + "|" + juce::String (tapeGeneration[file.getFullPathName()]);
        bool isNew = false;
        auto reader = getReader (file, key, isNew);

        if (reader == nullptr || reader->lengthInSamples <= 0)
            return;

        const Segment seg { reader.get(), 0, reader->lengthInSamples, 0 };
        dest.push_back (seg);
        g->keepAlive.push_back (reader);

        if (isNew)
            fresh.push_back (seg);
    };

    if (reel != nullptr)
    {
        for (int t = 0; t < reel->numTracks; ++t)
        {
            const auto file = reel->getTapeFile (t);
            if (! file.existsAsFile())
                continue;

            addFile (file, g->tracks[(size_t) t]);
        }

        if (juce::isPositiveAndBelow (reel->activeMaster, (int) reel->masters.size()))
            for (int side = 0; side < 2; ++side)
            {
                const auto file = reel->getMasterFile (reel->activeMaster, side);
                if (file.existsAsFile())
                    addFile (file, g->master[(size_t) side]);
            }
    }

    // Prime new readers before the audio thread can see them.
    const auto pos = position.load();
    const auto window = (juce::int64) ((reel != nullptr ? reel->sampleRate : 48000.0) * 0.5);
    for (auto& seg : fresh)
        primeSegment (seg, pos, seg.end > pos && seg.start < pos + window);

    auto old = std::move (ownedGraph);
    ownedGraph = std::move (g);
    graph.store (ownedGraph.get());
    retire (std::move (old));

    for (auto it = readerCache.begin(); it != readerCache.end();)
        it = it->second.use_count() == 1 ? readerCache.erase (it) : std::next (it);
}

juce::int64 TapeEngine::getReelLength() const
{
    juce::int64 reel = 0;
    for (int t = 0; t < kMaxTracks; ++t)
        reel = juce::jmax (reel, getTapeLength (t));
    if (ownedGraph != nullptr)
        for (auto& m : ownedGraph->master)
            if (! m.empty()) reel = juce::jmax (reel, m.back().end);
    return reel;
}

juce::int64 TapeEngine::getTapeLength (int track) const
{
    if (ownedGraph == nullptr || ! juce::isPositiveAndBelow (track, kMaxTracks))
        return 0;

    const auto& segs = ownedGraph->tracks[(size_t) track];
    return segs.empty() ? 0 : segs.back().end;
}

void TapeEngine::primeSegment (const Segment& seg, juce::int64 pos, bool waitUntilLoaded)
{
    // Point the reader's read-ahead at where playback will first need it.
    const auto tl = juce::jlimit (seg.start, seg.end - 1, pos);
    const auto filePos = seg.fileStart + (tl - seg.start);
    const int n = (int) juce::jmin ((juce::int64) 4096, seg.end - tl);

    std::array<float, 4096> scratch;
    float* dest = scratch.data();
    const auto deadline = juce::Time::getMillisecondCounter() + 2000;

    while (! seg.reader->read (&dest, 1, filePos, n)
           && waitUntilLoaded
           && juce::Time::getMillisecondCounter() < deadline)
        juce::Thread::sleep (1);
}

void TapeEngine::primeReaders (juce::int64 pos)
{
    // Only called while stopped, so the audio thread is not reading.
    if (ownedGraph == nullptr || reel == nullptr)
        return;

    const auto window = (juce::int64) (reel->sampleRate * 0.5);

    for (auto& segs : ownedGraph->tracks)
        for (auto& seg : segs)
            primeSegment (seg, pos, seg.end > pos && seg.start < pos + window);
    for (auto& segs : ownedGraph->master)
        for (auto& seg : segs)
            primeSegment (seg, pos, seg.end > pos && seg.start < pos + window);
}

//==============================================================================
// Message thread: lifetime + housekeeping

bool TapeEngine::waitForAudioThread()
{
    if (! deviceRunning)
        return true;

    const auto target = callbackCount.load() + 2;
    const auto t0 = juce::Time::getMillisecondCounter();

    while (callbackCount.load() < target)
    {
        if (! deviceRunning)
            return true;

        if (juce::Time::getMillisecondCounter() - t0 > 1000)
            return false;

        juce::Thread::sleep (1);
    }

    return true;
}

template <typename T>
void TapeEngine::retire (std::unique_ptr<T> p)
{
    if (p == nullptr)
        return;

    if (waitForAudioThread())
        p.reset();
    else
        graveyard.push_back (std::shared_ptr<void> (p.release(), [] (void* x) { delete static_cast<T*> (x); }));
}

void TapeEngine::alert (const juce::String& title, const juce::String& msg)
{
    if (onAlert)
        onAlert (title, msg);
}

bool TapeEngine::saveReel()
{
    if (reel == nullptr)
        return false;

    juce::String err;
    if (reel->save (err))
        return true;

    alert ("Could not save the reel", err);
    return false;
}

void TapeEngine::tick()
{
    if (deviceLost.exchange (false))
    {
        const bool wasRecording = recording.load() || ownedPass != nullptr;
        stop();

        juce::String detail;
        {
            const juce::ScopedLock sl (deviceErrorLock);
            detail = pendingDeviceError;
            pendingDeviceError.clear();
        }

        alert ("Audio device stopped",
               (wasRecording ? "Recording stopped. Everything recorded up to that point has been saved.\n" : "")
               + (detail.isNotEmpty() ? "\n" + detail : juce::String()));
    }

    auto* p = ownedPass.get();
    if (p == nullptr)
        return;

    juce::int64 dropped = 0;
    float bufferPeak = 0.0f;
    bool writeFailed = false;

    auto check = [&] (const std::shared_ptr<RecordingStream>& s)
    {
        if (s != nullptr)
        {
            dropped    += s->getDroppedSamples();
            bufferPeak  = juce::jmax (bufferPeak, s->getPeakFifoUsage());
            writeFailed = writeFailed || s->hasError();
        }
    };
    for (auto& s : p->streams)       check (s);
    for (auto& s : p->masterStreams) check (s);

    droppedTotal = droppedBeforePass.load() + dropped;
    diskBufferPeak = bufferPeak;

    if (writeFailed)
    {
        punchOut();
        alert ("Disk write error",
               "Hawthorne could not write to disk, so recording was stopped.\n"
               "Everything recorded up to that point has been kept.");
        return;
    }

    static int freeCheckCounter = 0;
    if (++freeCheckCounter % 10 == 0
        && reel->getFolder().getBytesFreeOnVolume() < stopRecordingBelowBytes)
    {
        punchOut();
        alert ("Disk almost full", "Recording was stopped because the disk is nearly full.\n"
                                   "The take has been saved.");
    }
}

} // namespace hawthorne
