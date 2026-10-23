#pragma once

#include <juce_core/juce_core.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "AudioEngine.h"

//==============================================================================
// Transport Control Panel
class TransportPanel : public juce::Component
{
public:
    TransportPanel();
    void paint(juce::Graphics& g) override;
    void resized() override;
    
    std::function<void()> onPlay;
    std::function<void()> onStop;
    std::function<void()> onRecord;
    std::function<void()> onRewind;
    std::function<void()> onFastForward;
    
    void setTimecode(const juce::String& timecode);
    void setPlaying(bool playing);
    void setRecording(bool recording);
    
private:
    juce::TextButton playButton;
    juce::TextButton stopButton;
    juce::TextButton recordButton;
    juce::TextButton rewindButton;
    juce::TextButton ffButton;
    juce::Label timecodeLabel;
    
    bool isPlaying = false;
    bool isRecording = false;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TransportPanel)
};

//==============================================================================
// Single Track Strip
class TrackStrip : public juce::Component
{
public:
    TrackStrip(int trackNumber, bool isMixdownTrack = false);
    void paint(juce::Graphics& g) override;
    void resized() override;
    
    void setTrackName(const juce::String& name);
    void setArmed(bool armed);
    void setRecording(bool recording);
    void setLevel(float level);  // 0.0 to 1.0
    void setWaveform(const std::vector<float>& peaks);
    
    std::function<void(bool)> onArmChanged;
    
private:
    int trackNum;
    juce::String trackName;
    juce::TextButton armButton;
    juce::Label nameLabel;
    bool isMixdown = false;  // Is this the stereo mixdown track?
    
    bool armed = false;
    bool recording = false;
    float currentLevel = 0.0f;
    std::vector<float> waveformPeaks;
    
    void drawVUMeter(juce::Graphics& g, juce::Rectangle<int> area);
    void drawWaveform(juce::Graphics& g, juce::Rectangle<int> area);
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrackStrip)
};

//==============================================================================
// Main Tape Machine UI
class TapeMachineUI : public juce::Component,
                      public juce::Timer
{
public:
    TapeMachineUI(AudioEngine* engine);
    ~TapeMachineUI() override;
    
    void paint(juce::Graphics& g) override;
    void resized() override;
    void timerCallback() override;
    
    std::function<void()> onSettingsClicked;
    
private:
    AudioEngine* audioEngine;
    
    TransportPanel transportPanel;
    juce::OwnedArray<TrackStrip> trackStrips;
    juce::Viewport tracksViewport;
    juce::Component tracksContainer;
    juce::TextButton settingsButton;
    juce::Label statusLabel;
    
    int numTracks = 24;
    
    void setupTracks();
    void updateFromEngine();
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TapeMachineUI)
};
