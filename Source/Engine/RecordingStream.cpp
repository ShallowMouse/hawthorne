#include "RecordingStream.h"

namespace hawthorne
{

RecordingStream::RecordingStream (std::unique_ptr<juce::AudioFormatWriter> w,
                                  const juce::File& f,
                                  int fifoSizeSamples,
                                  int flushIntervalSamples)
    : file (f),
      flushInterval (juce::jmax (1, flushIntervalSamples)),
      fifo (fifoSizeSamples),
      fifoBuffer (1, fifoSizeSamples),
      writer (std::move (w))
{
    fifoBuffer.clear();
    silence.clear();
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

void RecordingStream::writeBlockLocked (int start, int num)
{
    if (num <= 0)
        return;

    if (writer != nullptr && ! writer->writeFromAudioSampleBuffer (fifoBuffer, start, num))
        writeError = true;
}

void RecordingStream::writeSilenceLocked (juce::int64 numSamples)
{
    while (numSamples > 0)
    {
        const auto n = (int) juce::jmin ((juce::int64) silence.getNumSamples(), numSamples);

        if (writer != nullptr && ! writer->writeFromAudioSampleBuffer (silence, 0, n))
            writeError = true;

        numSamples        -= n;
        samplesSinceFlush += n;
    }
}

void RecordingStream::drainLocked()
{
    if (writer == nullptr)
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
        if (! writer->flush())
            writeError = true;

        samplesSinceFlush = 0;
    }
}

bool RecordingStream::finish()
{
    const juce::ScopedLock sl (ioLock);

    if (writer == nullptr)
        return ! writeError;

    drainLocked();

    // Any block dropped at the very end still has to occupy timeline space.
    if (pendingGap > 0)
    {
        writeSilenceLocked (pendingGap);
        pendingGap = 0;
    }

    if (! writer->flush())
        writeError = true;

    writer.reset(); // finalises header and closes the file
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
