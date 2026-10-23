#pragma once

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>
#include <vector>
#include <memory>

// Forward declarations
class RingBuffer;
class DiskStreamingThread;
class WaveformPeakGenerator;

//==============================================================================
// Track types for different recording modes
enum class TrackType
{
    Normal,         // Normal mono/stereo recording track
    Mixdown,        // Records stereo from master output (mixdown/return)
    Click,          // Virtual: generates metronome click locked to BPM
    Timecode        // Virtual: generates LTC SMPTE timecode audio
};

//==============================================================================
class Track
{
public:
    Track(int trackNumber);
    ~Track();
    
    // Track configuration
    void setStereo(bool isStereo);
    bool isStereo() const { return stereoMode; }
    
    // Track type
    void setTrackType(TrackType t) { trackType = t; stereoMode = (t == TrackType::Mixdown || t == TrackType::Click || t == TrackType::Timecode); }
    TrackType getTrackType() const { return trackType; }
    bool isMixdown() const { return trackType == TrackType::Mixdown; }
    bool isClickTrack() const { return trackType == TrackType::Click; }
    bool isTimecodeTrack() const { return trackType == TrackType::Timecode; }
    bool isVirtualTrack() const { return trackType == TrackType::Click || trackType == TrackType::Timecode; }
    
    // Legacy compatibility
    void setMixdown(bool isMix) { trackType = isMix ? TrackType::Mixdown : TrackType::Normal; stereoMode = isMix; }

    // Recording
    void startRecording(const juce::File& outputFile, juce::int64 startPosition = 0);
    void stopRecording();
    void writeAudio(const float* data, int numSamples, juce::int64 positionInSamples);
    void writeAudioStereo(const float* leftData, const float* rightData, int numSamples, juce::int64 positionInSamples);  // For stereo tracks
    void saveToFile();  // Save track buffer to disk
    
    // Playback with disk streaming
    bool hasAudio() const;
    void readAudio(float* outputBuffer, int numSamples, double startPosition);  // Mono playback
    void readAudioStereo(float* leftBuffer, float* rightBuffer, int numSamples, double startPosition);  // Stereo playback
    
    // Varispeed playback with resampling (linear interpolation)
    void readAudioResampled(float* outputBuffer, int numSamples, double startPosition, double speed);  // Mono varispeed
    void readAudioStereoResampled(float* leftBuffer, float* rightBuffer, int numSamples, double startPosition, double speed);  // Stereo varispeed
    void loadAudioFile(const juce::File& audioFile);  // Load existing audio file
    void clearTrack();  // Clear track to silence
    void updateStreamPosition(juce::int64 playbackPosition);  // Update disk streamer position
    
    // State
    void setArmed(bool armed);
    bool isArmed() const { return armed; }
    bool isRecording() const { return recording; }
    void setMonitoring(bool shouldMonitor);
    bool isMonitoring() const { return monitoring; }
    
    int getTrackNumber() const { return trackNumber; }
    
    // Configuration
    void setSampleRate(double sampleRate);
    double getSampleRate() const { return sampleRate; }
    void setBitDepth(int depth);
    int getBitDepth() const { return bitDepth; }
    
    // Waveform peaks (for UI display)
    std::vector<float> getWaveformPeaks(int resolution);
    void invalidateWaveformCache();  // Force waveform regeneration
    
    // Get recent recording buffer for live waveform display
    std::vector<float> getRecordingBuffer(int numSamples);
    
    // Get total length in samples
    juce::int64 getTotalLengthInSamples() const;
    
    // Track naming
    void setName(const juce::String& name);
    juce::String getName() const { return trackName; }
    
    // Get current audio file (for session saving)
    juce::File getCurrentAudioFile() const { return playbackFile; }

private:
    // Helper for tape-like punch-in: merge old and new recordings
    void mergePunchInRecording();
    
    int trackNumber;
    juce::String trackName;
    double sampleRate = 48000.0;
    int bitDepth = 24;
    bool stereoMode = false;
    TrackType trackType = TrackType::Normal;
    
    std::atomic<bool> armed{false};
    std::atomic<bool> recording{false};
    std::atomic<bool> monitoring{false};
    
    // DISK STREAMING FOR PLAYBACK (Cubase-style)
    std::unique_ptr<RingBuffer> playbackRingBuffer;
    std::unique_ptr<DiskStreamingThread> diskStreamer;
    juce::File playbackFile;
    juce::int64 audioLengthInSamples = 0;
    
    // For tape-like punch-in: track old file to merge with new recording
    juce::File previousPlaybackFile;
    juce::int64 previousAudioLength = 0;
    juce::int64 recordingStartPosition = 0;
    
    // ASYNC WAVEFORM PEAK GENERATION (Cubase-style - non-blocking)
    std::unique_ptr<WaveformPeakGenerator> peakGenerator;
    
    // RECORDING BUFFER (small, for real-time recording)
    // Recordings are written to disk immediately via background thread
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> recordingWriter;
    juce::TimeSliceThread backgroundThread{"Recording Disk Writer"};  // Background thread for non-blocking writes
    juce::File recordingFile;
    std::unique_ptr<juce::WavAudioFormat> wavFormat;
    
    // Small buffer for live waveform display during recording
    static constexpr int recordingBufferSize = 48000;  // 1 second at 48kHz
    std::vector<float> recordingBuffer;  // Mono or left channel
    std::vector<float> recordingBufferRight;  // Right channel (stereo only)
    std::atomic<int> recordingBufferWritePos{0};
    juce::CriticalSection recordingBufferLock;
    
    // Pre-allocated buffers for clamping (avoid allocation on audio thread)
    std::vector<float> clampBuffer;  // Mono or left channel
    std::vector<float> clampBufferRight;  // Right channel (stereo only)
    
    // Pre-allocated buffers for resampling during playback (avoid allocation on audio thread)
    std::vector<float> resampleSourceBuffer;  // Source samples for resampling
    std::vector<float> resampleSourceBufferRight;  // Right channel for stereo resampling
    
    // Recording statistics (for diagnostics)
    std::atomic<int> recordingWriteFailures{0};  // FIFO overflow count
    std::atomic<int> recordingWriteCalls{0};     // Total write attempts
    
    // Waveform peak caching
    std::vector<float> cachedWaveformPeaks;
    int cachedPeaksResolution = 0;
    bool waveformCacheValid = false;
    juce::CriticalSection waveformCacheLock;
    
    // Thread safety
    juce::CriticalSection recordingWriterLock;
    
    // High-quality playback processing state
    float dcFilterStateLeft = 0.0f;   // For DC removal high-pass filter (mono/left)
    float dcFilterStateRight = 0.0f;  // For stereo right channel
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Track)
};
