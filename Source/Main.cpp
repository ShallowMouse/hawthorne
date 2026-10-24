#include <juce_gui_basics/juce_gui_basics.h>
#include "UI/MainComponent.h"

#ifndef HAWTHORNE_VERSION
 #define HAWTHORNE_VERSION "1.0.0"
#endif

//==============================================================================
class MainWindow : public juce::DocumentWindow
{
public:
    explicit MainWindow (juce::ApplicationProperties& props)
        : juce::DocumentWindow ("Hawthorne", hawthorne::Colours::bg, juce::DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
        content = new hawthorne::MainComponent (props);
        setContentOwned (content, true);
        setResizable (true, true);
        setResizeLimits (1000, 470, 10000, 10000);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
        content->grabKeyboardFocus();
    }

    void shutdown() { if (content != nullptr) content->shutdown(); }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }

private:
    hawthorne::MainComponent* content = nullptr;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
};

//==============================================================================
class HawthorneApp : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "Hawthorne"; }
    const juce::String getApplicationVersion() override { return HAWTHORNE_VERSION; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise (const juce::String&) override
    {
        juce::PropertiesFile::Options o;
        o.applicationName     = "Hawthorne";
        o.filenameSuffix      = ".settings";
        o.folderName          = "Hawthorne";
        o.osxLibrarySubFolder = "Application Support";
        properties.setStorageParameters (o);

        mainWindow = std::make_unique<MainWindow> (properties);
    }

    void shutdown() override
    {
        if (mainWindow != nullptr)
            mainWindow->shutdown();   // finalise any take before anything else goes

        mainWindow = nullptr;
        properties.closeFiles();
    }

    void systemRequestedQuit() override { quit(); }
    void anotherInstanceStarted (const juce::String&) override {}

private:
    juce::ApplicationProperties properties;
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (HawthorneApp)
