#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "UpdateChecker.h"

#ifndef HAWTHORNE_VERSION
#define HAWTHORNE_VERSION "1.0.0"
#endif

class MainWindow : public juce::DocumentWindow
{
public:
    MainWindow() : juce::DocumentWindow("Hawthorne",
        juce::Colour(0xff1a1a1a), juce::DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar(true);
        setContentOwned(new juce::Component(), true);
        setResizable(true, true);
        centreWithSize(600, 400);
        setVisible(true);
    }
    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
};

class HawthornApp : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "Hawthorne"; }
    const juce::String getApplicationVersion() override { return HAWTHORNE_VERSION; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise(const juce::String&) override
    {
        mainWindow = std::make_unique<MainWindow>();
        updater    = std::make_unique<UpdateChecker>();
        updater->checkAsync();
    }

    void shutdown() override { mainWindow = nullptr; }
    void systemRequestedQuit() override { quit(); }
    void anotherInstanceStarted(const juce::String&) override {}

private:
    std::unique_ptr<MainWindow>    mainWindow;
    std::unique_ptr<UpdateChecker> updater;
};

START_JUCE_APPLICATION(HawthornApp)
