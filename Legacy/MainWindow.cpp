#include "MainWindow.h"

//==============================================================================
MainWindow::MainWindow(juce::String name, Session* session)
    : DocumentWindow(name,
                     juce::Desktop::getInstance().getDefaultLookAndFeel()
                                                 .findColour(juce::ResizableWindow::backgroundColourId),
                     DocumentWindow::allButtons)
{
    setUsingNativeTitleBar(true);
    setResizable(true, true);
    
    juce::Logger::writeToLog("=== VTape24 Starting (Native UI) ===");
    juce::Logger::writeToLog("CHECKPOINT 1: Constructor started");
    
    // Create settings first
    try
    {
    juce::Logger::writeToLog("CHECKPOINT 2: About to create settings");
        
        settings = std::make_unique<Settings>();
        
    juce::Logger::writeToLog("CHECKPOINT 3: Settings created, track count: " + juce::String(settings->getTrackCount()));
    
        // CRITICAL: If session provided, sync Settings with Session BEFORE creating anything
        if (session)
        {
            juce::Logger::writeToLog("CHECKPOINT 3a: Syncing Settings from Session");
            juce::Logger::writeToLog("  Session track count: " + juce::String(session->getTrackCount()));
            juce::Logger::writeToLog("  Session TOTAL track count (including stereo return): " + juce::String(session->getTotalTrackCount()));
            
            // Set track count — pure user count (stereo return is always at fixed engine index 2)
            settings->setNumTracks(session->getTrackCount());
            settings->setTrackCount(session->getTrackCount());
            settings->setSampleRate(session->getSampleRate());
            settings->setBufferSize(session->getBufferSize());
            
            // Sync audio device (ONLY if session has one - don't overwrite with empty)
            if (session->getAudioDevice().isNotEmpty())
            {
                settings->setAudioDevice(session->getAudioDevice());
                juce::Logger::writeToLog("  Audio device from session: " + session->getAudioDevice());
            }
            else
            {
                juce::Logger::writeToLog("  No audio device in session - using existing Settings device: " + 
                    settings->getAudioDevice());
            }
            
            // ---- Input routing ----
            // Only replace if the session has routing to apply.  Clearing without
            // a replacement would revert every track to the identity default (track N
            // → input N), breaking 2-channel interfaces where tracks 3+ have no physical
            // input and silently write empty files.
            // Sessions created before routing-save was added will have an empty map;
            // in that case leave the existing Settings intact so the user's previously
            // configured routing is preserved.
            const auto& sessionInputRouting = session->getInputRouting();
            if (!sessionInputRouting.empty())
            {
                settings->clearTrackInputMapping();
                for (const auto& [track, input] : sessionInputRouting)
                    settings->setTrackInputMapping(track, input);
                juce::Logger::writeToLog("  Input routing from session: " + juce::String(sessionInputRouting.size()) + " mappings");
            }
            else
            {
                juce::Logger::writeToLog("  No input routing in session - preserving current Settings routing");
            }

            // ---- Output routing ----
            // Same principle: only clear+replace if the session has routing saved.
            // If we always clear, any session opened before routing-save existed will
            // leave every track unrouted to hardware and produce no audio output.
            // The previous "accumulation bug" (routes from session A surviving into
            // session B) is now solved by saveSessionState() always writing current
            // routing back to the session file on close.
            const auto& sessionOutputRouting = session->getOutputRouting();
            if (!sessionOutputRouting.empty())
            {
                settings->clearTrackOutputMapping();
                for (const auto& [key, enabled] : sessionOutputRouting)
                    if (enabled)
                        settings->setTrackOutputMapping(key.first, key.second, true);
                juce::Logger::writeToLog("  Output routing from session: " + juce::String(sessionOutputRouting.size()) + " mappings");
            }
            else
            {
                juce::Logger::writeToLog("  No output routing in session - preserving current Settings routing");
            }

            juce::Logger::writeToLog("  Settings synced - track count now: " + juce::String(settings->getTrackCount()));
        }
    }
    catch (const std::exception& e)
    {
        juce::Logger::writeToLog("FATAL: Settings exception: " + juce::String(e.what()));
        return; // Can't continue without settings
    }
    catch (...)
    {
        juce::Logger::writeToLog("FATAL: Unknown settings exception");
        return;
    }
    
    juce::Logger::writeToLog("CHECKPOINT 4: About to create audio engine");
    
    // Create audio engine - DON'T INITIALIZE YET
    try
    {
        audioEngine = std::make_unique<AudioEngine>(settings.get());
        
    juce::Logger::writeToLog("CHECKPOINT 5: Audio engine object created");
    
    // Create tracks — if session provided, use it so stereo return gets correct TrackType BEFORE TapeUI builds
    if (audioEngine)
    {
        if (session)
        {
            audioEngine->rebuildTracksFromSession(session);  // Sets currentSession, marks Mixdown correctly
            juce::Logger::writeToLog("CHECKPOINT 5b: Tracks built from session (" + juce::String(audioEngine->getNumTracks()) + " tracks, currentSession set)");
        }
        else
        {
            audioEngine->refreshTracksFromSettings();
            juce::Logger::writeToLog("CHECKPOINT 5b: Tracks created from settings (" + juce::String(audioEngine->getNumTracks()) + " tracks)");
        }
    }
    }
    catch (const std::exception& e)
    {
        juce::Logger::writeToLog("FATAL: AudioEngine constructor exception: " + juce::String(e.what()));
        return;
    }
    catch (...)
    {
        juce::Logger::writeToLog("FATAL: Unknown AudioEngine constructor exception");
        return;
    }
    
    juce::Logger::writeToLog("CHECKPOINT 6: Skipping audio initialization (will do async after window visible)");
    
    juce::Logger::writeToLog("CHECKPOINT 8: About to create OSC controller");
    
    // Create OSC controller if enabled
    if (settings && settings->getOSCEnabled())
    {
        try
        {
            oscController = std::make_unique<OSCController>(audioEngine.get());
            juce::Logger::writeToLog("CHECKPOINT 9: OSC controller created");
            
            int oscPort = settings->getOSCPort();
            int connectedPort = oscPort;  // Track which port actually connected
            bool oscOk = oscController->connect(oscPort);
            
            if (!oscOk)
            {
                // Try alternative ports
                juce::Logger::writeToLog("OSC port " + juce::String(oscPort) + " failed, trying alternatives...");
                
                const int alternatePorts[] = {9000, 10000, 7000, 8001};
                for (int altPort : alternatePorts)
                {
                    oscController->disconnect();  // Disconnect before trying next port!
                    if (oscController->connect(altPort))
                    {
                        juce::Logger::writeToLog("OSC connected on alternative port " + juce::String(altPort));
                        connectedPort = altPort;  // Update connected port
                        oscOk = true;
                        break;
                    }
                }
            }
            
            juce::Logger::writeToLog("CHECKPOINT 10: OSC connect returned: " + juce::String(oscOk ? "true" : "false"));
            
            if (oscOk)
            {
                // Connect OSC controller to audio engine for feedback
                audioEngine->setOSCController(oscController.get());
                
                juce::Logger::writeToLog("✓ OSC Control Ready - Receive: " + juce::String(connectedPort) + 
                                       ", Feedback: " + juce::String(connectedPort + 1));
            }
            else
            {
                juce::Logger::writeToLog("OSC: All ports failed (8000, 9000, 10000, 7000, 8001)");
            }
        }
        catch (const std::exception& e)
        {
            juce::Logger::writeToLog("OSC controller exception (non-fatal): " + juce::String(e.what()));
        }
        catch (...)
        {
            juce::Logger::writeToLog("Unknown OSC controller exception (non-fatal)");
        }
    }
    else
    {
        juce::Logger::writeToLog("CHECKPOINT 9-10: OSC disabled in settings");
    }
    
    juce::Logger::writeToLog("CHECKPOINT 11: About to create TapeUI");
    
    // Create native tape UI
    try
    {
        tapeUI = std::make_unique<TapeUI>(audioEngine.get(), settings.get());
    juce::Logger::writeToLog("CHECKPOINT 12: TapeUI object created");
    }
    catch (const std::exception& e)
    {
        juce::Logger::writeToLog("FATAL: TapeUI constructor exception: " + juce::String(e.what()));
        return;
    }
    catch (...)
    {
        juce::Logger::writeToLog("FATAL: Unknown TapeUI constructor exception");
        return;
    }
    
    juce::Logger::writeToLog("CHECKPOINT 13: About to set content");
    
    try
    {
        setContentOwned(tapeUI.get(), true);
    juce::Logger::writeToLog("CHECKPOINT 14: Content set OK");
    }
    catch (const std::exception& e)
    {
        juce::Logger::writeToLog("FATAL: setContentOwned exception: " + juce::String(e.what()));
        return;
    }
    catch (...)
    {
        juce::Logger::writeToLog("FATAL: Unknown setContentOwned exception");
        return;
    }
    
    juce::Logger::writeToLog("CHECKPOINT 15: About to set window size");
    
    // Set window size and show
    try
    {
        // Center window at reasonable size with native title bar
        centreWithSize(1400, 800);
        juce::Logger::writeToLog("CHECKPOINT 16: Window sized");
        
        setVisible(true);
        juce::Logger::writeToLog("CHECKPOINT 17: Window visible");
    }
    catch (const std::exception& e)
    {
        juce::Logger::writeToLog("Window display exception: " + juce::String(e.what()));
    }
    catch (...)
    {
        juce::Logger::writeToLog("Unknown window display exception");
    }
    
    // Initialize audio in background thread AFTER window is visible (non-blocking)
    juce::Thread::launch([this]() {
        initializeAudioAsync();
    });
    
    juce::Logger::writeToLog("=== VTape24 Initialization Complete ===");
}

MainWindow::~MainWindow()
{
    juce::Logger::writeToLog("MainWindow destructor called");
}

void MainWindow::initializeAudioAsync()
{
    juce::Logger::writeToLog("=== Async Audio Initialization Starting ===");
    
    if (!audioEngine)
    {
        juce::Logger::writeToLog("ERROR: AudioEngine not available for async init");
        return;
    }
    
    // Log what device we're trying to use
    if (settings)
    {
        juce::Logger::writeToLog("AudioEngine: Settings audio device = '" + settings->getAudioDevice() + "'");
        juce::Logger::writeToLog("AudioEngine: Settings sample rate = " + juce::String(settings->getSampleRate()));
        juce::Logger::writeToLog("AudioEngine: Settings buffer size = " + juce::String(settings->getBufferSize()));
    }
    
    // Initialize audio (may take 1-3 seconds on Windows, but UI is already visible)
    try
    {
        bool audioOk = audioEngine->initialize();
        juce::Logger::writeToLog("Audio initialize returned: " + juce::String(audioOk ? "true" : "false"));
        
        if (!audioOk)
        {
            juce::Logger::writeToLog("Audio initialization failed - continuing without audio");
            juce::Logger::writeToLog("USER ACTION REQUIRED: Open Settings and configure audio device");
        }
        else
        {
            juce::Logger::writeToLog("Audio initialization SUCCESS - ready to play/record");
        }
    }
    catch (const std::exception& e)
    {
        juce::Logger::writeToLog("Audio initialization exception (non-fatal): " + juce::String(e.what()));
    }
    catch (...)
    {
        juce::Logger::writeToLog("Unknown audio initialization exception (non-fatal)");
    }
    
    juce::Logger::writeToLog("=== Async Audio Initialization Complete ===");
}


void MainWindow::loadSession(Session* session)
{
    if (!session)
    {
        juce::Logger::writeToLog("ERROR: Null session passed to loadSession!");
        return;
    }
    
    currentSession = session;
    
    juce::Logger::writeToLog("=== Loading session into MainWindow ===");
    juce::Logger::writeToLog("  Session: " + session->getSessionName());
    juce::Logger::writeToLog("  Session track count: " + juce::String(session->getTrackCount()));
    juce::Logger::writeToLog("  Audio folder: " + session->getAudioFolder().getFullPathName());
    
    // Settings were already synced in constructor, AudioEngine already has correct track count
    // Just load audio files into existing tracks
    
    if (!audioEngine)
    {
        juce::Logger::writeToLog("ERROR: Audio engine not initialized!");
        return;
    }
    
    int userTrackCnt = audioEngine->getUserTrackCount();
    int firstUserIdx = audioEngine->getFirstUserTrackIndex();
    const auto& sessionTracks = session->getTracks();
    
    juce::Logger::writeToLog("  Loading audio files into " + juce::String(userTrackCnt) + " user tracks...");
    
    // Load audio files for each user track (session tracks 0..N-1 → engine tracks firstUserIdx..N+firstUserIdx-1)
    for (int i = 0; i < userTrackCnt && i < (int)sessionTracks.size(); ++i)
    {
        const auto& sessionTrack = sessionTracks[i];
        Track* track = audioEngine->getTrack(firstUserIdx + i);
        
        if (!track)
            continue;
        
        if (!sessionTrack.recordingFile.isEmpty())
        {
            juce::File audioFile = session->getAudioFolder().getChildFile(sessionTrack.recordingFile);
            
            if (audioFile.existsAsFile())
            {
                juce::Logger::writeToLog("  Track " + juce::String(i + 1) + ": " + audioFile.getFileName());
                track->loadAudioFile(audioFile);  // Non-blocking
            }
        }
    }
    
    juce::Logger::writeToLog("  Audio files queued for loading (async)");
    
    // Set the session in audio engine so recordings go to the right place
    if (audioEngine)
    {
        // Force full track rebuild so stereo return gets TrackType::Mixdown
        // (cannot use refreshTracksFromSettings — early-return fires if count unchanged)
        audioEngine->rebuildTracksFromSession(session);
        juce::Logger::writeToLog("  Tracks rebuilt from session (stereo return type set correctly)");
        
        // Load markers from session
        audioEngine->loadMarkersFromSession(session);
        juce::Logger::writeToLog("  Markers loaded from session");
        
        // Recreate UI tracks to pick up mixdown flag
        if (tapeUI)
        {
            tapeUI->createTracks();
            juce::Logger::writeToLog("  UI tracks recreated with mixdown info");
        }
    }
    
    // Send initial OSC state so connected controllers sync on boot
    if (oscController)
    {
        juce::Logger::writeToLog("  Sending OSC boot state...");
        
        // Transport: stopped
        oscController->sendAllTransportStates(false, true, false, false, false);
        
        // Every user track: arm + monitor
        int bootUserCount  = audioEngine->getUserTrackCount();
        int bootFirstUser  = audioEngine->getFirstUserTrackIndex();
        for (int i = 0; i < bootUserCount; ++i)
        {
            Track* track = audioEngine->getTrack(bootFirstUser + i);
            if (track)
            {
                oscController->sendTrackArmState(i, track->isArmed());
                oscController->sendTrackMonitorState(i, track->isMonitoring());
            }
        }
        
        juce::Logger::writeToLog("  OSC boot state sent");
    }
    
    juce::Logger::writeToLog("=== Session load complete ===");
}

void MainWindow::saveSessionState()
{
    if (!currentSession || !audioEngine)
        return;
    
    juce::Logger::writeToLog("Saving session state...");
    
    // Session tracks are indexed 0..userTrackCount-1; engine user tracks start at firstUserIdx
    int userTrackCount = audioEngine->getUserTrackCount();
    int firstUserIdx   = audioEngine->getFirstUserTrackIndex();
    auto& sessionTracks = currentSession->getTracks();
    
    // Update session with current track states
    for (int i = 0; i < userTrackCount && i < (int)sessionTracks.size(); ++i)
    {
        Track* track = audioEngine->getTrack(firstUserIdx + i);
        if (!track)
            continue;
        
        auto& sessionTrack = sessionTracks[i];
        
        // Update track name
        sessionTrack.name = track->getName();
        
        // Update recording file if track has audio
        juce::File currentFile = track->getCurrentAudioFile();
        if (currentFile != juce::File())
        {
            // Store relative filename only
            sessionTrack.recordingFile = currentFile.getFileName();
            juce::Logger::writeToLog("  Track " + juce::String(i + 1) + " file: " + sessionTrack.recordingFile);
        }
    }
    
    // ---- Sync routing from Settings → Session ----
    // RoutingTabs writes routing changes directly to Settings (global settings.xml).
    // We must pull the live routing back into the Session object before saving to
    // session.xml, otherwise every session save would write stale/empty routing.
    if (settings)
    {
        // Input routing: use Settings map directly (keys = engine track indices, stable)
        currentSession->setInputRouting(settings->getAudioSettings().trackInputMapping);

        // Output routing: Settings stores map<pair<int,int>,bool> — same type as Session now
        currentSession->setOutputRouting(settings->getAudioSettings().trackOutputMapping);

        // Audio device — save per-session so the right interface is selected on load
        juce::String currentDevice = settings->getAudioDevice();
        if (currentDevice.isNotEmpty())
            currentSession->setAudioDevice(currentDevice);

        juce::Logger::writeToLog("  Routing synced from Settings: " +
                                 juce::String(settings->getAudioSettings().trackInputMapping.size()) + " inputs, " +
                                 juce::String(settings->getAudioSettings().trackOutputMapping.size()) + " outputs");
    }

    // Save markers to session before saving to disk
    audioEngine->saveMarkersToSession(currentSession);
    juce::Logger::writeToLog("  Markers saved to session");
    
    // Save session to disk
    if (currentSession->saveToFile(currentSession->getSessionFilePath()))
    {
        juce::Logger::writeToLog("Session state saved successfully");
    }
    else
    {
        juce::Logger::writeToLog("ERROR: Failed to save session state!");
    }
}

void MainWindow::closeButtonPressed()
{
    juce::Logger::writeToLog("Close button pressed");
    
    // Save session state before closing
    saveSessionState();
    
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}
