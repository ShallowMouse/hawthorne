#pragma once

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

#include <map>

//==============================================================================
struct AudioSettings
{
    juce::String audioDeviceName;
    juce::String audioDriverType;  // e.g. "ASIO", "Windows Audio", "CoreAudio"
    std::map<int, int> trackInputMapping; // track index -> input channel (one input per track)
    std::map<std::pair<int, int>, bool> trackOutputMapping; // (track, output) -> enabled (multiple outputs per track)
    juce::String recordingPath;
    juce::String projectName = "Untitled";
    juce::String sessionName = "New Session";
    juce::String sessionNotes = "";
    juce::String sessionPath = "";  // Base path for all sessions
    int numTracks = 8;  // Deprecated - use trackCount instead
    int trackCount = 8;  // User-selectable track count (8, 16, 32, 64)
    bool masterChannelEnabled = false;
    int sampleRate = 48000;
    int bufferSize = 512;
    bool oscEnabled = true;
    int oscPort = 8000;
    juce::String defaultSavePath = "";   // Default folder for new sessions
    int ltcInputChannel = -1;            // -1 = disabled; hardware input channel for LTC decode
    bool ltcSlaveMode   = false;         // true = transport chases decoded LTC
    bool ltcOutputEnabled = false;       // true = LTC generator active (output track)
    bool ltcSendWhileStopped = false;    // true = send static timecode when paused/stopped (for analog tape machines)
    double defaultBPM = 120.0;
    bool waveformScrollDown = true;  // true = tape-style (down), false = DAW-style (up)
    double waveformZoom = 1.0;       // Zoom level: 1.0 = default, 2.0 = 2x zoomed in, 0.5 = 2x zoomed out
    double amplitudeZoom = 1.0;      // Amplitude zoom: 1.0 = default, 2.0 = 2x larger waves, 0.5 = half size
    bool gridEnabled = false;        // Show musical time grid on waveforms
    int gridResolution = 1;          // 0=bars, 1=beats, 2=1/8, 3=1/16, 4=1/32
    std::map<int, juce::String> inputChannelNames;   // custom labels for hardware inputs
    std::map<int, juce::String> outputChannelNames;  // custom labels for hardware outputs
};

// Key offset used in trackInputMapping / trackOutputMapping for the stereo-right channel
// of any stereo track (e.g. mixdown).  Right-channel key = trackIndex + kStereoRightOffset.
static constexpr int kStereoRightOffset = 1000;

//==============================================================================
class Settings
{
public:
    Settings();
    ~Settings();
    
    // Audio settings
    AudioSettings getAudioSettings() const;
    void setAudioSettings(const AudioSettings& settings);
    
    // Individual setters
    void setAudioDevice(const juce::String& deviceName);
    void setAudioDriverType(const juce::String& driverType);
    void setTrackInputMapping(int trackIndex, int inputChannel);
    void setTrackOutputMapping(int trackIndex, int outputChannel, bool enabled);  // New: supports multiple outputs
    void clearTrackOutputMapping();  // Wipe all output routes (call before loading from session)
    void clearTrackInputMapping();   // Wipe all input routes (call before loading from session)
    void setRecordingPath(const juce::String& path);
    void setProjectName(const juce::String& name);
    void setNumTracks(int numTracks);
    void setTrackCount(int count);  // New: user-selectable track count
    void setMasterChannelEnabled(bool enabled);
    void setSampleRate(int rate);
    void setBufferSize(int size);
    
    // OSC settings
    void setOSCEnabled(bool enabled);
    void setOSCPort(int port);
    bool getOSCEnabled() const;
    int getOSCPort() const;
    
    // Default save location
    void setDefaultSavePath(const juce::String& path);
    juce::String getDefaultSavePath() const;
    
    // LTC input
    void setLTCInputChannel(int ch);
    int getLTCInputChannel() const;
    void setLTCSlaveMode(bool slave);
    bool getLTCSlaveMode() const;
    void setLTCOutputEnabled(bool enabled);
    bool getLTCOutputEnabled() const;
    void setLTCSendWhileStopped(bool send);
    bool getLTCSendWhileStopped() const;
    
    // Hardware channel names (custom labels, saved in settings)
    void setInputChannelName(int ch, const juce::String& name);
    juce::String getInputChannelName(int ch) const;   // returns "IN N" if not set
    void setOutputChannelName(int ch, const juce::String& name);
    juce::String getOutputChannelName(int ch) const;  // returns "OUT N" if not set
    
    // Default BPM
    void setDefaultBPM(double bpm);
    double getDefaultBPM() const;
    
    // Waveform scroll direction (true = down/tape-style, false = up/DAW-style)
    void setWaveformScrollDown(bool scrollDown);
    bool getWaveformScrollDown() const;
    
    // Waveform zoom level (1.0 = default, 2.0 = 2x in, 0.5 = 2x out)
    void setWaveformZoom(double zoom);
    double getWaveformZoom() const;
    
    // Amplitude zoom level (1.0 = default, 2.0 = 2x larger, 0.5 = half size)
    void setAmplitudeZoom(double zoom);
    double getAmplitudeZoom() const;
    
    // Musical grid settings
    void setGridEnabled(bool enabled);
    bool getGridEnabled() const;
    void setGridResolution(int resolution);  // 0=bars, 1=beats, 2=1/8, 3=1/16, 4=1/32
    int getGridResolution() const;
    
    // Session management
    void setSessionPath(const juce::String& path);
    void setSessionName(const juce::String& name);
    void setSessionNotes(const juce::String& notes);
    juce::String getSessionPath() const;
    juce::String getSessionName() const;
    juce::String getSessionNotes() const;
    juce::File getCurrentSessionDirectory() const;
    juce::File getSessionRecordingsDirectory() const;
    juce::File getSessionFile() const;
    bool saveSession();
    bool loadSession(const juce::File& sessionFile);
    juce::Array<juce::File> getAvailableSessions() const;
    
    // Individual getters
    juce::String getAudioDevice() const;
    juce::String getAudioDriverType() const;
    int getTrackInputMapping(int trackIndex) const;
    bool getTrackOutputMapping(int trackIndex, int outputChannel) const;  // New: check if output enabled
    juce::String getRecordingPath() const;
    juce::String getProjectName() const;
    int getNumTracks() const;
    int getTrackCount() const;  // New: get user-selectable track count
    bool getMasterChannelEnabled() const;
    int getSampleRate() const;
    int getBufferSize() const;
    
    // Get full project path (recordingPath/projectName/)
    juce::File getProjectDirectory() const;
    
    // Save/Load
    void save();
    void load();
    
    // Reset to defaults
    void resetToDefaults();

private:
    AudioSettings currentSettings;
    juce::File settingsFile;
    
    void ensureDefaultMappings();
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Settings)
};
