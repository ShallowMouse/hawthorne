#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <array>
#include <atomic>
#include <memory>

namespace hawthorne
{

//==============================================================================
/**
    One mono take being recorded to disk.

    Threading contract
      - push()                       : audio thread only. Lock-free, allocation-free.
      - service()                    : disk thread only.
      - finish()                     : message thread, only after the audio thread
                                       is guaranteed to have stopped calling push().

    Timeline integrity
      If the FIFO is ever full (disk stalled for longer than the buffer length),
      the block is dropped but its length is recorded as a "gap". The disk thread
      writes the same number of zero samples at exactly that point in the stream,
      so the file ALWAYS stays sample-aligned with the timeline. Dropped samples are
      counted and reported to the UI - never silent.
*/
class RecordingStream
{
public:
    RecordingStream (std::unique_ptr<juce::AudioFormatWriter> writer,
                     const juce::File& file,
                     int fifoSizeSamples,
                     int flushIntervalSamples);
    ~RecordingStream();

    //==========================================================================
    // Audio thread
    void push (const float* data, int numSamples) noexcept;

    //==========================================================================
    // Disk thread
    void service();

    //==========================================================================
    // Message thread (after audio thread has let go)
    /** Drains everything, writes trailing gap, finalises the header and closes
        the file. Returns false if any write failed. */
    bool finish();

    //==========================================================================
    // Any thread
    juce::int64 getTimelineLength() const noexcept   { return timelineSamples.load(); }
    juce::int64 getDroppedSamples() const noexcept   { return droppedSamples.load(); }
    bool hasError() const noexcept                   { return writeError.load(); }
    const juce::File& getFile() const noexcept       { return file; }
    /** Highest FIFO fill seen, 0..1 - useful as a disk-health indicator. */
    float getPeakFifoUsage() const noexcept          { return peakFifoUsage.load(); }

private:
    struct Gap { juce::int64 streamPos = 0; juce::int64 length = 0; };

    void drainLocked();
    void writeSilenceLocked (juce::int64 numSamples);
    void writeBlockLocked (int start, int num);

    const juce::File file;
    const int flushInterval;

    // FIFO shared between audio thread (writer) and disk thread (reader)
    juce::AbstractFifo fifo;
    juce::AudioBuffer<float> fifoBuffer;

    static constexpr int maxGaps = 512;
    juce::AbstractFifo gapFifo { maxGaps };
    std::array<Gap, maxGaps> gaps;

    // Audio-thread-only state (read by finish() after hand-off)
    juce::int64 streamPushed = 0;
    juce::int64 pendingGap   = 0;

    std::atomic<juce::int64> timelineSamples { 0 };
    std::atomic<juce::int64> droppedSamples  { 0 };
    std::atomic<float>       peakFifoUsage   { 0.0f };
    std::atomic<bool>        writeError      { false };

    // Disk-side state, guarded by ioLock (disk thread + finish())
    juce::CriticalSection ioLock;
    std::unique_ptr<juce::AudioFormatWriter> writer;
    juce::AudioBuffer<float> silence { 1, 4096 };
    juce::int64 streamDrained     = 0;
    juce::int64 samplesSinceFlush = 0;

    JUCE_DECLARE_NON_COPYABLE (RecordingStream)
};

//==============================================================================
/** Single high-priority thread that services every active RecordingStream. */
class DiskWriterThread : private juce::Thread
{
public:
    DiskWriterThread();
    ~DiskWriterThread() override;

    void add (std::shared_ptr<RecordingStream>);
    void remove (RecordingStream*);

private:
    void run() override;

    juce::CriticalSection listLock;
    std::vector<std::shared_ptr<RecordingStream>> streams;
};

} // namespace hawthorne
