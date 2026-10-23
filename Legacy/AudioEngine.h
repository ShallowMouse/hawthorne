#pragma once

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include "Settings.h"
#include "MarkerManager.h"
#include "Track.h"
#include <vector>
#include <atomic>

// Forward declarations
class OSCController;
class Session;

//==============================================================================
struct TransportState
{
    bool isPlaying = false;
    bool isRecording = false;
    bool isPaused = false;
    double currentTime = 0.0;
    double currentSample = 0.0;
    double sampleRate = 48000.0;
    int bar = 1;
    int beat = 1;
    int tick = 0;
    double bpm = 120.0;
};

//==============================================================================
class AudioEngine : public juce::AudioIODeviceCallback
{
public:
    AudioEngine(Settings* settings);
    ~AudioEngine();

    // Initialize audio system
    bool initialize();
    bool initializeWithDevice(const juce::String& deviceName);
    
    // Device management
    juce::StringArray getAvailableDevices();
    juce::String getCurrentDevice() const;
    bool setAudioDevice(const juce::String& deviceName);
    int getNumInputChannels() const;
    int getNumOutputChannels() const;
    juce::AudioDeviceManager* getDeviceManager() { return &deviceManager; }
    
    // Transport controls
    void play();
    void stop();
    void pause();
    void record();
    void rewind();  // Start continuous rewind (like holding button on tape machine)
    void stopRewind();  // Stop continuous rewind (release button)
    void fastForward();  // Start continuous fast forward
    void stopFastForward();  // Stop continuous fast forward
    void seek(double timeInSeconds);  // Seek to specific time position
    
    // Reverse playback (tape-style backwards play/record)
    void toggleReverse();  // Toggle reverse playback
    bool isReverseEnabled() const { return isReversing.load(); }
    
    // Playback position
    double getCurrentTime() const { return currentSamplePosition.load() / sampleRate; }
    
    // Section/loop points (like Reaper time selection)
    void setSectionStart(double timeInSeconds);  // Set section start point
    void setSectionEnd(double timeInSeconds);    // Set section end point
    void clearSection();  // Clear section points
    void toggleSectionLoop();  // Toggle looping within section
    double getSectionStart() const { return sectionStart.load(); }
    double getSectionEnd() const { return sectionEnd.load(); }
    bool isSectionLoopEnabled() const { return sectionLoop.load(); }
    bool hasSectionSet() const { return sectionStart.load() >= 0.0 && sectionEnd.load() >= 0.0; }
    
    // Transport controls
    void setTrackArmed(int trackIndex, bool armed);
    void setTrackRecording(int trackIndex, bool recording);
    void setTrackMonitoring(int trackIndex, bool monitoring);
    Track* getTrack(int trackIndex);
    int getNumTracks() const { return static_cast<int>(tracks.size()); }
    int getUserTrackCount() const { return userTrackCount; }  // User tracks only (excludes click/timecode)
    // Fixed track indices — stable regardless of user track count
    static constexpr int kClickTrackIndex    = 0;
    static constexpr int kLTCTrackIndex      = 1;
    static constexpr int kStereoReturnIndex  = 2;
    static constexpr int kFirstUserTrackIndex = 3;
    // Legacy alias: kVirtualTrackCount is the number of always-present non-user tracks
    static constexpr int kVirtualTrackCount  = kFirstUserTrackIndex;
    int getFirstUserTrackIndex() const { return kFirstUserTrackIndex; }
    int getStereoReturnEngineIndex() const { return kStereoReturnIndex; }
    bool hasArmedTracks() const;
    
    // Track name — persisted to session
    void setTrackName(int trackIndex, const juce::String& name);
    juce::String getTrackName(int trackIndex) const;
    
    // Save both the session XML (track names, recordings) and settings (routing)
    bool saveSessionToDisk();
    
    // Configuration
    void setNumTracks(int numTracks);
    void setSampleRate(double sampleRate);
    void refreshTracksFromSettings();
    void rebuildTracksFromSession(Session* session);  // Force full rebuild with session track types
    void loadSessionRecordings();  // Load recordings from current session
    void setOSCController(OSCController* osc) { oscController = osc; }  // Set OSC for feedback
    void sendTimecode();  // Send current timecode via OSC
    
    // Session management
    void setCurrentSession(Session* session) { currentSession = session; }
    Session* getCurrentSession() const { return currentSession; }
    
    // OSC Controller access
    OSCController* getOSCController() { return oscController; }
    
    // Marker management
    MarkerManager* getMarkerManager() { return &markerManager; }
    int addMarker(double timestamp, const juce::String& label = "");
    bool removeMarker(int markerNumber);
    void goToMarker(int markerNumber);
    void goToNextMarker();
    void goToPreviousMarker();
    void loadMarkersFromSession(Session* session);
    void saveMarkersToSession(Session* session);
    
    // Beat/Bar navigation
    void skipToNextBeat();
    void skipToPreviousBeat();
    void skipToNextBar();
    void skipToPreviousBar();
    
    // BPM / Click / Timecode
    void setBPM(double bpm);
    double getBPM() const;  // Implementation in .cpp to avoid forward declaration issues
    void setClickEnabled(bool enabled);  // Toggle metronome click
    bool getClickEnabled() const { return clickEnabled.load(); }
    
    // Varispeed control (like tape machine - affects both pitch and playback speed)
    void setPlaybackSpeed(double speed);  // 1.0 = normal, 2.0 = octave up, 0.5 = octave down
    double getPlaybackSpeed() const { return playbackSpeed.load(); }
    void adjustSpeedBySemitones(int semitones);  // +1 = one semitone up, -12 = octave down
    void adjustSpeedByOctave(int octaves);       // +1 = octave up, -1 = octave down

    // LTC output is always active — no on/off toggle.
    // setTimecodeEnabled is kept only for auto-routing convenience when first configuring.
    void setTimecodeEnabled(bool /*enabled*/)
    {
        // Auto-route LTC to output 0 if not yet routed (first-use convenience)
        if (settings && timecodeTrackIndex >= 0)
        {
            bool hasAnyRoute = false;
            for (int ch = 0; ch < 64; ++ch)
                if (settings->getTrackOutputMapping(timecodeTrackIndex, ch)) { hasAnyRoute = true; break; }
            if (!hasAnyRoute)
            {
                settings->setTrackOutputMapping(timecodeTrackIndex, 0, true);
                juce::Logger::writeToLog("LTC: auto-routed to output 0 (change in Session > Output Routing)");
            }
        }
    }
    bool getTimecodeEnabled() const { return true; }  // LTC always runs
    int getClickTrackIndex() const { return clickTrackIndex; }
    int getTimecodeTrackIndex() const { return timecodeTrackIndex; }
    
    // LTC Input — receive LTC from an audio input channel and optionally slave transport
    void setLTCInputChannel(int ch) { ltcInputChannel.store(ch); }
    int  getLTCInputChannel() const { return ltcInputChannel.load(); }
    double getLastDecodedLTCTime() const { return ltcLastDecodedTime.load(); }
    
    // LTC Input — sync transport to incoming SMPTE timecode on a hardware input
    void setLTCInputEnabled(bool enabled, bool slaveTransport = false);
    bool getLTCInputEnabled() const { return ltcInputEnabled.load(); }
    bool getLTCSlaveEnabled() const { return ltcSlaveEnabled.load(); }
    
    // State
    TransportState getTransportState() const;
    
    // AudioIODeviceCallback implementation
    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                          int numInputChannels,
                                          float* const* outputChannelData,
                                          int numOutputChannels,
                                          int numSamples,
                                          const juce::AudioIODeviceCallbackContext& context) override;
    
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;
    
    // Per-track peak levels (thread-safe, written by audio thread, read by UI thread)
    float getTrackLevel(int trackIndex) const
    {
        if (trackIndex >= 0 && trackIndex < MAX_TRACKS)
            return trackPeakLevels[trackIndex].load();
        return 0.0f;
    }

    float getTrackLevelRight(int trackIndex) const
    {
        if (trackIndex >= 0 && trackIndex < MAX_TRACKS)
            return trackPeakLevelsRight[trackIndex].load();
        return 0.0f;
    }

private:
    static constexpr int MAX_TRACKS = 70;  // 2 virtual (Click,LTC) + 64 user + stereo return + margin
    std::atomic<float> trackPeakLevels[MAX_TRACKS];       // L (or mono) peak — audio thread writes, UI reads
    std::atomic<float> trackPeakLevelsRight[MAX_TRACKS];  // R peak — for stereo/mixdown tracks
    juce::AudioDeviceManager deviceManager;
    juce::AudioFormatManager formatManager;
    Settings* settings;
    OSCController* oscController = nullptr;  // For OSC feedback
    Session* currentSession = nullptr;  // Current session (not owned)
    MarkerManager markerManager;  // Marker system
    
    std::vector<std::unique_ptr<Track>> tracks;
    
    std::atomic<bool> isPlaying{false};
    std::atomic<bool> isRecording{false};
    std::atomic<bool> isPaused{false};
    std::atomic<bool> isRewinding{false};  // Continuous rewind like tape machine
    std::atomic<bool> isFastForwarding{false};  // Continuous fast forward like tape machine
    std::atomic<bool> isReversing{false};  // Reverse playback (like tape backwards)
    std::atomic<double> currentSamplePosition{0.0};
    
    // Section/loop points (like Reaper time selection)
    std::atomic<double> sectionStart{-1.0};  // In seconds, -1 = no section set
    std::atomic<double> sectionEnd{-1.0};    // In seconds, -1 = no section set
    std::atomic<bool> sectionLoop{false};    // Loop when reaching end of section
    
    double sampleRate = 48000.0;
    double lastSentTimecode = -1.0;  // Track last sent timecode to avoid redundant OSC messages
    double lastSentBPM = -1.0;  // Track last sent BPM to only send on change
    int lastSentMarkerNumber = -1;  // Track current marker to continuously send marker info
    
    void createTracks(int numTracks);
    juce::File getSessionDirectory() const;
    
    // Click and Timecode generation helpers
    void generateClickAudio(float* left, float* right, int numSamples, double startSamplePos);
    void generateLTCAudio(float* left, float* right, int numSamples, double startSamplePos);
    uint8_t ltcEncodeBit(bool bit, float* buf, int startSample, int samplesPerBit, float& polarity);
    void buildLTCFrame(int h, int m, int s, int f, uint8_t bits[80]);
    
    // LTC Input decoding
    void processLTCInput(const float* samples, int numSamples, juce::int64 bufferStartSample);
    bool decodeLTCFrame(const uint8_t bits[80], int& hh, int& mm, int& ss, int& ff);
    
    // BPM and special tracks
    double bpm = 120.0;
    std::atomic<bool> clickEnabled{false};  // OFF by default — user must explicitly enable
    int clickTrackIndex = -1;
    int timecodeTrackIndex = -1;
    int userTrackCount = 0;
    
    // Varispeed - like tape machine speed control (affects both pitch and speed)
    std::atomic<double> playbackSpeed{1.0};  // 1.0 = normal, 2.0 = double speed (octave up), 0.5 = half speed (octave down)
    
    // Pre-allocated silence buffer for recording (avoid allocation on audio thread)
    std::vector<float> silenceBuffer;  // Reused when no input mapped
    
    // Pre-allocated playback buffers (avoid allocation on audio thread)
    std::vector<float> playbackBufferL;  // Left/mono playback buffer
    std::vector<float> playbackBufferR;  // Right playback buffer
    std::vector<float> clickBufferL;     // Click generation buffer
    std::vector<float> clickBufferR;     // Click generation buffer
    std::vector<float> ltcBufferL;       // LTC generation buffer
    std::vector<float> ltcBufferR;       // LTC generation buffer
    
    // Scrub preview - allow brief audio output after seek while paused
    std::atomic<int> scrubPreviewSamplesRemaining{0};
    
    // LTC output — stateful biphase bit-clock machine
    // Advances unconditionally every sample; timecode value latched each frame boundary.
    float    ltcPolarity   = 1.0f;       // current biphase signal level (+1 / -1)
    double   ltcBitPhase   = 0.0;        // samples elapsed in current bit  [0 .. samplesPerBit)
    int      ltcBitInFrame = 80;         // which bit we're outputting [0..79]; 80 = load frame on first sample
    uint8_t  ltcFrameBits[80] = {};      // 80 bits of the frame currently being clocked out
    
    // LTC input decoder
    std::atomic<bool> ltcInputEnabled{false};
    std::atomic<bool> ltcSlaveEnabled{false};   // When true, transport position follows decoded LTC
    std::atomic<int>  ltcInputChannel{-1};       // Hardware input channel index (-1 = use settings)
    std::atomic<double> ltcLastDecodedTime{0.0}; // Last successfully decoded timecode (seconds)
    struct LTCDecoderState
    {
        float lastSample = 0.0f;
        juce::int64 lastCrossingAbs = 0;
        juce::int64 pendingCrossingAbs = -1;
        uint8_t bits[80] = {};
        int bitsCollected = 0;
        bool synced = false;
        double lastDecodedTimecode = -1.0;
        int consecutiveFailures = 0;
    } ltcDec;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioEngine)
};
