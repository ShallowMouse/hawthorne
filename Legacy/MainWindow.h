#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>

#include "AudioEngine.h"
#include "OSCController.h"
#include "Settings.h"
#include "TapeUI.h"
#include "Session.h"

//==============================================================================
class MainWindow : public juce::DocumentWindow
{
public:
    MainWindow(juce::String name, Session* session = nullptr);
    ~MainWindow() override;

    void closeButtonPressed() override;
    
    // Session management
    void loadSession(Session* session);
    void saveSessionState();

private:
    // Native UI
    std::unique_ptr<TapeUI> tapeUI;
    
    // Settings
    std::unique_ptr<Settings> settings;
    
    // Audio engine
    std::unique_ptr<AudioEngine> audioEngine;
    
    // OSC controller
    std::unique_ptr<OSCController> oscController;
    
    // Current session (not owned)
    Session* currentSession = nullptr;
    
    // Async initialization
    void initializeAudioAsync();
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
};
