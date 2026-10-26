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
    tapeRec = std::make_unique<std::array<tape::RecordChannel, kMaxTracks>>();
    tapePlay = std::make_unique<std::array<tape::PlaybackChannel, kMaxTracks>>();
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
    targetRate = currentRate = s != nullptr ? s->getSpeedRatio() : 1.0;
    windCancel();
    tapeStock = s != nullptr ? s->tapeStock : 0;
    machineA = s != nullptr ? s->machine : 0;
    masterMachineA = s != nullptr ? s->masterMachine : 0;
    reelIps = s != nullptr ? s->ips : 15.0;
    tapeReset = true;
    if (s != nullptr)
    {
        setTempo (s->tempoBpm, s->beatsPerBar, s->barOne);
        clickOn = s->clickOn;
        clickToMaster = s->clickToMaster;
        clickGain = (double) juce::Decibels::decibelsToGain (s->clickLevelDb);
        tcRate = s->tcRate;
        tcStart = s->tcStartSeconds;
        ltcGain = (double) juce::Decibels::decibelsToGain (s->ltcLevelDb);
    }
    else
    {
        setTempo (0.0, 4, 0.0);
        clickOn = false;
    }
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

    vsMaxHalfSpan = SincKernel::halfSpan (1.0 / maxSpeedRatio);
    vsWeights.calloc ((size_t) vsChunk * (size_t) (2 * vsMaxHalfSpan));
    vsSpan.calloc ((size_t) std::ceil (vsChunk * maxSpeedRatio) + (size_t) (2 * vsMaxHalfSpan) + 16);
    vsOffset.calloc ((size_t) vsChunk);
    tapePosBuf.calloc ((size_t) vsChunk);
    tapeRecBuf.calloc ((size_t) juce::jmax (vsChunk, maxBlock));
    spoolBuf.calloc ((size_t) juce::jmax (vsChunk, maxBlock));
    tapeReset = true;
    clickBuf.setSize (2, maxBlock);
    clickBuf.clear();
    ltcEncoder.prepare (sampleRate);
    ltcDecoder.prepare (sampleRate);
    ltcDecoderCount = 0;
    static_assert (vsChunk <= NetReceiver::maxBlock, "network blocks");
    if (auto* net = netRx.load())
        net->prepare (sampleRate);

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
            const int n = juce::jmin (maxBlock, vsChunk, numSamples - done);

            for (int i = 0; i < numInputs; ++i)  in[(size_t) i]  = inputs[i]  != nullptr ? inputs[i]  + done : nullptr;
            for (int o = 0; o < numOutputs; ++o) out[(size_t) o] = outputs[o] != nullptr ? outputs[o] + done : nullptr;

            processBlock (in.data(), numInputs, out.data(), numOutputs, n);
            done += n;
        }
    }

    callbackCount.fetch_add (1);
}

bool TapeEngine::readTapeSpan (const std::vector<Segment>& segs, juce::int64 start, int len, float* dest) noexcept
{
    bool any = false;
    const auto end = start + len;

    for (const auto& seg : segs)
    {
        if (seg.end <= start)    continue;
        if (seg.start >= end)    break;

        if (! any)
        {
            juce::FloatVectorOperations::clear (dest, len);
            any = true;
        }

        const auto s0 = juce::jmax (seg.start, start);
        const auto s1 = juce::jmin (seg.end, end);
        float* d = dest + (s0 - start);
        if (! seg.reader->read (&d, 1, seg.fileStart + (s0 - seg.start), (int) (s1 - s0)))
            playbackUnderruns.fetch_add (1, std::memory_order_relaxed);
    }

    return any;
}

void TapeEngine::processBlock (const float* const* inputs, int numInputs,
                               float* const* outputs, int numOutputs, int n) noexcept
{
    // Chase: a roll scheduled for an exact device sample (the timecode will be here then)
    if (const auto ps = pendingStart.load (std::memory_order_relaxed); ps >= 0 && ! playing.load() && deviceClock + n > ps)
    {
        pendingStart = -1;
        playing = true;
    }

    const bool isRolling = playing.load();
    const bool isRec     = isRolling && recording.load();
    auto* g  = graph.load();
    auto* rp = isRec ? pass.load() : nullptr;
    auto pos = position.load();

    //--------------------------------------------------------------------------
    // Tape speed. The motor glides to a new speed (~80 ms) while playing;
    // while recording it is held exactly where the pass began.
    // Chasing timecode: the speed follows the timecode (held steady while recording).
    const bool chasing = chaseSource.load (std::memory_order_relaxed) != 0 && chaseRolling.load (std::memory_order_relaxed);
    if (chasing && isRolling && obsDev.load (std::memory_order_relaxed) >= 0)
    {
        const double sr = juce::jmax (1000.0, deviceSampleRate.load (std::memory_order_relaxed));
        const double err = chaseTarget (deviceClock) - ((double) pos + posFrac);
        chaseErrMs.store (err / sr * 1000.0, std::memory_order_relaxed);
        if (pass.load() == nullptr)
        {
            // gentle: closes the gap over ~4 s, never more than 1 %
            if (std::abs (err) > 0.0005 * sr)      chaseRate = 1.0 + juce::jlimit (-0.01, 0.01, err / (4.0 * sr));
            else if (std::abs (err) < 0.0001 * sr) chaseRate = 1.0;
        }
        chaseLocked.store (std::abs (err) < 0.001 * sr, std::memory_order_relaxed);
    }
    else
    {
        chaseRate = 1.0;
    }

    const double target = chasing ? chaseRate : targetRate.load (std::memory_order_relaxed);
    double r0 = currentRate.load (std::memory_order_relaxed), r1 = target;

    if (! isRolling)
    {
        posFrac = 0.0;      // parked on a whole sample
        r0 = r1 = target;
    }
    else if (rp != nullptr)
    {
        r0 = r1 = rp->rate;
    }
    else if (r0 != target)
    {
        const double sr = juce::jmax (1000.0, deviceSampleRate.load (std::memory_order_relaxed));
        r1 = target + (r0 - target) * std::exp (-(double) n / (0.08 * sr));
        if (std::abs (r1 - target) < 1.0e-9 * target)
            r1 = target;
    }
    currentRate.store (r1, std::memory_order_relaxed);

    // Tape: the stock's settings for the speed the tape is really running at,
    // and transport wow & flutter while playing back (held steady while recording).
    const int stockNow = tapeStock.load (std::memory_order_relaxed);
    const bool tapeOn = stockNow != 0;
    if (tapeReset.exchange (false))
    {
        for (int t = 0; t < kMaxTracks; ++t)
        {
            (*tapeRec)[(size_t) t].reset (0x1234567u + (uint32_t) t * 7919u);
            (*tapePlay)[(size_t) t].reset (0x7654321u + (uint32_t) t * 104729u);
        }
        tapeSharedStock = -1;
    }
    if (tapeOn)
    {
        const double sr = juce::jmax (1000.0, deviceSampleRate.load (std::memory_order_relaxed));
        const double ips = reelIps.load (std::memory_order_relaxed) * (isRolling ? r1 : targetRate.load (std::memory_order_relaxed));
        const int mach = machineA.load (std::memory_order_relaxed);
        const int key = stockNow * 64 + mach;
        if (key != tapeSharedStock || std::abs (ips - tapeSharedIps) > 0.003 * ips || tapeShared.sr != sr)
        {
            tapeShared.set ((tape::Stock) stockNow, ips, sr, tape::multitrackMachine (mach));
            tapeSharedStock = key;
            tapeSharedIps = ips;
        }
        if (isRolling && rp == nullptr)
        {
            const double w1 = 1.0 + (wowFlutter.next (ips, sr, n) - 1.0) * tape::multitrackMachine (mach).wowFlutter;
            r0 *= wfPrev;
            r1 *= w1;
            wfPrev = w1;
        }
        else
            wfPrev = 1.0;
    }
    else
        wfPrev = 1.0;

    // The 2-track master: its own tape and machine
    const int mStock = masterStockA.load (std::memory_order_relaxed);
    const bool masterTapeOn = mStock != 0;
    if (masterTapeOn)
    {
        const double sr = juce::jmax (1000.0, deviceSampleRate.load (std::memory_order_relaxed));
        const double ips = reelIps.load (std::memory_order_relaxed) * (isRolling ? r1 : targetRate.load (std::memory_order_relaxed));
        const int key = mStock * 64 + masterMachineA.load (std::memory_order_relaxed);
        if (key != masterSharedKey || std::abs (ips - masterShared.ips) > 0.003 * ips || masterShared.sr != sr)
        {
            if (masterSharedKey < 0)
                for (int side = 0; side < 2; ++side) { masterRec[(size_t) side].reset (0x2468ACEu + (uint32_t) side); masterPlay[(size_t) side].reset (0x13579BDu + (uint32_t) side); }
            masterShared.set ((tape::Stock) mStock, ips, sr, tape::masterMachine (key % 64));
            masterSharedKey = key;
        }
    }
    else
        masterSharedKey = -1;

    // First block of a record pass: decide how it goes to tape.
    if (rp != nullptr && rp->mode.load (std::memory_order_relaxed) == 0)
    {
        for (int t = 0; t < kMaxTracks; ++t)
            if (rp->streams[(size_t) t] != nullptr)
                (*tapeRec)[(size_t) t].reset (0x1234567u + (uint32_t) t * 7919u);   // a fresh pass starts clean
        for (int side = 0; side < 2; ++side)
            masterRec[(size_t) side].reset (0x2468ACEu + (uint32_t) side);
        if (rp->rate == 1.0 && posFrac == 0.0)
        {
            rp->trackStart.store (pos - rp->latency);
            rp->masterStart.store (pos - rp->masterLatency);
            rp->mode.store (1);
        }
        else
        {
            const double p0 = (double) pos + posFrac;
            rp->trackRs.start (p0, rp->latency);
            rp->masterRs.start (p0, rp->masterLatency);
            rp->trackStart.store (rp->trackRs.firstTapeSample());
            rp->masterStart.store (rp->masterRs.firstTapeSample());
            rp->mode.store (2);
        }
    }
    const bool resampledRec = rp != nullptr && rp->mode.load (std::memory_order_relaxed) == 2;

    // Varispeed playback: read positions and interpolation weights for this block,
    // shared by every track.
    const bool vs = isRolling && ! (r0 == 1.0 && r1 == 1.0 && posFrac == 0.0);
    int vsTaps = 0, vsSpanLen = 0;
    juce::int64 vsLo = 0;
    double endFrac = posFrac + (double) n;

    if (vs)
    {
        const auto& kernel = SincKernel::get();
        const double rmax = juce::jmax (r0, r1);
        const double scale = juce::jmin (1.0, 1.0 / rmax);
        const int hs = juce::jmin (vsMaxHalfSpan, SincKernel::halfSpan (scale));
        vsTaps = 2 * hs;

        double f = posFrac;          // tape position relative to `pos`
        for (int k = 0; k < n; ++k)
        {
            const double fl = std::floor (f);
            const auto first = (juce::int64) fl - hs + 1;
            if (k == 0) vsLo = first;
            vsOffset[k] = (int) (first - vsLo);
            kernel.weights (f - fl, scale, hs, vsWeights + (size_t) k * (size_t) vsTaps);
            f += r0 + (r1 - r0) * ((double) k + 0.5) / (double) n;
        }
        endFrac = f;
        vsSpanLen = vsOffset[n - 1] + vsTaps;
        vsLo += pos;
    }

    auto readTape = [&] (const std::vector<Segment>& segs, float* dest) -> bool
    {
        if (! vs)
            return readTapeSpan (segs, pos, n, dest);

        if (! readTapeSpan (segs, vsLo, vsSpanLen, vsSpan))
            return false;
        for (int k = 0; k < n; ++k)
            dest[k] = dotProduct (vsWeights + (size_t) k * (size_t) vsTaps, vsSpan + vsOffset[k], vsTaps);
        return true;
    };

    // Tape position of every sample in this block (for the click and LTC)
    if (isRolling)
    {
        double f = posFrac;
        for (int k = 0; k < n; ++k)
        {
            tapePosBuf[k] = (double) pos + f;
            f += r0 + (r1 - r0) * ((double) k + 0.5) / (double) n;
        }
    }

    // Published for MTC out and for chase timing
    pubClock.store (deviceClock, std::memory_order_relaxed);
    pubMs.store (juce::Time::getMillisecondCounterHiRes(), std::memory_order_relaxed);
    pubTapePos.store ((double) pos + posFrac, std::memory_order_relaxed);
    pubRate.store (isRolling ? r0 : 0.0, std::memory_order_relaxed);
    pubRolling.store (isRolling, std::memory_order_relaxed);

    //--------------------------------------------------------------------------
    float* mixL = mixBus.getWritePointer (0);
    float* mixR = mixBus.getWritePointer (1);
    juce::FloatVectorOperations::clear (mixL, n);
    juce::FloatVectorOperations::clear (mixR, n);

    auto* net = netRx.load (std::memory_order_relaxed);
    if (net != nullptr)
    {
        net->process (n);

        // Hawthorne Sync: where an input sample at the start of this block is recorded on
        // tape (latency compensation included: input sample u lands at P0 + rate * (u - RT)).
        const double rate = isRolling ? (rp != nullptr ? rp->rate : 0.5 * (r0 + r1)) : 0.0;
        const double tapeAtStart = (double) pos + posFrac - (isRolling ? r0 * (double) getRoundTrip() : 0.0);
        const int flags = (isRolling ? hawthorne::net::TapeSync::rolling : 0) | (isRec ? hawthorne::net::TapeSync::recording : 0)
                        | (g != nullptr ? hawthorne::net::TapeSync::reelLoaded : 0);
        net->publishTape (tapeAtStart, rate, flags);
    }

    auto inputFor = [&] (int absCh) -> const float*
    {
        if (NetReceiver::isNetworkInput (absCh))
            return net != nullptr ? net->getChannel (NetReceiver::slotOf (absCh), NetReceiver::channelOf (absCh)) : nullptr;

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
            {
                const float* toTape = src != nullptr ? src : zeros.getReadPointer (0);
                if (tapeOn)
                {
                    // the tape's record side, printed into the file
                    (*tapeRec)[(size_t) t].process (tapeShared, src, tapeRecBuf.get(), n);
                    toTape = tapeRecBuf.get();
                }
                if (resampledRec) rp->trackRs.push (rp->rsChannel[(size_t) t], toTape, n);
                else              stream->push (toTape, n);
            }

        // --- Playback from tape
        // (Tape is read even under a recording track so its reader stays warm
        //  for a seamless punch-out.)
        float* tb = trackBuffers.getWritePointer (t);
        const bool hasTape = isRolling && g != nullptr && readTape (g->tracks[(size_t) t], tb);
        if (hasTape && tapeOn)
            (*tapePlay)[(size_t) t].process (tapeShared, tb, n);      // repro head: bump, top, hiss

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

    if (resampledRec)
    {
        const int count = rp->trackRs.produce (n);
        if (count > 0)
            for (int t = 0; t < nt; ++t)
                if (auto* stream = rp->streams[(size_t) t].get())
                    stream->push (rp->trackRs.output (rp->rsChannel[(size_t) t]), count);
    }

    // --- Master bus
    //  1. internal mix of the tracks (in step with the tape)
    //  2. + patched-in returns. They come back one round trip late, so the
    //     internal mix is delayed by that much to meet them.
    //  3. recorded to the master with the total lag taken off, so it lands
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


        if (recMaster)
        {
            const float* toTape = bus;
            if (masterTapeOn)
            {
                // the 2-track's record side, printed onto the master tape
                masterRec[(size_t) side].process (masterShared, bus, tapeRecBuf.get(), n);
                toTape = tapeRecBuf.get();
            }
            if (resampledRec) rp->masterRs.push (side, toTape, n);
            else              rp->masterStreams[(size_t) side]->push (toTape, n);
        }

        // Master tape playback
        const float* outSig = bus;
        if (monitorTape && isRolling && g != nullptr && ! g->master[(size_t) side].empty())
        {
            float* tb = masterTapeBus.getWritePointer (side);
            if (! readTape (g->master[(size_t) side], tb))
                juce::FloatVectorOperations::clear (tb, n);
            else if (masterTapeOn)
                masterPlay[(size_t) side].process (masterShared, tb, n);   // 2-track repro head
            outSig = tb;
        }

        raisePeak (masterPeak[(size_t) side], peakOf (outSig, n));
        masterEnergy[(size_t) side].add (outSig, n);
        if (rt != nullptr)
            sendToOutputs (outSig, rt->masterOut[(size_t) side]);
    }

    if (recMaster && resampledRec)
    {
        const int count = rp->masterRs.produce (n);
        if (count > 0)
            for (int side = 0; side < 2; ++side)
                rp->masterStreams[(size_t) side]->push (rp->masterRs.output (side), count);
    }

    // --- Click: on every beat of the reel's tempo, following the tape (and its speed)
    const double spb = tempoSpb.load (std::memory_order_relaxed);
    if (rt != nullptr && clickOn.load (std::memory_order_relaxed) && spb > 0.0)
    {
        float* cb = clickBuf.getWritePointer (0);
        juce::FloatVectorOperations::clear (cb, n);
        const double sr = juce::jmax (1000.0, deviceSampleRate.load (std::memory_order_relaxed));
        const double barOne = tempoBarOne.load (std::memory_order_relaxed);
        const int bpb = juce::jmax (1, tempoBpb.load (std::memory_order_relaxed));
        const float gain = (float) clickGain.load (std::memory_order_relaxed);
        // The click is "on the tape": its pitch and length follow the tape speed (varispeed).
        const double speed = isRolling ? juce::jlimit (0.25, 4.0, currentRate.load (std::memory_order_relaxed)) : 1.0;
        const double decay = std::exp (-speed / (0.012 * sr));

        for (int k = 0; k < n; ++k)
        {
            if (isRolling)
            {
                const double beats = (tapePosBuf[k] - barOne) / spb;
                const auto b = (juce::int64) std::floor (beats);
                if (! clickWasRolling)
                {
                    clickLastBeat = (juce::int64) std::ceil (beats - 1.0e-9) - 1;   // a start exactly on a beat clicks
                    clickWasRolling = true;
                }
                if (b > clickLastBeat)
                {
                    const bool accent = ((b % bpb) + bpb) % bpb == 0;
                    clickFreq = accent ? 1760.0 : 1175.0;
                    clickPhase = 0.0;
                    clickEnv = accent ? 1.0 : 0.7;
                }
                clickLastBeat = b;
            }

            if (clickEnv > 1.0e-4)
            {
                cb[k] = gain * (float) (clickEnv * std::sin (clickPhase));
                clickPhase += juce::MathConstants<double>::twoPi * clickFreq * speed / sr;
                clickEnv *= decay;
            }
        }
        if (! isRolling) clickWasRolling = false;
        sendToOutputs (cb, rt->clickOut);
        // Never to tape: the click only ever goes to device outputs, after the
        // tracks and master have been recorded. Not into the master outs if they
        // come back in as master returns (it would be recorded from there).
        if (clickToMaster.load (std::memory_order_relaxed) && ! rt->hasReturns)
            for (int side = 0; side < 2; ++side)              // option: also in the master outs (monitor only, never recorded)
                sendToOutputs (cb, rt->masterOut[(size_t) side]);
    }
    else
    {
        clickWasRolling = false;
        clickEnv = 0.0;
    }

    // --- Lifter defeat: the tape heard while it spools (master outs, monitor only)
    if (rt != nullptr && spoolBuf != nullptr)
    {
        const bool spooling = ! isRolling && lifterDefeat.load (std::memory_order_relaxed) && windActiveA.load (std::memory_order_relaxed);
        juce::FloatVectorOperations::clear (spoolBuf.get(), n);
        spool.render (spoolBuf.get(), n, spooling, windPosA.load (std::memory_order_relaxed), windVelA.load (std::memory_order_relaxed),
                      juce::jmax (1000.0, deviceSampleRate.load (std::memory_order_relaxed)));
        for (int side = 0; side < 2; ++side)
                sendToOutputs (spoolBuf.get(), rt->masterOut[(size_t) side]);
    }

    // --- LTC out: timecode of the tape position, while it rolls
    if (rt != nullptr && ! rt->ltcOut.empty())
    {
        float* lb = clickBuf.getWritePointer (1);
        if (isRolling)
        {
            const auto rate = (timecode::Rate) tcRate.load (std::memory_order_relaxed);
            const float gain = (float) ltcGain.load (std::memory_order_relaxed);
            for (int k = 0; k < n; ++k)
                lb[k] = ltcEncoder.sample (timecodeFramesAt (tapePosBuf[k]), rate, gain);
        }
        else
        {
            juce::FloatVectorOperations::clear (lb, n);
            ltcEncoder.reset();
        }
        sendToOutputs (lb, rt->ltcOut);
    }

    // --- LTC in (chase)
    if (chaseSource.load (std::memory_order_relaxed) == (int) ChaseSource::ltc)
    {
        if (auto* in = inputFor (ltcInput.load (std::memory_order_relaxed)))
        {
            const auto base = ltcDecoderCount;
            const auto rate = (timecode::Rate) tcRate.load (std::memory_order_relaxed);
            ltcDecoder.process (in, n, [&] (const timecode::LtcDecoder::Frame& f)
            {
                const auto r = f.drop ? timecode::Rate::fps2997drop
                                      : (rate == timecode::Rate::fps2997drop ? timecode::Rate::fps2997 : rate);
                const double tcSec = (double) (timecode::toFrames (f.time, r) + 1) / timecode::fps (r);
                observe (tcSec, deviceClock + (f.endSample - base));
            });
            ltcDecoderCount += n;
        }
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

    // Advance the tape - only if nobody located us mid-block.
    if (isRolling)
    {
        const double whole = std::floor (endFrac);
        if (position.compare_exchange_strong (pos, pos + (juce::int64) whole))
            posFrac = endFrac - whole;
        else
            posFrac = 0.0;
    }
}

//==============================================================================
// Tempo, click, timecode

void TapeEngine::setTempo (double bpm, int beatsPerBar, double barOne) noexcept
{
    const double sr = reel != nullptr ? reel->sampleRate : 48000.0;
    tempoSpb = bpm > 0.0 ? sr * 60.0 / bpm : 0.0;
    tempoBpb = juce::jmax (1, beatsPerBar);
    tempoBarOne = barOne;
}

void TapeEngine::setClick (bool on, double levelDb)
{
    clickOn = on;
    clickGain = (double) juce::Decibels::decibelsToGain (levelDb);
    if (reel != nullptr) { reel->clickOn = on; reel->clickLevelDb = levelDb; saveReel(); }
}

void TapeEngine::setClickToMaster (bool on)
{
    clickToMaster = on;
    if (reel != nullptr) { reel->clickToMaster = on; saveReel(); }
}

void TapeEngine::setClickOutputs (const ChannelList& outs)
{
    if (reel == nullptr) return;
    reel->clickOutputs = outs;
    rebuildRouting();
    saveReel();
}

void TapeEngine::setTimecodeFormat (timecode::Rate r, double startSeconds)
{
    tcRate = (int) r;
    tcStart = juce::jmax (0.0, startSeconds);
    if (reel != nullptr) { reel->tcRate = (int) r; reel->tcStartSeconds = tcStart; saveReel(); }
}

void TapeEngine::setLtcOutputs (const ChannelList& outs, double levelDb)
{
    ltcGain = (double) juce::Decibels::decibelsToGain (levelDb);
    if (reel == nullptr) return;
    reel->ltcOutputs = outs;
    reel->ltcLevelDb = levelDb;
    rebuildRouting();
    saveReel();
}

double TapeEngine::timecodeFramesAt (double tapePos) const noexcept
{
    const double sr = reel != nullptr ? reel->sampleRate : juce::jmax (1000.0, deviceSampleRate.load());
    return (tapePos / sr + tcStart.load (std::memory_order_relaxed)) * timecode::fps ((timecode::Rate) tcRate.load (std::memory_order_relaxed));
}

void TapeEngine::setChase (ChaseSource src, int ltcInputChannel)
{
    ltcInput = ltcInputChannel;
    chaseSource = (int) src;
    obsDev = -1;
    obsPrevDev = -1;
    pendingStart = -1;
    chaseRolling = false;
}

void TapeEngine::observe (double tcSeconds, juce::int64 devSample) noexcept
{
    obsPrevTc.store (obsTc.load (std::memory_order_relaxed), std::memory_order_relaxed);
    obsPrevDev.store (obsDev.load (std::memory_order_relaxed), std::memory_order_relaxed);
    obsTc.store (tcSeconds, std::memory_order_relaxed);
    obsDev.store (devSample, std::memory_order_release);
}

void TapeEngine::pushTimecode (double tcSeconds, double atMs) noexcept
{
    const double sr = juce::jmax (1000.0, deviceSampleRate.load());
    const auto dev = pubClock.load() + (juce::int64) std::llround ((atMs - pubMs.load()) * sr / 1000.0);
    observe (tcSeconds, dev);
}

double TapeEngine::chaseTarget (juce::int64 devSample) const noexcept
{
    const double sr = juce::jmax (1000.0, deviceSampleRate.load (std::memory_order_relaxed));
    const double tc = obsTc.load (std::memory_order_relaxed) + (double) (devSample - obsDev.load (std::memory_order_relaxed)) / sr;
    return (tc - tcStart.load (std::memory_order_relaxed)) * sr;
}

TapeEngine::ChaseStatus TapeEngine::getChaseStatus() const noexcept
{
    ChaseStatus st;
    const auto dev = obsDev.load();
    if (chaseSource.load() == 0 || dev < 0)
        return st;
    const double sr = juce::jmax (1000.0, deviceSampleRate.load());
    const auto devNow = pubClock.load() + (juce::int64) ((juce::Time::getMillisecondCounterHiRes() - pubMs.load()) * sr / 1000.0);
    st.receiving = devNow - dev < (juce::int64) (0.4 * sr);
    st.running = st.receiving && obsTc.load() > obsPrevTc.load() && dev - obsPrevDev.load() < (juce::int64) (0.5 * sr);
    st.tcSeconds = obsTc.load() + (st.running ? (double) (devNow - dev) / sr : 0.0);
    st.locked = st.running && chaseRolling.load() && chaseLocked.load();
    st.errorMs = chaseErrMs.load();
    return st;
}

TapeEngine::TapeSnapshot TapeEngine::getTapeSnapshot() const noexcept
{
    TapeSnapshot s;
    s.tapePos = pubTapePos.load();
    s.rate = pubRate.load();
    s.atMs = pubMs.load();
    s.rolling = pubRolling.load();
    s.sampleRate = reel != nullptr ? reel->sampleRate : juce::jmax (1000.0, deviceSampleRate.load());
    return s;
}

void TapeEngine::chaseTick()
{
    // Message thread: the transport side of chasing (locate, roll, stop).
    static double lastObsTc = -1.0;
    if (chaseSource.load() == 0 || reel == nullptr)
    {
        chaseRolling = false;
        pendingStart = -1;
        return;
    }

    const auto st = getChaseStatus();
    const double sr = juce::jmax (1000.0, deviceSampleRate.load());
    const auto devNow = pubClock.load() + (juce::int64) ((juce::Time::getMillisecondCounterHiRes() - pubMs.load()) * sr / 1000.0);

    if (st.running)
    {
        if (! playing && pendingStart.load() < 0 && checkCanRoll().isEmpty())
        {
            // park a little ahead of the timecode and roll the moment it gets there
            const auto startDev = devNow + (juce::int64) (0.3 * sr);
            const double target = chaseTarget (startDev);
            if (target >= 0.0)
            {
                locate ((juce::int64) std::llround (target));
                pendingStart = startDev;
                chaseRolling = true;
            }
        }
        else if (playing && ! recording && std::abs (chaseTarget (devNow) - (double) position.load()) > 0.5 * sr)
        {
            playing = false;     // too far out: re-park and roll again
            chaseRolling = false;
        }
        else if (playing)
        {
            chaseRolling = true;
        }
    }
    else
    {
        if (chaseRolling.load() || pendingStart.load() >= 0)
        {
            pendingStart = -1;
            if (playing) stop();
            chaseRolling = false;
        }

        // A parked master (e.g. a locate in Reaper sends a full-frame message): follow it.
        if (st.receiving && ! playing && obsTc.load() != lastObsTc)
        {
            const double target = chaseTarget (obsDev.load());
            if (target >= 0.0) locate ((juce::int64) std::llround (target));
        }
    }
    lastObsTc = obsTc.load();
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

    if (wind.active)
    {
        // Play while spooling: brake, then roll from wherever the tape stops.
        wind.braking = true;
        wind.rollAfter = true;
        return {};
    }

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

    windCancel();

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

    if (wind.active)
    {
        wind.braking = true;        // STOP while spooling: the brakes come on
        wind.rollAfter = false;
    }
}

void TapeEngine::locate (juce::int64 samplePos)
{
    if (recording)
        return;

    windCancel();
    position.store (juce::jmax ((juce::int64) 0, samplePos));

    if (! playing)
        primeReaders (position.load());
}

//==============================================================================
// Tape wind: a real transport spools to the locate point.
//
// Speeds are in tape inches per second, so a 7.5 ips reel spools through its
// time twice as fast as a 15 ips one, as on a real machine.
namespace windModel
{
    constexpr double maxIps     = 360.0;   // full fast wind
    constexpr double accelTime  = 1.6;     // s from stop to full wind
    constexpr double brakeTime  = 1.1;     // s from full wind to stop
    constexpr double brakeLate  = 0.92;    // brakes a touch late: overshoots, then creeps back
    constexpr double creepIps   = 30.0;    // slow creep onto the locate point
    constexpr double creepAccel = 0.25;    // s to reach creep speed
}

void TapeEngine::windTo (juce::int64 target)
{
    if (recording)
        return;

    target = juce::jmax ((juce::int64) 0, target);

    if (! windPhysics || reel == nullptr || getChase() != ChaseSource::off)
    {
        locate (target);
        return;
    }

    if (! wind.active)
    {
        wind = {};
        wind.pos = (double) position.load();
        wind.rollAfter = playing.load();
        if (playing)
            playing = false;          // lifters out, spool, then play again
    }
    if (target == (juce::int64) wind.pos && ! wind.active)
    {
        windFinish (target);
        return;
    }

    if (! wind.active)
    {
        windPosA.store (wind.pos);
        windVelA.store (0.0);
    }
    windActiveA = true;
    wind.active = true;
    wind.braking = false;
    wind.creeping = false;
    wind.target = target;
    wind.lastMs = juce::Time::getMillisecondCounterHiRes();
}

void TapeEngine::windCancel()
{
    windActiveA = false;
    wind.active = false;
    wind.rollAfter = false;
}

void TapeEngine::windFinish (juce::int64 at)
{
    const bool roll = wind.rollAfter;
    wind = {};
    windActiveA = false;
    position.store (juce::jmax ((juce::int64) 0, at));
    primeReaders (position.load());
    if (roll)
        play();
}

void TapeEngine::windStep()
{
    if (! wind.active)
        return;
    if (reel == nullptr || recording) { windCancel(); return; }

    using namespace windModel;
    const double now = juce::Time::getMillisecondCounterHiRes();
    const double dt = juce::jlimit (0.0, 0.1, (now - wind.lastMs) / 1000.0);
    wind.lastMs = now;

    const double sr = reel->sampleRate;
    const double perIps = sr / juce::jmax (1.0, reel->ips);     // samples/s of tape time per 1 ips
    const double vmax = maxIps * perIps, accel = vmax / accelTime, brake = vmax / brakeTime;
    const double creepV = creepIps * perIps, creepA = creepV / creepAccel;
    auto sgn = [] (double x) { return x < 0.0 ? -1.0 : 1.0; };
    auto towards = [] (double v, double goal, double step) { return v < goal ? juce::jmin (goal, v + step) : juce::jmax (goal, v - step); };

    double err = (double) wind.target - wind.pos;

    if (wind.braking)
    {
        wind.vel = towards (wind.vel, 0.0, brake * dt);
        wind.pos += wind.vel * dt;
        if (wind.pos <= 0.0) { wind.pos = 0.0; wind.vel = 0.0; }
        position.store ((juce::int64) wind.pos);
        windPosA.store (wind.pos);
        windVelA.store (wind.vel);
        if (wind.vel == 0.0)
            windFinish ((juce::int64) wind.pos);
        return;
    }

    // Short hops (and the end of a long one) go at creep speed.
    if (! wind.creeping && ! wind.moved && std::abs (err) < 0.6 * creepV)
        wind.creeping = true;

    if (! wind.creeping)
    {
        const double dir = sgn (err);
        const double stopping = wind.vel * wind.vel / (2.0 * brake);
        const bool towardTarget = wind.vel * dir >= 0.0;

        if (towardTarget && std::abs (err) > stopping * brakeLate)
            wind.vel = towards (wind.vel, dir * vmax, accel * dt);
        else
            wind.vel = towards (wind.vel, 0.0, brake * dt);

        wind.pos += wind.vel * dt;
        wind.moved = true;
        if (wind.pos <= 0.0) { wind.pos = 0.0; wind.vel = 0.0; }

        if (wind.vel == 0.0)
        {
            err = (double) wind.target - wind.pos;
            if (std::abs (err) < 0.002 * sr) { windFinish (wind.target); return; }
            wind.creeping = true;      // overshot (or fell short): creep onto it
        }
    }
    else
    {
        const double dir = sgn (err);
        const double goal = dir * juce::jmin (creepV, std::sqrt (2.0 * creepA * std::abs (err)));
        wind.vel = towards (wind.vel, goal, creepA * dt);
        const double before = err;
        wind.pos += wind.vel * dt;
        err = (double) wind.target - wind.pos;
        if (std::abs (err) < 0.0005 * sr || sgn (err) != sgn (before) || wind.pos < 0.0)
        {
            windFinish (wind.target);
            return;
        }
    }

    position.store ((juce::int64) wind.pos);
    windPosA.store (wind.pos);
    windVelA.store (wind.vel);
    windActiveA.store (true);
}

void TapeEngine::setMachine (int m)
{
    machineA = juce::jlimit (0, tape::numMultitrackMachines - 1, m);
    if (reel != nullptr) { reel->machine = machineA.load(); saveReel(); }
}

void TapeEngine::setMasterMachine (int m)
{
    masterMachineA = juce::jlimit (0, tape::numMasterMachines - 1, m);
    if (reel != nullptr) { reel->masterMachine = masterMachineA.load(); saveReel(); }
}

void TapeEngine::setLifterDefeat (bool on)
{
    lifterDefeat = on;
    if (on) requestSpool();
}

void TapeEngine::requestSpool()
{
    if (reel == nullptr || recording) return;
    juce::Array<juce::File> files;
    for (int t = 0; t < reel->numTracks; ++t)
        if (reel->getTapeFile (t).existsAsFile())
            files.add (reel->getTapeFile (t));
    spool.request (files);
}

juce::String TapeEngine::setSpeed (double deckIps, bool variOn, double variPercent, bool save)
{
    if (recording)
        return "The speed can't change while recording.";

    if (reel == nullptr)
        return {};

    reel->deckIps = (deckIps == 7.5 || deckIps == 30.0) ? deckIps : 15.0;
    reel->variOn = variOn;
    reel->variPercent = juce::jlimit (-maxVarispeedPercent, maxVarispeedPercent, variPercent);
    targetRate = reel->getSpeedRatio();

    if (save)
        saveReel();
    return {};
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


int TapeEngine::getMasterBusLatency() const noexcept
{
    auto* rt = ownedRouting.get();
    if (rt == nullptr) return 0;
    return rt->hasReturns ? getRoundTrip() : 0;
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
        r->clickOut.assign (reel->clickOutputs.begin(), reel->clickOutputs.end());
        r->ltcOut.assign (reel->ltcOutputs.begin(), reel->ltcOutputs.end());
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
    const auto bytesPerSec = (double) juce::jmax (1, files) * reel->sampleRate * (reel->bitDepth / 8) * targetRate.load();
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

        if (NetReceiver::isNetworkInput (ch))
        {
            auto* net = netRx.load();
            if (net == nullptr || ! net->isLive (NetReceiver::slotOf (ch)))
                return reel->tracks[(size_t) t].name + " is armed on a network input that isn't receiving audio.\n"
                       "Check the sender in the NETWORK window.";
            continue;
        }

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
    p->latency = getRoundTrip() + getTapeLatency();     // the tape's record side adds a fixed delay
    p->masterLatency = getMasterBusLatency()
                     + (juce::isPositiveAndBelow (reel->activeMaster, (int) reel->masters.size())
                          && reel->masters[(size_t) reel->activeMaster].stock != 0 ? tape::HalfBand::latency : 0);
    p->rate = targetRate.load();
    p->rsChannel.fill (-1);
    {
        int ch = 0;
        for (int t = 0; t < numTracks.load(); ++t)
            if (armed[(size_t) t]) p->rsChannel[(size_t) t] = ch++;
        // Both resamplers are ready even if the pass turns out to go straight to tape
        // (decided on the first audio block, where the exact tape position is known).
        p->trackRs.prepare (juce::jmax (1, ch), p->rate, vsChunk);
        p->masterRs.prepare (2, p->rate, vsChunk);
    }

    auto abandon = [&p]
    {
        for (auto& s : p->streams)       if (s != nullptr) s->finish();
        for (auto& s : p->masterStreams) if (s != nullptr) s->finish();
    };

    auto openStream = [&] (const juce::File& file, const std::atomic<juce::int64>* startAt, juce::String& err) -> std::shared_ptr<RecordingStream>
    {
        if (! file.existsAsFile() && ! TapeFile::createEmpty (file, sr, err))
            return nullptr;

        auto tape = std::make_unique<TapeFile>();
        if (! tape->open (file, sr, err))
            return nullptr;

        return std::make_shared<RecordingStream> (std::move (tape), file, fifoSize, flushInterval, startAt, 0);
    };

    for (int t = 0; t < numTracks.load(); ++t)
    {
        if (! armed[(size_t) t])
            continue;

        juce::String err;
        p->streams[(size_t) t] = openStream (reel->getTapeFile (t), &p->trackStart, err);
        if (p->streams[(size_t) t] == nullptr) { abandon(); return err; }
    }

    if (recMaster)
    {
        p->masterName = reel->masters[(size_t) reel->activeMaster].name;
        for (int side = 0; side < 2; ++side)
        {
            juce::String err;
            p->masterStreams[(size_t) side] = openStream (reel->getMasterFile (reel->activeMaster, side), &p->masterStart, err);
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
    if (! tracksRecorded) passLength = masterLength;

    if (const auto first = (tracksRecorded ? p->trackStart : p->masterStart).load();
        p->mode.load() != 0 && passLength > 0)
    {
        TakeEntry e;
        e.take   = reel->nextTake++;
        e.when   = (juce::Time::getCurrentTime() - juce::RelativeTime::seconds ((double) passLength / reel->sampleRate))
                       .formatted ("%d %b %Y  %H:%M");
        e.start  = juce::jmax ((juce::int64) 0, first);
        e.end    = first + passLength;

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

    if (lifterDefeat.load())
        requestSpool();

    masterStockA = reel != nullptr && juce::isPositiveAndBelow (reel->activeMaster, (int) reel->masters.size())
                     ? reel->masters[(size_t) reel->activeMaster].stock : 0;
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
    chaseTick();
    windStep();

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
