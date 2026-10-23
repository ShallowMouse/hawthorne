#include "MainWindow.h"

//==============================================================================
MainWindow::MainWindow(juce::String name)
    : DocumentWindow(name,
                     juce::Desktop::getInstance().getDefaultLookAndFeel()
                                                 .findColour(juce::ResizableWindow::backgroundColourId),
                     DocumentWindow::allButtons)
{
    setUsingNativeTitleBar(true);
    setResizable(true, true);
    
    juce::Logger::writeToLog("=== VTape24 Starting (Native UI) ===");
    
    // Create settings first
    try
    {
        juce::Logger::writeToLog("Creating settings...");
        settings = std::make_unique<Settings>();
        juce::Logger::writeToLog("Settings created OK");
    }
    catch (const std::exception& e)
    {
        juce::Logger::writeToLog("Settings error: " + juce::String(e.what()));
    }
    
    // Create audio engine with settings
    try
    {
        juce::Logger::writeToLog("Creating audio engine...");
        audioEngine = std::make_unique<AudioEngine>(settings.get());
        juce::Logger::writeToLog("Audio engine created OK");
        
        // Try to initialize audio
        juce::Logger::writeToLog("Initializing audio...");
        audioEngine->initialize();
        juce::Logger::writeToLog("Audio initialized OK");
    }
    catch (const std::exception& e)
    {
        juce::Logger::writeToLog("Audio engine error: " + juce::String(e.what()));
        // Continue anyway - user can select device in settings
    }
    
    // Create OSC controller
    try
    {
        juce::Logger::writeToLog("Creating OSC controller...");
        oscController = std::make_unique<OSCController>(audioEngine.get());
        oscController->connect(8000);
        juce::Logger::writeToLog("OSC controller created OK");
    }
    catch (const std::exception& e)
    {
        juce::Logger::writeToLog("OSC controller error: " + juce::String(e.what()));
    }
    
    // Create native UI
    juce::Logger::writeToLog("Creating native tape machine UI...");
    
    try
    {
        nativeUI = std::make_unique<TapeMachineUI>(audioEngine.get());
        
        // Settings button callback
        nativeUI->onSettingsClicked = [this]() {
            juce::Logger::writeToLog("Settings button clicked");
            // TODO: Open settings dialog
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::InfoIcon,
                "Settings",
                "Settings dialog coming soon!\n\nAudio device and routing configuration.");
        };
        
        setContentOwned(nativeUI.get(), true);
        juce::Logger::writeToLog("Native UI created and set as content");
    }
    catch (const std::exception& e)
    {
        juce::Logger::writeToLog("ERROR creating native UI: " + juce::String(e.what()));
        
        // Ultimate fallback
        auto* label = new juce::Label("error", 
            "VTape24\n\nFailed to create UI:\n" + juce::String(e.what()));
        label->setFont(juce::Font(16.0f));
        label->setJustificationType(juce::Justification::centred);
        setContentOwned(label, true);
    }
    
    // Set window size and show
    juce::Logger::writeToLog("Setting window size to 1400x800...");
    centreWithSize(1400, 800);
    
    juce::Logger::writeToLog("Making window visible...");
    setVisible(true);
    
    juce::Logger::writeToLog("=== VTape24 Native UI Initialization Complete ===");
}

MainWindow::~MainWindow()
{
    juce::Logger::writeToLog("=== MainWindow Destructor Called ===");
    juce::Logger::writeToLog("Cleaning up native UI...");
    nativeUI = nullptr;
    juce::Logger::writeToLog("Cleaning up OSC controller...");
    oscController = nullptr;
    juce::Logger::writeToLog("Cleaning up audio engine...");
    audioEngine = nullptr;
    juce::Logger::writeToLog("Cleaning up settings...");
    settings = nullptr;
    juce::Logger::writeToLog("=== MainWindow Destroyed ===");
}

void MainWindow::closeButtonPressed()
{
    juce::Logger::writeToLog("Close button pressed by user");
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}
