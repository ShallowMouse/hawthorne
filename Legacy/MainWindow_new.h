#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "AudioEngine.h"
#include "OSCController.h"
#include "Settings.h"
#include "TapeMachineUI.h"

//==============================================================================
class MainWindow : public juce::DocumentWindow
{
public:
    MainWindow(juce::String name);
    ~MainWindow() override;

    void closeButtonPressed() override;

private:
    // Native UI Component
    std::unique_ptr<TapeMachineUI> nativeUI;
    
    // Settings
    std::unique_ptr<Settings> settings;
    
    // Audio engine
    std::unique_ptr<AudioEngine> audioEngine;
    
    // OSC controller
    std::unique_ptr<OSCController> oscController;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
};
