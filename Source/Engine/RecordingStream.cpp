#include "RecordingStream.h"

namespace hawthorne
{

RecordingStream::RecordingStream (std::unique_ptr<TapeFile> t,
                                  const juce::File& f,
                                  int fifoSizeSamples,
                                  int flushIntervalSamples,
                                  const std::atomic<juce::int64>* start,
                                  int latencySamples)
    : file (f),
      flushInterval (juce::jmax (1, flushIntervalSamples)),
      startPos (start),
      latency (latencySamples),
      fifo (fifoSizeSamples),
      fifoBuffer (1, fifoSizeSamples),
      tape (std::move (t))
{
    fifoBuffer.clear();
}

RecordingStream::~RecordingStream()
{
    finish();
}

void RecordingStream::setReverse (const juce::File& scratchFile, double sampleRate)
{
    reverseScratch = scratchFile;
    reverseRate = sampleRate;
}

//==============================================================================
void RecordingStream::push (const float* data, int numSamples) noexcept
{
    if (numSamples <= 0)
        return;

    timelineSamples.store (timelineSamples.load (std::memory_order_relaxed) + numSamples,
                           std::memory_order_relaxed);

    // A previous block was dropped - announce the gap before any new audio so
    // the disk thread inserts silence at exactly the right stream position.
    if (pendingGap > 0)
    {
        if (gapFifo.getFreeSpace() < 1 || fifo.getFreeSpace() < numSamples)
        {
            pendingGap += numSamples;
            droppedSamples.fetch_add (numSamples, std::memory_order_relaxed);
            return;
        }

        int s1, z1, s2, z2;
        gapFifo.prepareToWrite (1, s1, z1, s2, z2);
        gaps[(size_t) s1] = { streamPushed, pendingGap };
        gapFifo.finishedWrite (1);
        pendingGap = 0;
    }

    if (fifo.getFreeSpace() < numSamples)
    {
        pendingGap += numSamples;
        droppedSamples.fetch_add (numSamples, std::memory_order_relaxed);
        return;
    }

    int s1, z1, s2, z2;
    fifo.prepareToWrite (numSamples, s1, z1, s2, z2);

    if (z1 > 0) fifoBuffer.copyFrom (0, s1, data, z1);
    if (z2 > 0) fifoBuffer.copyFrom (0, s2, data + z1, z2);

    fifo.finishedWrite (z1 + z2);
    streamPushed += numSamples;

    const auto usage = (float) fifo.getNumReady() / (float) fifo.getTotalSize();
    if (usage > peakFifoUsage.load (std::memory_order_relaxed))
        peakFifoUsage.store (usage, std::memory_order_relaxed);
}

//==============================================================================
void RecordingStream::service()
{
    const juce::ScopedLock sl (ioLock);
    drainLocked();
}

bool RecordingStream::positionLocked()
{
    if (positioned)
        return true;

    const auto sp = startPos != nullptr ? startPos->load() : (juce::int64) 0;
    if (sp == notStarted)
        return false; // audio thread hasn't recorded the first block yet

    if (reverseScratch != juce::File())
    {
        // backwards: the scratch file takes the pass in the order it was played
        skipRemaining = 0;
        if (! tape->seek (0))
            writeError = true;
        positioned = true;
        return true;
    }

    // Latency compensation: the first captured sample belongs `latency` samples
    // before the tape position at which it arrived.
    const auto tapeStart = sp - latency;
    skipRemaining = tapeStart < 0 ? -tapeStart : 0;

    if (! tape->seek (juce::jmax ((juce::int64) 0, tapeStart)))
        writeError = true;

    positioned = true;
    return true;
}

void RecordingStream::writeBlockLocked (int start, int num)
{
    const float* p = fifoBuffer.getReadPointer (0, start);

    if (skipRemaining > 0)
    {
        const auto k = (int) juce::jmin ((juce::int64) num, skipRemaining);
        p += k;
        num -= k;
        skipRemaining -= k;
    }

    if (num > 0 && ! tape->write (p, num))
        writeError = true;
}

void RecordingStream::writeSilenceLocked (juce::int64 numSamples)
{
    if (skipRemaining > 0)
    {
        const auto k = juce::jmin (numSamples, skipRemaining);
        numSamples -= k;
        skipRemaining -= k;
    }

    if (numSamples > 0 && ! tape->writeSilence (numSamples))
        writeError = true;

    samplesSinceFlush += numSamples;
}

void RecordingStream::drainLocked()
{
    if (tape == nullptr || ! positionLocked())
        return;

    for (;;)
    {
        // Read the sample count FIRST: a gap is always published before the
        // samples that follow it, so seeing the samples guarantees we see the gap.
        const int ready = fifo.getNumReady();

        juce::int64 limit = std::numeric_limits<juce::int64>::max();

        if (gapFifo.getNumReady() > 0)
        {
            int s1, z1, s2, z2;
            gapFifo.prepareToRead (1, s1, z1, s2, z2);
            const auto gap = gaps[(size_t) s1];

            if (gap.streamPos <= streamDrained)
            {
                writeSilenceLocked (gap.length);
                gapFifo.finishedRead (1);
                continue;
            }

            limit = gap.streamPos - streamDrained;
        }

        const int todo = (int) juce::jmin ((juce::int64) ready, limit);

        if (todo <= 0)
            break;

        int s1, z1, s2, z2;
        fifo.prepareToRead (todo, s1, z1, s2, z2);
        writeBlockLocked (s1, z1);
        writeBlockLocked (s2, z2);
        fifo.finishedRead (z1 + z2);

        streamDrained     += z1 + z2;
        samplesSinceFlush += z1 + z2;
    }

    // Periodically rewrite the WAV header so a crash loses at most ~1 second
    // (and the loader can repair even that from the file size).
    if (samplesSinceFlush >= flushInterval)
    {
        if (! tape->flushHeader())
            writeError = true;

        samplesSinceFlush = 0;
    }
}

bool RecordingStream::finish()
{
    const juce::ScopedLock sl (ioLock);

    if (tape == nullptr)
        return ! writeError;

    drainLocked();

    // Any block dropped at the very end still has to occupy tape space.
    if (positioned && pendingGap > 0)
    {
        writeSilenceLocked (pendingGap);
        pendingGap = 0;
    }

    if (! tape->close())
        writeError = true;

    tape.reset();

    if (reverseScratch != juce::File())
    {
        if (positioned && ! writeError && ! spliceReversedLocked())
            writeError = true;
        reverseScratch.deleteFile();
        reverseScratch = juce::File();
    }
    return ! writeError;
}

bool RecordingStream::spliceReversedLocked()
{
    // The pass, as played: sample k was heard with the tape at (first - k).
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> in (wav.createReaderFor (new juce::FileInputStream (reverseScratch), true));
    if (in == nullptr)
        return false;
    const juce::int64 n = in->lengthInSamples;
    if (n <= 0 || startPos == nullptr || startPos->load() == notStarted)
        return true;

    const juce::int64 high = startPos->load() - latency;       // tape position of the first sample played
    const juce::int64 low  = high - n + 1;                      // ... and of the last (nearest the head)
    if (high < 0)
        return true;                                            // all of it before the head of the tape

    // What's on the tape under the ends of the pass, for the crossfades in and out.
    const int fade = juce::jmax (1, (int) (reverseRate * 0.01));
    std::vector<float> oldLow ((size_t) fade, 0.0f), oldHigh ((size_t) fade, 0.0f);
    if (file.existsAsFile())
        if (std::unique_ptr<juce::AudioFormatReader> old (wav.createReaderFor (new juce::FileInputStream (file), true)); old != nullptr)
        {
            auto grab = [&] (juce::int64 from, std::vector<float>& dest)
            {
                const auto start = juce::jmax ((juce::int64) 0, from);
                const auto skip = (int) (start - from);
                if (start < old->lengthInSamples && skip < fade)
                {
                    float* dd[] = { dest.data() + skip };
                    old->read (dd, 1, start, (int) juce::jmin ((juce::int64) (fade - skip), old->lengthInSamples - start));
                }
            };
            grab (low, oldLow);
            grab (high - fade + 1, oldHigh);
        }

    TapeFile out;
    juce::String err;
    if (! file.existsAsFile() && ! TapeFile::createEmpty (file, reverseRate, err))
        return false;
    if (! out.open (file, reverseRate, err))
        return false;
    if (! out.seek (juce::jmax ((juce::int64) 0, low)))
        return false;

    // Write it reversed, oldest-on-tape first: tape position low + j holds pass sample n - 1 - j.
    constexpr int chunk = 65536;
    juce::AudioBuffer<float> buf (1, chunk);
    bool ok = true;
    for (juce::int64 j = 0; j < n && ok; j += chunk)
    {
        const int m = (int) juce::jmin ((juce::int64) chunk, n - j);
        in->read (&buf, 0, m, n - j - m, true, false);
        buf.reverse (0, 0, m);
        float* x = buf.getWritePointer (0);
        for (int k = 0; k < m; ++k)
        {
            const auto jj = j + k;
            auto mix = [] (float a, float b, double w) { w = std::sin (0.5 * juce::MathConstants<double>::pi * w); w *= w; return (float) (w * a + (1.0 - w) * b); };
            if (jj < fade)          x[k] = mix (x[k], oldLow[(size_t) jj], (double) jj / fade);        // the punch out (played last)
            if (jj >= n - fade)     x[k] = mix (x[k], oldHigh[(size_t) (jj - (n - fade))], (double) (n - 1 - jj) / fade);  // the punch in
        }
        int from = 0;
        if (low + j < 0) from = (int) juce::jmin ((juce::int64) m, -(low + j));     // past the head of the tape
        if (m - from > 0 && ! out.write (x + from, m - from))
            ok = false;
    }
    if (! out.close())
        ok = false;
    return ok;
}

//==============================================================================
DiskWriterThread::DiskWriterThread() : juce::Thread ("Hawthorne Disk Writer")
{
    startThread (juce::Thread::Priority::high);
}

DiskWriterThread::~DiskWriterThread()
{
    stopThread (5000);
}

void DiskWriterThread::add (std::shared_ptr<RecordingStream> s)
{
    const juce::ScopedLock sl (listLock);
    streams.push_back (std::move (s));
    notify();
}

void DiskWriterThread::remove (RecordingStream* s)
{
    const juce::ScopedLock sl (listLock);
    streams.erase (std::remove_if (streams.begin(), streams.end(),
                                   [s] (auto& p) { return p.get() == s; }),
                   streams.end());
}

void DiskWriterThread::run()
{
    while (! threadShouldExit())
    {
        {
            const juce::ScopedLock sl (listLock);

            for (auto& s : streams)
                s->service();
        }

        wait (4);
    }
}

} // namespace hawthorne
