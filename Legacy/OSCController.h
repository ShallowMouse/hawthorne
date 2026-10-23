#pragma once

#include <juce_core/juce_core.h>
#include <juce_osc/juce_osc.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "AudioEngine.h"

//==============================================================================
class OSCController : public juce::OSCReceiver,
                      private juce::OSCReceiver::Listener<juce::OSCReceiver::MessageLoopCallback>
{
public:
    OSCController(AudioEngine* engine);
    ~OSCController() override;

    // Connect to OSC port
    bool connect(int port);
    void disconnect();
    
    // Send OSC feedback messages (for hardware controllers)
    void sendAllTransportStates(bool play, bool stop, bool record, bool rewind, bool ffwd, bool pause = false);
    void sendTrackArmState(int trackIndex, bool armed);
    void sendTrackMonitorState(int trackIndex, bool monitoring);
    void sendTimecode(double timeInSeconds);  // Send current time position
    void sendBPM(double bpm);  // Send current BPM
    void sendBeat(int bar, int beat, int tick);  // Send current bar.beat.tick
    void sendClickEnabled(bool enabled);  // Send click track state feedback
    void sendSpeed(double speed, int semitones);  // Send varispeed: speed multiplier and semitone offset
    void sendMessage(const juce::String& address, float value);  // Generic OSC send
    
    // VU meter feedback
    void sendTrackVU(int trackIndex, bool led1, bool led2, bool led3);
    
    // Marker feedback
    void sendMarkerAdded(int markerNumber, double timestamp, const juce::String& label);
    void sendMarkerRemoved(int markerNumber);
    void sendMarkerInfo(int markerNumber, double timestamp, const juce::String& label);

private:
    AudioEngine* audioEngine;
    std::unique_ptr<juce::OSCSender> sender;
    
    // OSC message handler
    void oscMessageReceived(const juce::OSCMessage& message) override;
    
    // Message parsers
    void handleTransportMessage(const juce::OSCMessage& message);
    void handleTrackMessage(const juce::OSCMessage& message);
    void handleMarkerMessage(const juce::OSCMessage& message);
    void handleClickMessage(const juce::OSCMessage& message);
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OSCController)
};
