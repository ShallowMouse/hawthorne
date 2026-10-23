#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "MainWindow.h"
#include "ProjectManagerDialog.h"
#include "Session.h"
#include "Settings.h"
#include "UpdateChecker.h"

#ifndef HAWTHORNE_VERSION
#define HAWTHORNE_VERSION "1.0.0"
#endif

//==============================================================================
class VTape24Application : public juce::JUCEApplication
{
public:
    VTape24Application() {}

    const juce::String getApplicationName() override { return "Hawthorne"; }
    const juce::String getApplicationVersion() override { return HAWTHORNE_VERSION; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise(const juce::String& commandLine) override
    {
        // Setup logging to file - use Desktop for easy access
        juce::File logLocation = juce::File::getSpecialLocation(juce::File::userDesktopDirectory)
                                      .getChildFile("VTape24_debug.log");
        
        logFile = std::make_unique<juce::FileLogger>(
            logLocation,
            "VTape24 Debug Log",
            1024 * 1024 * 10  // 10MB max
        );
        juce::Logger::setCurrentLogger(logFile.get());
        
        juce::Logger::writeToLog("========================================");
        juce::Logger::writeToLog("VTape24 Application Starting");
        juce::Logger::writeToLog("Version: 0.1.0 - Native UI with Session Management");
        juce::Logger::writeToLog("Log file: " + logFile->getLogFile().getFullPathName());
        juce::Logger::writeToLog("========================================");
        
        // Show session manager dialog FIRST
        showSessionManager();
    }
    
    void showSessionManager()
    {
        juce::Logger::writeToLog("Showing Session Manager dialog...");
        
        // Create settings early so dialog can read/write defaults
        if (!appSettings)
        {
            appSettings = std::make_unique<Settings>();
            appSettings->load();
            juce::Logger::writeToLog("AppSettings loaded");
        }
        
        // Create session manager dialog
        sessionDialog = std::make_unique<ProjectManagerDialog>(appSettings.get());
        
        // Set up callback for when dialog closes
        sessionDialog->onDialogClosed = [this]()
        {
            auto result = sessionDialog->getResult();
            
            if (result == ProjectManagerDialog::Result::Cancel)
            {
                juce::Logger::writeToLog("User cancelled - exiting application");
                quit();
                return;
            }
            
            // Get the selected session file
            juce::File sessionFile = sessionDialog->getSelectedSessionFile();
            
            if (result == ProjectManagerDialog::Result::NewSession ||
                result == ProjectManagerDialog::Result::OpenSession)
            {
                juce::Logger::writeToLog("Loading session: " + sessionFile.getFullPathName());
                
                // Load the session
                currentSession = std::make_unique<Session>();
                if (!currentSession->loadFromFile(sessionFile))
                {
                    juce::Logger::writeToLog("ERROR: Failed to load session!");
                    juce::AlertWindow::showMessageBoxAsync(
                        juce::MessageBoxIconType::WarningIcon,
                        "Session Load Error",
                        "Failed to load session file:\n" + sessionFile.getFullPathName());
                    
                    // Show session manager again
                    showSessionManager();
                    return;
                }
                
                juce::Logger::writeToLog("Session loaded successfully:");
                juce::Logger::writeToLog("  Name: " + currentSession->getSessionName());
                juce::Logger::writeToLog("  Sample Rate: " + juce::String(currentSession->getSampleRate()));
                juce::Logger::writeToLog("  Bit Depth: " + juce::String(currentSession->getBitDepth()));
                juce::Logger::writeToLog("  Track Count: " + juce::String(currentSession->getTrackCount()));
            }
            
            // Close the dialog
            if (sessionDialogWindow)
            {
                sessionDialogWindow->exitModalState(0);
                sessionDialogWindow = nullptr;
            }
            sessionDialog = nullptr;
            
            // Now create the main window with the session
            createMainWindow();
        };
        
        // Create a dialog window for the session manager
        sessionDialogWindow = std::make_unique<juce::DocumentWindow>(
            "VTape24 - Session Manager",
            juce::Colour(VintageLookAndFeel::bgTrack),
            juce::DocumentWindow::closeButton);
        
        sessionDialogWindow->setContentNonOwned(sessionDialog.get(), true);
        sessionDialogWindow->centreWithSize(700, 550);
        sessionDialogWindow->setUsingNativeTitleBar(true);
        sessionDialogWindow->setVisible(true);
        sessionDialogWindow->enterModalState(true);
    }
    
    void createMainWindow()
    {
        if (!currentSession)
        {
            juce::Logger::writeToLog("ERROR: No session loaded!");
            return;
        }
        
        juce::Logger::writeToLog("Creating main window with session: " + currentSession->getSessionName());
        
        try
        {
            // Create main window with native UI, passing session to sync Settings first
            juce::Logger::writeToLog("Creating main window with native UI...");
            mainWindow.reset(new MainWindow(getApplicationName(), currentSession.get()));
            
            // Load session ASYNC after window is visible (don't block UI thread)
            juce::Logger::writeToLog("Scheduling async session load...");
            juce::MessageManager::callAsync([this]() {
                if (mainWindow)
                {
                    juce::Logger::writeToLog("Loading session into MainWindow (async)...");
                    mainWindow->loadSession(currentSession.get());
                    juce::Logger::writeToLog("Session loaded successfully");
                }
            });
            
            juce::Logger::writeToLog("Main window created successfully");
        }
        catch (const std::exception& e)
        {
            juce::Logger::writeToLog("FATAL ERROR creating window: " + juce::String(e.what()));
            
            // Show error message box
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::WarningIcon,
                "VTape24 Startup Error",
                "Failed to create main window:\n\n" + juce::String(e.what()) +
                "\n\nCheck log file at:\n" + logFile->getLogFile().getFullPathName());
        }
        catch (...)
        {
            juce::Logger::writeToLog("FATAL ERROR: Unknown exception creating window");
            
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::WarningIcon,
                "VTape24 Startup Error",
                "Unknown error occurred during startup.\n\nCheck log file at:\n" + logFile->getLogFile().getFullPathName());
        }
        
        juce::Logger::writeToLog("Initialise() completed");

        // Check for updates in background (non-blocking)
        updater = std::make_unique<UpdateChecker>();
        updater->checkAsync();
    }

    void shutdown() override
    {
        juce::Logger::writeToLog("Shutting down...");
        
        // Save session before closing
        if (currentSession)
        {
            juce::Logger::writeToLog("Saving session...");
            currentSession->saveToFile(currentSession->getSessionFilePath());
        }
        
        // Clean up
        mainWindow = nullptr;
        sessionDialog = nullptr;
        sessionDialogWindow = nullptr;
        currentSession = nullptr;
        
        juce::Logger::writeToLog("Shutdown complete");
        juce::Logger::setCurrentLogger(nullptr);
    }

    void systemRequestedQuit() override
    {
        quit();
    }

    void anotherInstanceStarted(const juce::String& commandLine) override {}
    
    Session* getCurrentSession() const { return currentSession.get(); }

private:
    std::unique_ptr<MainWindow> mainWindow;
    std::unique_ptr<juce::FileLogger> logFile;
    std::unique_ptr<ProjectManagerDialog> sessionDialog;
    std::unique_ptr<juce::DocumentWindow> sessionDialogWindow;
    std::unique_ptr<Session> currentSession;
    std::unique_ptr<Settings> appSettings;  // Loaded before session dialog
    std::unique_ptr<UpdateChecker> updater;
};

//==============================================================================
START_JUCE_APPLICATION(VTape24Application)
