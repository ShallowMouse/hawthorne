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
    if (sp < 0)
        return false; // audio thread hasn't recorded the first block yet

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
    return ! writeError;
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
