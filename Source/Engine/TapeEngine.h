#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "Reel.h"
#include "RecordingStream.h"
#include "Varispeed.h"
#include "Timecode.h"
#include "TapeStock.h"
#include "SpoolMonitor.h"
#include "../Network/NetReceiver.h"
#include <map>

namespace hawthorne
{

//==============================================================================
/**
    The multitrack machine (up to 96 tracks).

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
    void setReel (Reel*);
    Reel* getReel() const noexcept { return reel; }

    //==========================================================================
    // Transport. Return an empty string on success, or a user-facing error.
    juce::String play();
    juce::String record();          // from stop: roll + record; while playing: punch in
    void punchOut();                // stop recording, keep rolling
    void stop();
    void locate (juce::int64 samplePos);

    /** Locate like a real machine: spool there in fast wind (accelerate, brake,
        a little overshoot, creep back). Instant if wind physics is off, or while
        chasing. If the tape was playing, it plays again on arrival. */
    void windTo (juce::int64 samplePos, bool rollAfter = false);
    void setWindPhysics (bool on) noexcept { windPhysics = on; windPhysicsA = on; }
    /** Lifter defeat: hear the tape (the "chatter") while it spools. Monitor only. */
    void setLifterDefeat (bool on);
    bool getLifterDefeat() const noexcept  { return lifterDefeat.load(); }
    bool isSpoolOverviewReady() const noexcept { return spool.isReady(); }
    bool getWindPhysics() const noexcept   { return windPhysics; }
    /** -1 rewinding, +1 fast forwarding, 0 not spooling. */
    int getWindDirection() const noexcept  { return wind.active ? (wind.vel < 0.0 ? -1 : wind.vel > 0.0 ? 1 : (wind.target < wind.pos ? -1 : 1)) : 0; }
    juce::int64 getWindTarget() const noexcept { return wind.active ? wind.target : position.load(); }

    //==========================================================================
    // Speed: the deck's IPS switch and varispeed. Tape speed relative to the
    // reel = deckIps / reel ips * (1 + varispeed %). Recording at a different
    // speed lands on tape like a real machine (plays back shifted at normal speed).
    // Not while recording. save = false while a knob is being dragged.
    juce::String setSpeed (double deckIps, bool variOn, double variPercent, bool save = true);
    double getTargetSpeed() const noexcept { return targetRate.load(); }
    double getSpeed() const noexcept       { return currentRate.load(); }   // what the tape is doing now
    static constexpr double maxVarispeedPercent = 50.0;
    static constexpr double maxSpeedRatio = 4.0 * 1.5;                     // 7.5 ips reel on a 30 ips deck, +50 %

    bool isPlaying() const noexcept    { return playing.load(); }
    bool isRecording() const noexcept  { return recording.load(); }
    juce::int64 getPosition() const noexcept { return position.load(); }

    //==========================================================================
    // Tracks
    void setArmed (int track, bool);
    bool isArmed (int track) const noexcept { return armed[(size_t) track].load(); }
    void setInput (int track, int absoluteInputChannel);

    int getNumTracks() const noexcept { return numTracks.load(); }

    // Routing (swapped in atomically for the audio thread; saved with the reel)
    void setTrackToMix (int track, bool);
    void setTrackOutputs (int track, const ChannelList& deviceOutputs);
    void setMasterInputs (int side, const ChannelList& deviceInputs);    // side 0 = L, 1 = R
    void setMasterOutputs (int side, const ChannelList& deviceOutputs);

    //==========================================================================
    // Tempo, click and timecode
    /** The reel's tempo (bpm <= 0 = none). Message thread; read by the audio thread. */
    void setTempo (double bpm, int beatsPerBar, double barOne) noexcept;
    /** Click on/off and level; it plays on the reel's click outputs (routing) while the tape rolls. */
    void setClick (bool on, double levelDb);
    void setClickToMaster (bool on);        // also play the click in the master outputs
    void setClickOutputs (const ChannelList& deviceOutputs);
    /** Timecode format: rate and the timecode at tape zero. */
    void setTimecodeFormat (timecode::Rate, double startSeconds);
    void setLtcOutputs (const ChannelList& deviceOutputs, double levelDb);
    /** Timecode at a tape position (reel samples), in frames since 00:00:00:00 (fractional). */
    double timecodeFramesAt (double tapePos) const noexcept;
    timecode::Rate getTimecodeRate() const noexcept { return (timecode::Rate) tcRate.load(); }
    double getTimecodeStart() const noexcept        { return tcStart.load(); }

    /** Chase: follow incoming timecode (locate, roll, hold sync, stop). */
    enum class ChaseSource { off = 0, ltc, mtc };
    void setChase (ChaseSource, int ltcInputChannel = -1);
    ChaseSource getChase() const noexcept { return (ChaseSource) chaseSource.load(); }
    /** MTC (or anything else) came in: tcSeconds of real time since 00:00:00:00, at a
        Time::getMillisecondCounterHiRes() moment. Any thread. */
    void pushTimecode (double tcSeconds, double atMs) noexcept;
    struct ChaseStatus { bool receiving = false, running = false, locked = false; double tcSeconds = 0.0, errorMs = 0.0; };
    ChaseStatus getChaseStatus() const noexcept;

    /** For MTC out: where the tape is, at a moment on the hi-res clock. Any thread. */
    struct TapeSnapshot { double tapePos = 0.0, rate = 0.0, atMs = 0.0, sampleRate = 48000.0; bool rolling = false; };
    TapeSnapshot getTapeSnapshot() const noexcept;

    /** Hawthorne Link: network inputs (ids NetReceiver::inputBase + ...) become available
        anywhere a device input can be patched. Set before the audio starts. */
    void setNetworkReceiver (NetReceiver* r) noexcept { netRx = r; }

    //==========================================================================
    // Masters: stereo mixes recorded onto the reel, aligned with its tracks
    void setActiveMaster (int index);           // the master on the machine (-1 = none)
    void setMasterArmed (bool);
    bool isMasterArmed() const noexcept       { return masterArmedFlag.load(); }
    void setMonitorMaster (bool);               // hear the master tape instead of the live mix
    bool isMonitoringMaster() const noexcept  { return monitorMasterFlag.load(); }
    /** Rebuild playback after masters were added / renamed / removed. */
    void mastersChanged();

    //==========================================================================
    // Latency. Round trip = out -> (cable or outboard) -> in, in samples.
    /** Measured interface round trip (loopback cable); <= 0 = trust the driver's figure. */
    void setMeasuredRoundTrip (int samples)   { measuredRoundTrip = samples; }
    int  getReportedRoundTrip() const noexcept { return latencySamples.load(); }
    int  getRoundTrip() const noexcept;        // what compensation actually uses
    /** How far behind the tape the master bus is right now (when inputs are patched to it). */
    int  getMasterBusLatency() const noexcept;

    enum class PingState { idle, running, done, failed };
    /** Sends one click out of a device output and times it back in on an input.
        The click is audible on that output - use an unused one. */
    bool startPing (int outputChannel, int inputChannel);
    PingState getPingState() const noexcept   { return pingState.load(); }
    int getPingResult() const noexcept        { return pingResult.load(); }

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
    /** Seconds of recording possible on the reel's disk with the armed tracks. */
    double getRecordTimeRemainingSeconds() const;
    /** Length of a track's tape in samples (0 = blank). Message thread. */
    juce::int64 getTapeLength (int track) const;
    /** Length of the reel (longest track). Message thread. */
    juce::int64 getReelLength() const;
    /** Peak disk-buffer fill seen during the current/last take, 0..1. */
    float getDiskBufferPeak() const noexcept { return diskBufferPeak.load(); }

    /** Call ~10-30 times a second from the message thread. Handles crash-safety
        bookkeeping, device loss, disk errors and low disk space. */
    void tick();

    //==========================================================================
    // Autolocator: punch IN / OUT points, auto punch (with pre / post roll),
    // rehearse (monitor switches, nothing recorded) and cycle (loop).
    // Punches are sample-accurate at normal speed, with a 10 ms crossfade
    // against what was on the tape.
    void setPunchPoints (juce::int64 in, juce::int64 out);   // -1 = not set
    juce::int64 getPunchIn() const noexcept  { return reel != nullptr ? reel->punchIn : -1; }
    juce::int64 getPunchOut() const noexcept { return reel != nullptr ? reel->punchOut : -1; }
    void setAutoPunch (bool on);
    bool getAutoPunch() const noexcept       { return reel != nullptr && reel->autoPunch; }
    void setRehearse (bool on);
    bool getRehearse() const noexcept        { return reel != nullptr && reel->rehearse; }
    void setCycle (bool on);
    bool getCycle() const noexcept           { return reel != nullptr && reel->cycle; }
    void setRolls (double preSeconds, double postSeconds);
    double getPreRoll() const noexcept       { return reel != nullptr ? reel->preRoll : 2.0; }
    double getPostRoll() const noexcept      { return reel != nullptr ? reel->postRoll : 1.0; }
    /** REC with AUTO on: spool to the pre-roll, play, punch in and out at the points. */
    juce::String autoRecord();
    bool isAutoPunching() const noexcept     { return apSession.load(); }
    /** Punch out while rolling, with the crossfade (REC pressed again). */
    void punchOutSmooth();

    /** Called on the message thread with anything the user must know about. */
    std::function<void (const juce::String& title, const juce::String& message)> onAlert;

    /** Called on the message thread after every take; the entry is already in the take log. */
    std::function<void (const TakeEntry&)> onTakeLogged;

    //==========================================================================
    // Tunables (exposed for tests)
    static constexpr double fifoSeconds          = 20.0;     // per track, reduced when many tracks are armed
    static constexpr double fifoBudgetBytes      = 256.0 * 1024.0 * 1024.0;
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
        std::array<std::vector<Segment>, kMaxTracks> tracks;
        std::array<std::vector<Segment>, 2> master;     // the active master, L / R
    };

    struct RoutingTable
    {
        std::array<int, kMaxTracks>              input;     // absolute device input, -1 = none
        std::array<bool, kMaxTracks>             toMix;
        std::array<std::vector<int>, kMaxTracks> outputs;   // absolute device outputs
        std::array<std::vector<int>, 2>          masterIn, masterOut;
        bool hasReturns = false;  // inputs patched to the master: internal mix is delayed to meet them
        std::vector<int> clickOut, ltcOut;
    };

    /** Simple ring-buffer delay (audio thread only after prepare). */
    struct Delay
    {
        juce::AudioBuffer<float> buf;
        int write = 0;
        void prepare (int maxDelay) { buf.setSize (1, juce::jmax (1, maxDelay) + 1); buf.clear(); write = 0; }
        void process (float* data, int n, int delay) noexcept
        {
            const int size = buf.getNumSamples();
            delay = juce::jlimit (0, size - 1, delay);
            auto* b = buf.getWritePointer (0);
            for (int i = 0; i < n; ++i)
            {
                b[write] = data[i];
                int r = write - delay; if (r < 0) r += size;
                data[i] = b[r];
                if (++write == size) write = 0;
            }
        }
    };

    struct RecordPass
    {
        std::array<std::shared_ptr<RecordingStream>, kMaxTracks> streams;
        std::array<std::shared_ptr<RecordingStream>, 2> masterStreams;
        juce::String masterName;
        int masterLatency = 0;
        int latency = 0;
        double rate = 1.0;                                   // constant for the whole pass
        bool crossfade = false;                              // punched in on a rolling tape: fade in over the old
        std::atomic<int> mode { 0 };                         // 0 not started, 1 straight to tape, 2 resampled (varispeed)
        std::atomic<juce::int64> trackStart { RecordingStream::notStarted }, masterStart { RecordingStream::notStarted };   // first tape sample written
        RecordResampler trackRs, masterRs;
        std::array<int, kMaxTracks> rsChannel;               // track -> trackRs channel
    };

    //==========================================================================
    void processBlock (const float* const* inputs, int numInputs,
                       float* const* outputs, int numOutputs, int numSamples) noexcept;

    juce::String checkCanRoll() const;
    juce::String beginRecordPass (bool pendingStart = false);
    void endRecordPass();
    void rebuildPlaybackGraph();
    void rebuildRouting();
    void primeReaders (juce::int64 pos);
    bool waitForAudioThread();
    template <typename T> void retire (std::unique_ptr<T>);
    std::shared_ptr<juce::BufferingAudioReader> getReader (const juce::File&, const juce::String& key, bool& isNew);
    void primeSegment (const Segment&, juce::int64 pos, bool waitUntilLoaded);
    void alert (const juce::String& title, const juce::String& msg);
    bool saveReel();

    //==========================================================================
    Reel* reel = nullptr;
    juce::AudioFormatManager formatManager;
    juce::TimeSliceThread prefetchThread { "Hawthorne Playback Prefetch" };
    DiskWriterThread diskThread;
    std::map<juce::String, std::shared_ptr<juce::BufferingAudioReader>> readerCache;
    std::map<juce::String, int> tapeGeneration;   // file path -> bumped whenever the tape changes

    std::atomic<PlaybackGraph*> graph { nullptr };
    std::atomic<RecordPass*>    pass  { nullptr };
    std::atomic<RoutingTable*>  routing { nullptr };
    std::unique_ptr<RoutingTable> ownedRouting;
    std::unique_ptr<PlaybackGraph> ownedGraph;
    std::unique_ptr<RecordPass>    ownedPass;
    std::vector<std::shared_ptr<void>> graveyard; // things we could not prove were released

    std::atomic<bool> playing { false }, recording { false };

    // Autolocator
    std::atomic<bool> apSession { false }, cycleOn { false }, rehearseOn { false }, passEnded { false }, fadeOutReq { false };
    std::atomic<juce::int64> apInB { -1 }, apOutB { -1 }, loopS { -1 }, loopE { -1 };
    std::atomic<RecordPass*> pendingPass { nullptr };
    std::atomic<PlaybackGraph*> standbyGraph { nullptr };   // cycle: primed at the loop start, swapped in at the jump
    std::unique_ptr<PlaybackGraph> ownedGraphB;
    juce::SpinLock graphSwapLock;
    std::atomic<int> loopJumps { 0 };
    std::atomic<bool> windPhysicsA { false }, loopWindReq { false };   // cycle with wind physics: spool back, don't jump
    int loopJumpsSeen = 0;
    juce::int64 apPostStop = -1;
    void updateLoopRegion();
    std::unique_ptr<PlaybackGraph> makeGraph (const juce::String& keySuffix, juce::int64 primeAt);
    // punch crossfades (audio thread)
    static constexpr int ringLen = 32768;
    juce::HeapBlock<float> rawRing;               // the tape just read, per track (for crossfading punches)
    int ringWrite = 0, fadeLen = 480, fadeInLeft = 0, fadeOutLeft = 0;
    bool fadeOutActive = false;
    juce::int64 ringValid = 0, ringNextPos = -1;
    std::atomic<juce::int64> position { 0 };
    double posFrac = 0.0;                                  // audio thread: sub-sample tape position
    std::atomic<double> targetRate { 1.0 }, currentRate { 1.0 };
    std::atomic<NetReceiver*> netRx { nullptr };

    // Tempo / click (audio thread reads)
    std::atomic<double> tempoSpb { 0.0 }, tempoBarOne { 0.0 }, clickGain { 0.25 };
    std::atomic<int> tempoBpb { 4 };
    std::atomic<bool> clickOn { false }, clickToMaster { false };

    // Tape wind (message thread)
    struct Wind
    {
        bool active = false, braking = false, creeping = false, rollAfter = false, moved = false;
        juce::int64 target = 0;
        double pos = 0.0, vel = 0.0, lastMs = 0.0;     // samples, samples/s
    };
    bool windPhysics = false;
    Wind wind;
    std::atomic<bool> lifterDefeat { false }, windActiveA { false };
    std::atomic<double> windPosA { 0.0 }, windVelA { 0.0 };
    SpoolMonitor spool;
    juce::HeapBlock<float> spoolBuf;
    void requestSpool();
    void windStep();
    void windFinish (juce::int64 at);
    void windCancel();
    juce::int64 clickLastBeat = 0;
    bool clickWasRolling = false;
    double clickPhase = 0.0, clickEnv = 0.0, clickFreq = 1000.0;

    // Timecode out
    std::atomic<int> tcRate { 1 };
    std::atomic<double> tcStart { 0.0 }, ltcGain { 0.3 };
    timecode::LtcEncoder ltcEncoder;

    // Chase
    std::atomic<int> chaseSource { 0 }, ltcInput { -1 };
    timecode::LtcDecoder ltcDecoder;
    juce::int64 ltcDecoderCount = 0;
    std::atomic<double> obsTc { 0.0 }, obsPrevTc { 0.0 };
    std::atomic<juce::int64> obsDev { -1 }, obsPrevDev { -1 };
    std::atomic<juce::int64> pendingStart { -1 };       // device sample at which a chase roll begins
    std::atomic<bool> chaseRolling { false }, chaseLocked { false };
    std::atomic<double> chaseErrMs { 0.0 };
    double chaseRate = 1.0;
    void chaseTick();
    void observe (double tcSeconds, juce::int64 devSample) noexcept;
    double chaseTarget (juce::int64 devSample) const noexcept;    // tape position the timecode says, at a device sample

    // Published each callback (for MTC timing and chase)
    std::atomic<juce::int64> pubClock { 0 };
    std::atomic<double> pubMs { 0.0 }, pubTapePos { 0.0 }, pubRate { 0.0 };
    std::atomic<bool> pubRolling { false };

    // Tape emulation (the reel's stock)
    std::atomic<int> tapeStock { 0 }, machineA { 0 }, masterMachineA { 0 }, masterStockA { 0 };
    tape::Shared masterShared;                          // audio thread
    int masterSharedKey = -1;
    std::array<tape::RecordChannel, 2> masterRec;
    std::array<tape::PlaybackChannel, 2> masterPlay;
    std::atomic<double> reelIps { 15.0 };
    std::atomic<bool> tapeReset { true };
    tape::Shared tapeShared;                            // audio thread
    double tapeSharedIps = -1.0;
    int tapeSharedStock = -1;
    std::unique_ptr<std::array<tape::RecordChannel, kMaxTracks>> tapeRec;
    std::unique_ptr<std::array<tape::PlaybackChannel, kMaxTracks>> tapePlay;
    tape::WowFlutter wowFlutter;
    double wfPrev = 1.0;
    juce::HeapBlock<float> tapeRecBuf;
public:
    /** The record path's added latency for this reel's stock (compensated when recording). */
    int getTapeLatency() const noexcept { return tapeStock.load() != 0 ? tape::HalfBand::latency : 0; }
    /** Machine models (changeable): the multitrack, and the 2-track for the master. */
    void setMachine (int multitrackMachine);
    void setMasterMachine (int twoTrackMachine);
private:

    juce::HeapBlock<double> tapePosBuf;                 // tape position of every sample in a block
    juce::AudioBuffer<float> clickBuf;
    static constexpr int vsChunk = 256;                    // varispeed processes at most this many samples at once
    int vsMaxHalfSpan = 0;
    juce::HeapBlock<float> vsWeights, vsSpan;
    juce::HeapBlock<int> vsOffset;
    bool readTapeSpan (const std::vector<Segment>&, juce::int64 start, int len, float* dest) noexcept;
    std::atomic<int> numTracks { kDefaultTracks };
    std::array<std::atomic<bool>, kMaxTracks> armed;
    std::array<std::atomic<float>, kMaxTracks> trackPeak;
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
    std::array<Energy, kMaxTracks> trackEnergy;
    std::array<Energy, 2> masterEnergy;

    // Device state
    std::atomic<bool>   deviceRunning { false }, deviceLost { false };
    std::atomic<double> deviceSampleRate { 0.0 };
    std::atomic<int>    blockSize { 0 }, latencySamples { 0 };
    std::atomic<juce::uint64> callbackCount { 0 };
    juce::StringArray inputNames, outputNames;
    std::array<int, 256> absToActive {}, absToActiveOut {};
    juce::AudioBuffer<float> mixBus, returnBus, masterTapeBus;
    std::array<Delay, 2> returnsDelayLine;
    std::atomic<bool> masterArmedFlag { false }, monitorMasterFlag { false };
    std::atomic<int>  measuredRoundTrip { 0 };

    // Ping (latency measurement)
    std::atomic<PingState> pingState { PingState::idle };
    std::atomic<int> pingOut { -1 }, pingIn { -1 }, pingResult { 0 };
    std::atomic<bool> pingRequested { false };
    juce::int64 deviceClock = 0, pingSentAt = -1;
    int maxBlock = 0;
    juce::AudioBuffer<float> trackBuffers, zeros;

    std::atomic<juce::int64> droppedTotal { 0 }, droppedBeforePass { 0 }, playbackUnderruns { 0 };
    std::atomic<float> diskBufferPeak { 0.0f };
    juce::String pendingDeviceError;
    juce::CriticalSection deviceErrorLock;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TapeEngine)
};

} // namespace hawthorne
