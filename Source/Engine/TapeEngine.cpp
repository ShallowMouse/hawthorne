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

    for (int t = 0; t < kNumTracks; ++t)
    {
        armed[(size_t) t] = false;
        inputChannel[(size_t) t] = t;
        trackPeak[(size_t) t] = 0.0f;
    }

    masterPeak[0] = masterPeak[1] = 0.0f;
}

TapeEngine::~TapeEngine()
{
    stop();
    auto old = std::move (ownedGraph);
    graph.store (nullptr);
    retire (std::move (old));
    readerCache.clear();
    prefetchThread.stopThread (2000);
}

//==============================================================================
void TapeEngine::setSession (Session* s)
{
    stop();
    session = s;
    position = 0;
    droppedTotal = 0;
    droppedBeforePass = 0;
    playbackUnderruns = 0;

    if (s != nullptr)
        for (int t = 0; t < kNumTracks; ++t)
        {
            armed[(size_t) t]        = s->tracks[(size_t) t].armed;
            inputChannel[(size_t) t] = s->tracks[(size_t) t].input;
        }

    rebuildPlaybackGraph();
}

//==============================================================================
// Device

void TapeEngine::prepare (double sampleRate, int maxBlockSize, const juce::BigInteger& activeInputs,
                          const juce::StringArray& names, int inputLatency, int outputLatency)
{
    maxBlock = juce::jmax (16, maxBlockSize);
    trackBuffers.setSize (kNumTracks, maxBlock);
    trackBuffers.clear();
    zeros.setSize (1, maxBlock);
    zeros.clear();

    absToActive.fill (-1);
    int idx = 0;
    for (int ch = 0; ch < (int) absToActive.size(); ++ch)
        if (activeInputs[ch])
            absToActive[(size_t) ch] = idx++;

    inputNames = names;
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

    float* outL = numOutputs > 0 ? outputs[0] : nullptr;
    float* outR = numOutputs > 1 ? outputs[1] : nullptr;

    for (int t = 0; t < kNumTracks; ++t)
    {
        const int ch = inputChannel[(size_t) t].load (std::memory_order_relaxed);
        const int active = juce::isPositiveAndBelow (ch, (int) absToActive.size()) ? absToActive[(size_t) ch] : -1;
        const float* src = juce::isPositiveAndBelow (active, numInputs) ? inputs[active] : nullptr;
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

            if (outL != nullptr) juce::FloatVectorOperations::add (outL, mon, n);
            if (outR != nullptr) juce::FloatVectorOperations::add (outR, mon, n);
        }
    }

    if (outL != nullptr) { raisePeak (masterPeak[0], peakOf (outL, n)); masterEnergy[0].add (outL, n); }
    if (outR != nullptr) { raisePeak (masterPeak[1], peakOf (outR, n)); masterEnergy[1].add (outR, n); }

    // Only advance if nobody located us mid-block.
    if (isRolling)
        position.compare_exchange_strong (pos, pos + n);
}

//==============================================================================
// Message thread: transport

juce::String TapeEngine::checkCanRoll() const
{
    if (session == nullptr)
        return "No session is open.";

    if (! deviceRunning)
        return "The audio device is not running.\nOpen Audio Settings and choose a device.";

    if (std::abs (deviceSampleRate.load() - session->sampleRate) > 0.5)
        return "The audio device is running at " + juce::String (deviceSampleRate.load() / 1000.0, 1)
             + " kHz but this session is " + juce::String (session->sampleRate / 1000.0, 1)
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
    if (! juce::isPositiveAndBelow (t, kNumTracks) || recording)
        return;

    armed[(size_t) t] = shouldArm;

    if (session != nullptr)
    {
        session->tracks[(size_t) t].armed = shouldArm;
        saveSession();
    }
}

void TapeEngine::setInput (int t, int ch)
{
    if (! juce::isPositiveAndBelow (t, kNumTracks) || (recording && armed[(size_t) t]))
        return;

    inputChannel[(size_t) t] = ch;

    if (session != nullptr)
    {
        session->tracks[(size_t) t].input = ch;
        saveSession();
    }
}

int TapeEngine::getNumArmedTracks() const noexcept
{
    int n = 0;
    for (auto& a : armed)
        n += a.load() ? 1 : 0;
    return n;
}

double TapeEngine::getRecordTimeRemainingSeconds() const
{
    if (session == nullptr)
        return 0.0;

    const auto freeBytes = (double) session->getFolder().getBytesFreeOnVolume();
    const auto bytesPerSec = (double) juce::jmax (1, getNumArmedTracks()) * session->sampleRate * (session->bitDepth / 8);
    return juce::jmax (0.0, freeBytes - (double) stopRecordingBelowBytes) / bytesPerSec;
}

//==============================================================================
// Message thread: record passes

juce::String TapeEngine::beginRecordPass()
{
    jassert (ownedPass == nullptr);

    int numArmed = 0;

    for (int t = 0; t < kNumTracks; ++t)
    {
        if (! armed[(size_t) t])
            continue;

        ++numArmed;
        const int ch = inputChannel[(size_t) t];

        if (! juce::isPositiveAndBelow (ch, (int) absToActive.size()) || absToActive[(size_t) ch] < 0)
            return session->tracks[(size_t) t].name + " is armed but has no active input.\n"
                   "Choose an input for it, or enable more inputs in Audio Settings.";
    }

    if (numArmed == 0)
        return "No tracks are armed.";

    if (session->getFolder().getBytesFreeOnVolume() < minFreeBytesToRecord)
        return "Not enough free disk space to record safely.\nAt least 500 MB is required.";

    const auto sr = session->sampleRate;
    const int fifoSize = fifoSizeOverride > 0 ? fifoSizeOverride : (int) (sr * fifoSeconds);
    const int flushInterval = (int) (sr * flushIntervalSeconds);

    auto p = std::make_unique<RecordPass>();
    p->latency = latencySamples;

    for (int t = 0; t < kNumTracks; ++t)
    {
        if (! armed[(size_t) t])
            continue;

        const auto file = session->getTapeFile (t);
        juce::String err;

        if (! file.existsAsFile() && ! TapeFile::createEmpty (file, sr, err))
            return err;

        auto tape = std::make_unique<TapeFile>();
        if (! tape->open (file, sr, err))
        {
            for (auto& s : p->streams)
                if (s != nullptr) s->finish();
            return err;
        }

        p->streams[(size_t) t] = std::make_shared<RecordingStream> (std::move (tape), file, fifoSize, flushInterval,
                                                                    &p->startPos, p->latency);
    }

    for (auto& s : p->streams)
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
    juce::int64 droppedThisPass = 0;

    for (int t = 0; t < kNumTracks; ++t)
    {
        auto& s = p->streams[(size_t) t];
        if (s == nullptr)
            continue;

        diskThread.remove (s.get());

        if (! s->finish())
            problems.add (session->tracks[(size_t) t].name + ": writing to tape failed");

        droppedThisPass += s->getDroppedSamples();
        ++tapeGeneration[(size_t) t]; // tape contents changed: readers must reload
    }

    droppedTotal = droppedBeforePass.load() + droppedThisPass;

    if (! released)
        graveyard.push_back (std::shared_ptr<void> (p.release(), [] (void* x) { delete static_cast<RecordPass*> (x); }));

    rebuildPlaybackGraph();

    if (droppedThisPass > 0 && session != nullptr)
        alert ("Disk could not keep up",
               juce::String ((double) droppedThisPass / session->sampleRate * 1000.0, 0)
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

    const auto sr = session != nullptr ? session->sampleRate : 48000.0;
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

    if (session != nullptr)
    {
        for (int t = 0; t < kNumTracks; ++t)
        {
            const auto file = session->getTapeFile (t);
            if (! file.existsAsFile())
                continue;

            const auto key = file.getFullPathName() + "|" + juce::String (tapeGeneration[(size_t) t]);
            bool isNew = false;
            auto reader = getReader (file, key, isNew);

            if (reader == nullptr || reader->lengthInSamples <= 0)
                continue;

            const Segment seg { reader.get(), 0, reader->lengthInSamples, 0 };
            g->tracks[(size_t) t].push_back (seg);
            g->keepAlive.push_back (reader);

            if (isNew)
                fresh.push_back (seg);
        }
    }

    // Prime new readers before the audio thread can see them.
    const auto pos = position.load();
    const auto window = (juce::int64) ((session != nullptr ? session->sampleRate : 48000.0) * 0.5);
    for (auto& seg : fresh)
        primeSegment (seg, pos, seg.end > pos && seg.start < pos + window);

    auto old = std::move (ownedGraph);
    ownedGraph = std::move (g);
    graph.store (ownedGraph.get());
    retire (std::move (old));

    for (auto it = readerCache.begin(); it != readerCache.end();)
        it = it->second.use_count() == 1 ? readerCache.erase (it) : std::next (it);
}

juce::int64 TapeEngine::getTapeLength (int track) const
{
    if (ownedGraph == nullptr || ! juce::isPositiveAndBelow (track, kNumTracks))
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
    if (ownedGraph == nullptr || session == nullptr)
        return;

    const auto window = (juce::int64) (session->sampleRate * 0.5);

    for (auto& segs : ownedGraph->tracks)
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

bool TapeEngine::saveSession()
{
    if (session == nullptr)
        return false;

    juce::String err;
    if (session->save (err))
        return true;

    alert ("Could not save session", err);
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

    for (auto& s : p->streams)
        if (s != nullptr)
        {
            dropped    += s->getDroppedSamples();
            bufferPeak  = juce::jmax (bufferPeak, s->getPeakFifoUsage());
            writeFailed = writeFailed || s->hasError();
        }

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
        && session->getFolder().getBytesFreeOnVolume() < stopRecordingBelowBytes)
    {
        punchOut();
        alert ("Disk almost full", "Recording was stopped because the disk is nearly full.\n"
                                   "The take has been saved.");
    }
}

} // namespace hawthorne
