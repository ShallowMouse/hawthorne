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
        setResizeLimits (1340, 600, 10000, 10000);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
        setFullScreen (true);          // with the native title bar this is "maximised"
        content->grabKeyboardFocus();

        // Windows can hand a program started by an installer or the updater a
        // "start minimised" request; Hawthorne always opens in front.
        juce::Component::SafePointer<MainWindow> safe (this);
        for (int ms : { 300, 1200 })
            juce::Timer::callAfterDelay (ms, [safe]
            {
                if (safe != nullptr && safe->isMinimised())
                {
                    safe->setMinimised (false);
                    safe->setFullScreen (true);
                    safe->toFront (true);
                }
            });
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
