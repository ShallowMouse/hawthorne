#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "Session.h"
#include "RecordingStream.h"
#include <map>

namespace hawthorne
{

//==============================================================================
/**
    The 24-track machine.

    Realtime rules (audio thread):
      - no locks, no allocation, no file I/O
      - shared structures (playback graph, record pass) are swapped in via atomic
        pointers; the message thread only frees the old one after the audio
        thread has completed at least two further callbacks.

    All public non-const methods are message-thread only unless stated.
*/
class TapeEngine : public juce::AudioIODeviceCallback
{
public:
    TapeEngine();
    ~TapeEngine() override;

    //==========================================================================
    void setSession (Session*);
    Session* getSession() const noexcept { return session; }

    //==========================================================================
    // Transport. Return an empty string on success, or a user-facing error.
    juce::String play();
    juce::String record();          // from stop: roll + record; while playing: punch in
    void punchOut();                // stop recording, keep rolling
    void stop();
    void locate (juce::int64 samplePos);

    bool isPlaying() const noexcept    { return playing.load(); }
    bool isRecording() const noexcept  { return recording.load(); }
    juce::int64 getPosition() const noexcept { return position.load(); }

    //==========================================================================
    // Tracks
    void setArmed (int track, bool);
    bool isArmed (int track) const noexcept { return armed[(size_t) track].load(); }
    void setInput (int track, int absoluteInputChannel);

    // Routing (all lock-free for the audio thread; saved with the session)
    void setTrackToMix (int track, bool);
    void setTrackOutputs (int track, ChannelMask deviceOutputs);
    void setMasterInputs (int side, ChannelMask deviceInputs);    // side 0 = L, 1 = R
    void setMasterOutputs (int side, ChannelMask deviceOutputs);

    /** Peak since last call (0..1+). Safe from the message thread. */
    float popTrackPeak (int track) noexcept { return trackPeak[(size_t) track].exchange (0.0f); }
    float popMasterPeak (int ch) noexcept   { return masterPeak[(size_t) (ch & 1)].exchange (0.0f); }

    /** RMS since last call (for VU meters). Safe from the message thread. */
    float popTrackRms (int track) noexcept  { return trackEnergy[(size_t) track].pop(); }
    float popMasterRms (int ch) noexcept    { return masterEnergy[(size_t) (ch & 1)].pop(); }

    //==========================================================================
    // Status
    double getDeviceSampleRate() const noexcept { return deviceSampleRate.load(); }
    int    getBlockSize() const noexcept        { return blockSize.load(); }
    int    getLatencySamples() const noexcept   { return latencySamples.load(); }
    bool   isDeviceRunning() const noexcept     { return deviceRunning.load(); }
    juce::int64 getDroppedSamples() const noexcept  { return droppedTotal.load(); }
    juce::int64 getPlaybackUnderruns() const noexcept { return playbackUnderruns.load(); }
    juce::StringArray getInputChannelNames() const  { return inputNames; }
    juce::StringArray getOutputChannelNames() const { return outputNames; }
    int getNumArmedTracks() const noexcept;
    /** Seconds of recording possible on the session's disk with the armed tracks. */
    double getRecordTimeRemainingSeconds() const;
    /** Length of a track's tape in samples (0 = blank). Message thread. */
    juce::int64 getTapeLength (int track) const;
    /** Peak disk-buffer fill seen during the current/last take, 0..1. */
    float getDiskBufferPeak() const noexcept { return diskBufferPeak.load(); }

    /** Call ~10-30 times a second from the message thread. Handles crash-safety
        bookkeeping, device loss, disk errors and low disk space. */
    void tick();

    /** Called on the message thread with anything the user must know about. */
    std::function<void (const juce::String& title, const juce::String& message)> onAlert;

    //==========================================================================
    // Tunables (exposed for tests)
    static constexpr double fifoSeconds          = 20.0;
    static constexpr double flushIntervalSeconds = 1.0;
    static constexpr juce::int64 minFreeBytesToRecord = 500LL * 1024 * 1024;
    static constexpr juce::int64 stopRecordingBelowBytes = 200LL * 1024 * 1024;
    int fifoSizeOverride = 0; // tests only

    //==========================================================================
    // Device-independent entry points (used by the callback and by tests)
    void prepare (double sampleRate, int maxBlockSize,
                  const juce::BigInteger& activeInputs, const juce::StringArray& inputChannelNames,
                  const juce::BigInteger& activeOutputs, const juce::StringArray& outputChannelNames,
                  int inputLatency, int outputLatency);
    void process (const float* const* inputs, int numInputs,
                  float* const* outputs, int numOutputs, int numSamples) noexcept;
    void release();

    // juce::AudioIODeviceCallback
    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                           float* const* outputChannelData, int numOutputChannels,
                                           int numSamples, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;
    void audioDeviceError (const juce::String& errorMessage) override;

private:
    //==========================================================================
    struct Segment
    {
        juce::AudioFormatReader* reader = nullptr;
        juce::int64 start = 0, end = 0, fileStart = 0;
    };

    struct PlaybackGraph
    {
        std::vector<std::shared_ptr<juce::BufferingAudioReader>> keepAlive;
        std::array<std::vector<Segment>, kNumTracks> tracks;
    };

    struct RecordPass
    {
        std::array<std::shared_ptr<RecordingStream>, kNumTracks> streams;
        std::atomic<juce::int64> startPos { -1 };
        int latency = 0;
    };

    //==========================================================================
    void processBlock (const float* const* inputs, int numInputs,
                       float* const* outputs, int numOutputs, int numSamples) noexcept;

    juce::String checkCanRoll() const;
    juce::String beginRecordPass();
    void endRecordPass();
    void rebuildPlaybackGraph();
    void primeReaders (juce::int64 pos);
    bool waitForAudioThread();
    template <typename T> void retire (std::unique_ptr<T>);
    std::shared_ptr<juce::BufferingAudioReader> getReader (const juce::File&, const juce::String& key, bool& isNew);
    void primeSegment (const Segment&, juce::int64 pos, bool waitUntilLoaded);
    void alert (const juce::String& title, const juce::String& msg);
    bool saveSession();

    //==========================================================================
    Session* session = nullptr;
    juce::AudioFormatManager formatManager;
    juce::TimeSliceThread prefetchThread { "Hawthorne Playback Prefetch" };
    DiskWriterThread diskThread;
    std::map<juce::String, std::shared_ptr<juce::BufferingAudioReader>> readerCache;
    std::array<int, kNumTracks> tapeGeneration {};

    std::atomic<PlaybackGraph*> graph { nullptr };
    std::atomic<RecordPass*>    pass  { nullptr };
    std::unique_ptr<PlaybackGraph> ownedGraph;
    std::unique_ptr<RecordPass>    ownedPass;
    std::vector<std::shared_ptr<void>> graveyard; // things we could not prove were released

    std::atomic<bool> playing { false }, recording { false };
    std::atomic<juce::int64> position { 0 };
    std::array<std::atomic<bool>, kNumTracks> armed;
    std::array<std::atomic<int>,  kNumTracks> inputChannel;
    std::array<std::atomic<float>, kNumTracks> trackPeak;
    std::array<std::atomic<float>, 2> masterPeak;

    struct Energy
    {
        std::atomic<float> sumSq { 0.0f };
        std::atomic<int>   count { 0 };

        void add (const float* d, int n) noexcept   // audio thread
        {
            float s = 0.0f;
            if (d != nullptr)
                for (int i = 0; i < n; ++i) s += d[i] * d[i];
            sumSq.store (sumSq.load (std::memory_order_relaxed) + s, std::memory_order_relaxed);
            count.store (count.load (std::memory_order_relaxed) + n, std::memory_order_relaxed);
        }

        float pop() noexcept                       // UI thread
        {
            const auto s = sumSq.exchange (0.0f);
            const auto c = count.exchange (0);
            return c > 0 ? std::sqrt (s / (float) c) : 0.0f;
        }
    };
    std::array<Energy, kNumTracks> trackEnergy;
    std::array<Energy, 2> masterEnergy;

    // Device state
    std::atomic<bool>   deviceRunning { false }, deviceLost { false };
    std::atomic<double> deviceSampleRate { 0.0 };
    std::atomic<int>    blockSize { 0 }, latencySamples { 0 };
    std::atomic<juce::uint64> callbackCount { 0 };
    juce::StringArray inputNames, outputNames;
    std::array<int, 256> absToActive {}, absToActiveOut {};
    juce::AudioBuffer<float> mixBus;
    std::array<std::atomic<bool>, kNumTracks>        trackToMix;
    std::array<std::atomic<ChannelMask>, kNumTracks> trackOutputs;
    std::array<std::atomic<ChannelMask>, 2>          masterInputs, masterOutputs;
    int maxBlock = 0;
    juce::AudioBuffer<float> trackBuffers, zeros;

    std::atomic<juce::int64> droppedTotal { 0 }, droppedBeforePass { 0 }, playbackUnderruns { 0 };
    std::atomic<float> diskBufferPeak { 0.0f };
    juce::String pendingDeviceError;
    juce::CriticalSection deviceErrorLock;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TapeEngine)
};

} // namespace hawthorne
