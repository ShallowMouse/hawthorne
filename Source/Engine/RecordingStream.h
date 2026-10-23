#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include "Session.h"
#include <array>
#include <atomic>
#include <memory>

namespace hawthorne
{

//==============================================================================
/**
    One track being recorded onto tape. Audio is written straight into the
    track's tape file starting at (startPos - latency), overwriting what was
    there - like a real tape machine.

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
    /** startPos is written by the audio thread when the first block is recorded;
        nothing is written to the tape until it is known. */
    RecordingStream (std::unique_ptr<TapeFile> tape,
                     const juce::File& file,
                     int fifoSizeSamples,
                     int flushIntervalSamples,
                     const std::atomic<juce::int64>* startPos,
                     int latencySamples);
    ~RecordingStream();

    //==========================================================================
    // Audio thread
    void push (const float* data, int numSamples) noexcept;

    //==========================================================================
    // Disk thread
    void service();

    //==========================================================================
    // Message thread (after audio thread has let go)
    /** Drains everything, writes any trailing gap, finalises the header and
        closes the tape. Returns false if any write failed. */
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
    bool positionLocked();

    const juce::File file;
    const int flushInterval;
    const std::atomic<juce::int64>* startPos;
    const int latency;

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
    std::unique_ptr<TapeFile> tape;
    bool positioned = false;
    juce::int64 skipRemaining     = 0;   // samples that fall before the start of tape
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
