#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include "VintageLookAndFeel.h"
#include "TransportComponent.h"
#include "TrackComponent.h"
#include "TimelineComponent.h"
#include "SettingsDialog.h"
#include "SessionDialog.h"
#include "ProjectManagerDialog.h"
#include "MarkerWindow.h"
#include "AudioEngine.h"
#include "Track.h"

class TapeUI : public juce::Component, public juce::Timer
{
public:
    TapeUI(AudioEngine* engine, Settings* settingsPtr) 
        : audioEngine(engine), settings(settingsPtr)
    {
        setLookAndFeel(&vintageLF);
        
        // Enable keyboard focus
        setWantsKeyboardFocus(true);
        
        // Transport component setup
        transport.onPlay = [this](bool play) { 
            if (audioEngine) {
                if (play) audioEngine->play();
                else audioEngine->stop();
            }
        };
        
        transport.onPause = [this](bool pause) {
            if (audioEngine) {
                if (pause) audioEngine->pause();
                else audioEngine->play();
            }
        };
        
        transport.onStop = [this]() { 
            if (audioEngine) audioEngine->stop();
        };
        
        transport.onRecord = [this](bool rec) {
            if (!audioEngine) return;
            if (rec)
            {
                if (!audioEngine->hasArmedTracks())
                {
                    // Nothing armed — don't start recording, tell user
                    juce::Logger::writeToLog("TapeUI: Record pressed but NO tracks armed");
                    juce::AlertWindow::showMessageBoxAsync(
                        juce::MessageBoxIconType::WarningIcon,
                        "No Tracks Armed",
                        "Press the A button on one or more tracks before recording.",
                        juce::String(),
                        nullptr);
                    return;
                }
                juce::Logger::writeToLog("TapeUI: Starting recording — armed tracks found");
                audioEngine->record();
            }
            else
            {
                // Falling edge of record toggle — stop recording
                juce::Logger::writeToLog("TapeUI: Stopping recording");
                audioEngine->stop();
            }
        };
        
        transport.onRewindStart = [this]() {
            if (audioEngine) audioEngine->rewind();
        };
        transport.onRewindStop = [this]() {
            if (audioEngine) audioEngine->stopRewind();
        };
        
        transport.onFFStart = [this]() {
            if (audioEngine) audioEngine->fastForward();
        };
        transport.onFFStop = [this]() {
            if (audioEngine) audioEngine->stopFastForward();
        };
        
        transport.onBPMChanged = [this](double newBPM) {
            if (audioEngine) audioEngine->setBPM(newBPM);
        };
        
        transport.onClickToggle = [this](bool enabled) {
            if (audioEngine) audioEngine->setClickEnabled(enabled);
        };
        
        transport.onLTCToggle = [this](bool enabled) {
            if (audioEngine) {
                bool slaveMode = settings ? settings->getLTCSlaveMode() : false;
                audioEngine->setLTCInputEnabled(enabled, slaveMode);
            }
        };
        
        transport.onZoomChanged = [this](double delta) {
            if (settings) {
                double currentZoom = settings->getWaveformZoom();
                double newZoom = currentZoom + delta;
                settings->setWaveformZoom(newZoom);
                transport.setZoom(settings->getWaveformZoom());  // Update display
            }
        };
        
        transport.onAmplitudeZoomChanged = [this](double delta) {
            if (settings) {
                double currentZoom = settings->getAmplitudeZoom();
                double newZoom = currentZoom + delta;
                settings->setAmplitudeZoom(newZoom);
                transport.setAmplitudeZoom(settings->getAmplitudeZoom());  // Update display
            }
        };
        
        transport.onGridToggle = [this](bool enabled) {
            if (settings) settings->setGridEnabled(enabled);
        };
        
        transport.onGridResolutionCycle = [this]() {
            if (settings) {
                int current = settings->getGridResolution();
                int next = (current + 1) % 5;  // Cycle through 0-4
                settings->setGridResolution(next);
                transport.setGridResolution(next);
            }
        };
        
        transport.onSpeedChangeSemitones = [this](int semitones) {
            if (audioEngine) {
                audioEngine->adjustSpeedBySemitones(semitones);
                transport.setSpeed(audioEngine->getPlaybackSpeed());
            }
        };
        
        transport.onSpeedReset = [this]() {
            if (audioEngine) {
                audioEngine->setPlaybackSpeed(1.0);
                transport.setSpeed(1.0);
            }
        };
        
        transport.onReverseToggle = [this]() {
            if (audioEngine) {
                audioEngine->toggleReverse();
                transport.setReverseActive(audioEngine->isReverseEnabled());
            }
        };
        
        transport.onSectionSetStart = [this]() {
            if (audioEngine) {
                double currentTime = audioEngine->getCurrentTime();
                audioEngine->setSectionStart(currentTime);
                updateSectionDisplay();
            }
        };
        
        transport.onSectionSetEnd = [this]() {
            if (audioEngine) {
                double currentTime = audioEngine->getCurrentTime();
                audioEngine->setSectionEnd(currentTime);
                updateSectionDisplay();
            }
        };
        
        transport.onSectionClear = [this]() {
            if (audioEngine) {
                audioEngine->clearSection();
                updateSectionDisplay();
            }
        };
        
        transport.onSectionLoopToggle = [this]() {
            if (audioEngine) {
                audioEngine->toggleSectionLoop();
                transport.setSectionLoopActive(audioEngine->isSectionLoopEnabled());
            }
        };
        
        transport.onSettings = [this]() {
            showSettingsDialog();
        };
        
        transport.onSession = [this]() {
            showSessionDialog();
        };
        
        transport.onSave = [this]() {
            bool ok = audioEngine ? audioEngine->saveSessionToDisk()
                                  : (settings ? settings->saveSession() : false);
            if (ok) {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::MessageBoxIconType::InfoIcon,
                    "Session Saved", "Session saved successfully!",
                    juce::String(), nullptr);
            } else {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::MessageBoxIconType::WarningIcon,
                    "Save Failed", "Failed to save session.",
                    juce::String(), nullptr);
            }
        };
        
        addAndMakeVisible(transport);
        addAndMakeVisible(timeline);
        
        // Give timeline access to audio engine for scrubbing
        timeline.setAudioEngine(audioEngine);
        timeline.setSettings(settings);
        
        // Create tracks based on settings
        createTracks();
        
        // Apply defaults from settings
        if (audioEngine && settings)
        {
            double bpm = settings->getDefaultBPM();
            audioEngine->setBPM(bpm);
            transport.setBPM(bpm, 4, 4);
            
            // Sync CLICK and LTC IN button states
            transport.setClickEnabled(audioEngine->getClickEnabled());
            transport.setLTCEnabled(audioEngine->getLTCInputEnabled());
            
            // Initialize zoom displays
            transport.setZoom(settings->getWaveformZoom());
            transport.setAmplitudeZoom(settings->getAmplitudeZoom());
            
            // Initialize grid state
            transport.setGridEnabled(settings->getGridEnabled());
            transport.setGridResolution(settings->getGridResolution());
            
            // Initialize speed display
            if (audioEngine)
                transport.setSpeed(audioEngine->getPlaybackSpeed());
            
            int ltcCh = settings->getLTCInputChannel();
            if (ltcCh >= 0)
            {
                bool slaveMode = settings->getLTCSlaveMode();
                audioEngine->setLTCInputChannel(ltcCh);
                audioEngine->setLTCInputEnabled(true, slaveMode);
            }
        }
        
        // Viewport for horizontal scrolling
        tracksViewport.setViewedComponent(&tracksContainer, false);
        tracksViewport.setScrollBarsShown(false, true);
        addAndMakeVisible(tracksViewport);
        
        // Marker window (initially hidden)
        markerWindow = std::make_unique<MarkerWindow>(audioEngine);
        markerWindow->setVisible(false);
        addAndMakeVisible(markerWindow.get());
        
        // Marker button
        markerButton.setButtonText("M");
        markerButton.setColour(juce::TextButton::buttonColourId, juce::Colour(VintageLookAndFeel::bgPanel));
        markerButton.setColour(juce::TextButton::textColourOffId, juce::Colour(VintageLookAndFeel::accentRed));
        markerButton.onClick = [this]() {
            markerWindowVisible = !markerWindowVisible;
            markerWindow->setVisible(markerWindowVisible);
            resized();  // Re-layout
        };
        addAndMakeVisible(markerButton);
        
        // Fullscreen button
        fullscreenButton.setButtonText("F");
        fullscreenButton.setColour(juce::TextButton::buttonColourId, juce::Colour(VintageLookAndFeel::bgPanel));
        fullscreenButton.setColour(juce::TextButton::textColourOffId, juce::Colour(VintageLookAndFeel::accentAmber));
        fullscreenButton.onClick = [this]() {
            // True OS fullscreen via kiosk mode (not just maximise)
            auto& desktop = juce::Desktop::getInstance();
            if (desktop.getKioskModeComponent() != nullptr)
            {
                // Currently fullscreen — exit
                desktop.setKioskModeComponent(nullptr);
                fullscreenButton.setColour(juce::TextButton::textColourOffId, 
                                          juce::Colour(VintageLookAndFeel::accentAmber));
            }
            else
            {
                // Enter fullscreen using the top-level component
                auto* topLevel = getTopLevelComponent();
                if (topLevel)
                {
                    desktop.setKioskModeComponent(topLevel, true);
                    fullscreenButton.setColour(juce::TextButton::textColourOffId,
                                              juce::Colours::white);
                }
            }
        };
        addAndMakeVisible(fullscreenButton);
        
        // Start UI update timer (15fps to reduce load)
        startTimer(66);
    }
    
    void parentHierarchyChanged() override
    {
        // Grab keyboard focus when added to parent
        if (isShowing())
            grabKeyboardFocus();
    }
    
    void createTracks()
    {
        // Clear existing tracks from container first
        tracksContainer.removeAllChildren();
        
        // Clear the tracks vector
        tracks.clear();
        
        // Only create strips for user tracks — click/timecode have their own transport buttons
        int trackCount = audioEngine ? audioEngine->getUserTrackCount() : (settings ? settings->getTrackCount() : 8);
        int firstUserIdx = audioEngine ? audioEngine->getFirstUserTrackIndex() : 0;
        
        juce::Logger::writeToLog("TapeUI: Creating " + juce::String(trackCount) + " track strips (user tracks only)");
        
        // Create tracks — i = 0-based user track index; engineIdx = actual engine track index
        for (int i = 0; i < trackCount; ++i)
        {
            const int engineIdx = firstUserIdx + i;

            // Check track type using the correct engine index
            bool isMixdown = false;
            TrackType trackType = TrackType::Normal;
            if (audioEngine)
            {
                Track* t = audioEngine->getTrack(engineIdx);
                if (t)
                {
                    trackType = t->getTrackType();
                    isMixdown = t->isMixdown();
                }
            }
        
            auto track = std::make_unique<TrackComponent>(i + 1, isMixdown, trackType);
            track->onArmChanged = [this, i, engineIdx]() {
                bool isArmed = tracks[i]->isArmed();
                juce::Logger::writeToLog("TapeUI: Track " + juce::String(i + 1) + " arm changed to " + juce::String(isArmed ? "ARMED" : "UNARMED"));
                if (audioEngine) audioEngine->setTrackArmed(engineIdx, isArmed);
            };
            track->onMonitorChanged = [this, i, engineIdx]() {
                bool isMonitoring = tracks[i]->isMonitoring();
                juce::Logger::writeToLog("TapeUI: Track " + juce::String(i + 1) + " monitor changed to " + juce::String(isMonitoring ? "ON" : "OFF"));
                if (audioEngine) audioEngine->setTrackMonitoring(engineIdx, isMonitoring);
            };
            
            // Load saved name from engine/session
            if (audioEngine)
            {
                juce::String savedName = audioEngine->getTrackName(engineIdx);
                if (savedName.isNotEmpty())
                    track->setTrackName(savedName);
            }
            
            // Save name back to session when user edits it
            track->onNameChanged = [this, engineIdx](const juce::String& newName) {
                if (audioEngine)
                {
                    audioEngine->setTrackName(engineIdx, newName);
                    audioEngine->saveSessionToDisk();
                }
            };
            track->setSettings(settings);
            tracksContainer.addAndMakeVisible(track.get());
            tracks.push_back(std::move(track));
        }

        // ---- Stereo return strip (always at engine index 2 when session has one) ----
        if (audioEngine)
        {
            Track* stereoTr = audioEngine->getTrack(AudioEngine::kStereoReturnIndex);
            if (stereoTr && stereoTr->isMixdown())
            {
                const int stereoEngineIdx = AudioEngine::kStereoReturnIndex;
                const int stereoUiNum     = trackCount + 1;
                auto stereoStrip = std::make_unique<TrackComponent>(stereoUiNum, true, TrackType::Mixdown);

                juce::String stereoName = audioEngine->getTrackName(stereoEngineIdx);
                if (stereoName.isNotEmpty())
                    stereoStrip->setTrackName(stereoName);

                stereoStrip->onNameChanged = [this, stereoEngineIdx](const juce::String& name) {
                    if (audioEngine) { audioEngine->setTrackName(stereoEngineIdx, name); audioEngine->saveSessionToDisk(); }
                };
                stereoStrip->setSettings(settings);
                tracksContainer.addAndMakeVisible(stereoStrip.get());
                tracks.push_back(std::move(stereoStrip));
            }
        }
        
        juce::Logger::writeToLog("TapeUI: Tracks created, calling resized()");
        
        // Force complete layout update
        tracksContainer.resized();
        resized();
    }
    
    ~TapeUI() override
    {
        stopTimer();
        setLookAndFeel(nullptr);
    }
    
    void paint(juce::Graphics& g) override
    {
        // Background matching web CSS
        juce::ColourGradient bgGradient(
            juce::Colour(0xff2a2520), 0.0f, 0.0f,
            juce::Colour(0xff1a1512), (float)getWidth(), (float)getHeight(), false);
        g.setGradientFill(bgGradient);
        g.fillAll();
    }
    
    void resized() override
    {
        auto bounds = getLocalBounds();
        
        // Transport at top
        auto topBar = bounds.removeFromTop(70);
        
        // Fullscreen button on the left
        fullscreenButton.setBounds(topBar.removeFromLeft(50).reduced(10));
        
        // Marker button on the RIGHT side
        markerButton.setBounds(topBar.removeFromRight(50).reduced(10));
        
        // Transport fills remaining top bar
        transport.setBounds(topBar);
        
        // If marker window visible, take RIGHT side
        if (markerWindowVisible && markerWindow)
        {
            markerWindow->setBounds(bounds.removeFromRight(300));
        }
        
        // Timeline on left (120px), tracks fill remaining space
        auto tracksArea = bounds;
        timeline.setBounds(tracksArea.removeFromLeft(120));
        tracksViewport.setBounds(tracksArea);
        
        // Layout tracks horizontally
        layoutTracks();
    }
    
    void timerCallback() override
    {
        if (!audioEngine) return;
        
        try
        {
            // Safety check: ensure tracks exist
            int engineTrackCount = audioEngine->getNumTracks();
            int uiTrackCount = static_cast<int>(tracks.size());
            
            if (engineTrackCount == 0 || uiTrackCount == 0)
            {
                // Tracks not created yet, skip this update
                return;
            }
            
            // Update transport state
            auto state = audioEngine->getTransportState();
            
            // Format timecode exactly like web version
            double seconds = state.currentTime;
            int hours = (int)(seconds / 3600.0);
            int minutes = ((int)seconds % 3600) / 60;
            int secs = (int)seconds % 60;
            int ms = (int)((seconds - (int)seconds) * 1000.0);
            
            juce::String timecode = juce::String::formatted("%02d:%02d:%02d.%03d", 
                                                            hours, minutes, secs, ms);
            transport.setTimecode(timecode);
            
            // Update transport button states
            // Keep play button lit during pause
            bool showPlayLit = state.isPlaying || state.isPaused;
            transport.setPlaying(showPlayLit);
            transport.setRecording(state.isRecording);
            
            // Sync CLICK and LTC buttons
            transport.setClickEnabled(audioEngine->getClickEnabled());
            transport.setLTCEnabled(audioEngine->getLTCInputEnabled());  // LTC IN button = input slave mode
            
            // Send timecode via OSC
            audioEngine->sendTimecode();
            
            // Update timeline with current time (playhead position calculated internally)
            timeline.setCurrentTime(state.currentTime);
            
            // Calculate playback position for horizontal playhead
            // Loop every 10 seconds for now (normalized 0.0 to 1.0)
            float playbackPos = 0.0f;
            if (state.isPlaying)
            {
                double loopLength = 10.0;  // 10 second loop
                double posInLoop = fmod(state.currentTime, loopLength);
                playbackPos = (float)(posInLoop / loopLength);
            }
            
            // Update track states and playback position
            int userTrackCount = audioEngine->getUserTrackCount();
            int firstUserIdx   = audioEngine->getFirstUserTrackIndex();
            // tracks[] = [user0..userN-1, (stereoStrip?)]
            int maxTracks = juce::jmin(static_cast<int>(tracks.size()), userTrackCount);

            for (int i = 0; i < maxTracks; ++i)
            {
                const int engineIdx = firstUserIdx + i;

                // Safety check: ensure track UI component exists
                if (i >= (int)tracks.size() || !tracks[i])
                    continue;
                
                // Update BPM for grid calculations
                tracks[i]->setBPM(audioEngine->getBPM());
                    
                tracks[i]->setRecording(state.isRecording && tracks[i]->isArmed());
                tracks[i]->setPlaybackState(state.isPlaying, playbackPos);
                
                // Get waveform data from audio engine track
                Track* track = audioEngine->getTrack(engineIdx);
                if (!track)  // Safety check
                    continue;
                
                // Sync ARM and MONITOR states from AudioEngine to UI
                bool engineArmed = track->isArmed();
                    bool engineMonitoring = track->isMonitoring();
                    bool uiArmed = tracks[i]->isArmed();
                    bool uiMonitoring = tracks[i]->isMonitoring();
                    
                    if (engineArmed != uiArmed || engineMonitoring != uiMonitoring)
                    {
                        juce::Logger::writeToLog("TIMER SYNC Track " + juce::String(i + 1) + 
                                                ": Engine armed=" + juce::String(engineArmed ? "true" : "false") +
                                                ", UI armed=" + juce::String(uiArmed ? "true" : "false") +
                                                ", Engine monitor=" + juce::String(engineMonitoring ? "true" : "false") +
                                                ", UI monitor=" + juce::String(uiMonitoring ? "true" : "false"));
                    }
                    
                    tracks[i]->setArmedState(engineArmed);
                    tracks[i]->setMonitoringState(engineMonitoring);
                    
                    // Get waveform peaks (2000 points for high-resolution display)
                    std::vector<float> peaks = track->getWaveformPeaks(5000);
                    
                    // Get current playback position and total length
                    juce::int64 totalLength = track->getTotalLengthInSamples();
                    
                    // Pass waveform data to track component
                    tracks[i]->setWaveformData(peaks, state.currentSample, static_cast<double>(totalLength));
                    
                    // If recording, get real-time recording buffer for waveform display
                    if (track->isRecording())
                    {
                        std::vector<float> recordingBuffer = track->getRecordingBuffer(96000);
                        tracks[i]->setRecordingWaveform(recordingBuffer);
                    }
                
                // Get VU LED states and send via OSC if changed
                bool led1, led2, led3;
                
                // Pull live level from AudioEngine into the track VU meter
                tracks[i]->setLevel(audioEngine->getTrackLevel(engineIdx));
                if (tracks[i]->isMixdownTrack())
                    tracks[i]->setLevelRight(audioEngine->getTrackLevelRight(engineIdx));
                
                tracks[i]->getVULedStates(led1, led2, led3);
                
                // Only send OSC if LED states changed (to avoid spam)
                if (led1 != tracks[i]->lastLed1State || 
                    led2 != tracks[i]->lastLed2State || 
                    led3 != tracks[i]->lastLed3State)
                {
                    if (auto* osc = audioEngine->getOSCController())
                    {
                        osc->sendTrackVU(i, led1, led2, led3);
                    }
                    
                    // Update last states
                    tracks[i]->lastLed1State = led1;
                    tracks[i]->lastLed2State = led2;
                    tracks[i]->lastLed3State = led3;
                }
            }

            // Stereo return strip lives at the end of tracks[] — engine index is always kStereoReturnIndex
            {
                int stereoUiIdx = userTrackCount;  // index in tracks[]
                const int stereoEngineIdx = AudioEngine::kStereoReturnIndex;
                if (stereoUiIdx < (int)tracks.size() && tracks[stereoUiIdx]
                    && tracks[stereoUiIdx]->isMixdownTrack())
                {
                    tracks[stereoUiIdx]->setBPM(audioEngine->getBPM());
                    tracks[stereoUiIdx]->setPlaybackState(state.isPlaying, playbackPos);
                    Track* stereoTr = audioEngine->getTrack(stereoEngineIdx);
                    if (stereoTr)
                    {
                        std::vector<float> peaks = stereoTr->getWaveformPeaks(5000);
                        juce::int64 len = stereoTr->getTotalLengthInSamples();
                        tracks[stereoUiIdx]->setWaveformData(peaks, state.currentSample, (double)len);
                    }
                    tracks[stereoUiIdx]->setLevel(audioEngine->getTrackLevel(stereoEngineIdx));
                    tracks[stereoUiIdx]->setLevelRight(audioEngine->getTrackLevelRight(stereoEngineIdx));
                }
            }
            
            // Update section display
            updateSectionDisplay();
        }
        catch (...)
        {
            // Catch any exceptions during UI update
        }
    }

private:
    void updateSectionDisplay()
    {
        if (!audioEngine) return;
        
        double startTime = audioEngine->getSectionStart();
        double endTime = audioEngine->getSectionEnd();
        double bpm = audioEngine->getBPM();
        
        transport.setSectionPositions(startTime, endTime, bpm);
    }
    
    void showSettingsDialog()
    {
        // Use SafePointer to avoid dangling pointer issues
        auto safeThis = juce::Component::SafePointer<TapeUI>(this);
        
        try
        {
            juce::Logger::writeToLog("Creating settings dialog...");
            
            // Create modal settings dialog
            auto* dialog = new SettingsDialog(settings, audioEngine->getDeviceManager(), audioEngine);
            
            juce::Logger::writeToLog("Settings dialog created, setting up callbacks...");
            
            dialog->onClose = [dialog]() {
                juce::Logger::writeToLog("Settings dialog closing...");
                // Remove from desktop before deleting
                dialog->removeFromDesktop();
                juce::MessageManager::callAsync([dialog]() {
                    juce::Logger::writeToLog("Deleting settings dialog...");
                    delete dialog;
                });
            };
            
            dialog->onApply = [safeThis]() {
                if (safeThis != nullptr)
                {
                    juce::Logger::writeToLog("Settings applied - refreshing engine then recreating tracks");
                    // Refresh engine first so getUserTrackCount() is correct before building strips
                    if (safeThis->audioEngine)
                        safeThis->audioEngine->refreshTracksFromSettings();
                    safeThis->createTracks();
                }
            };
            
            juce::Logger::writeToLog("Adding dialog to desktop...");
            
            // Add to desktop as modal component
            dialog->addToDesktop(juce::ComponentPeer::windowIsTemporary | 
                               juce::ComponentPeer::windowHasTitleBar);
            dialog->centreWithSize(700, 550);
            dialog->setVisible(true);
            dialog->toFront(true);
            
            juce::Logger::writeToLog("Settings dialog shown successfully");
        }
        catch (const std::exception& e)
        {
            juce::Logger::writeToLog("EXCEPTION showing settings dialog: " + juce::String(e.what()));
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::WarningIcon,
                "Settings Error",
                "Failed to open settings dialog:\n" + juce::String(e.what()),
                juce::String(),
                nullptr);
        }
        catch (...)
        {
            juce::Logger::writeToLog("UNKNOWN EXCEPTION showing settings dialog");
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::WarningIcon,
                "Settings Error",
                "Failed to open settings dialog: Unknown error",
                juce::String(),
                nullptr);
        }
    }

    void showSessionDialog()
    {
        auto safeThis = juce::Component::SafePointer<TapeUI>(this);

        auto* dialog = new SessionDialog(
            settings,
            audioEngine,
            audioEngine ? audioEngine->getCurrentSession() : nullptr,
            audioEngine ? audioEngine->getDeviceManager() : nullptr);

        dialog->onManageSessions = [safeThis]() {
            if (safeThis) safeThis->showProjectManager();
        };

        dialog->onClose = [dialog, safeThis]() {
            dialog->removeFromDesktop();
            juce::MessageManager::callAsync([dialog, safeThis]() {
                delete dialog;
                // Refresh tracks if track count may have changed
                if (safeThis && safeThis->audioEngine)
                    safeThis->audioEngine->refreshTracksFromSettings();
                if (safeThis)
                    safeThis->createTracks();
            });
        };

        dialog->addToDesktop(juce::ComponentPeer::windowIsTemporary |
                             juce::ComponentPeer::windowHasTitleBar);
        dialog->centreWithSize(700, 630);
        dialog->setVisible(true);
        dialog->toFront(true);
    }

    void showProjectManager()
    {
        juce::DialogWindow::LaunchOptions opts;
        opts.content.setOwned(new ProjectManagerDialog(settings));
        opts.dialogTitle           = "Manage Sessions";
        opts.dialogBackgroundColour = juce::Colour(VintageLookAndFeel::bgPanel);
        opts.escapeKeyTriggersCloseButton = true;
        opts.useNativeTitleBar     = false;
        opts.resizable             = true;
        opts.launchAsync();
    }
    
    bool keyPressed(const juce::KeyPress& key) override
    {
        if (!audioEngine) return false;
        
        // M = Add marker at current position
        if (key.getKeyCode() == 'M' && !key.getModifiers().isAnyModifierKeyDown())
        {
            double currentTime = audioEngine->getTransportState().currentTime;
            audioEngine->addMarker(currentTime, "");
            if (markerWindow)
                markerWindow->refreshMarkerList();
            return true;
        }
        
        // Shift+M = Add marker and open window for editing
        if (key.getKeyCode() == 'M' && key.getModifiers().isShiftDown())
        {
            double currentTime = audioEngine->getTransportState().currentTime;
            audioEngine->addMarker(currentTime, "");
            
            // Show marker window so user can immediately edit label
            if (!markerWindowVisible)
            {
                markerWindowVisible = true;
                markerWindow->setVisible(true);
                resized();
            }
            
            if (markerWindow)
                markerWindow->refreshMarkerList();
            
            return true;
        }
        
        // 1-8 = Jump to markers 1-8
        int keyCode = key.getKeyCode();
        if (keyCode >= '1' && keyCode <= '8' && !key.getModifiers().isAnyModifierKeyDown())
        {
            int markerNumber = keyCode - '0';
            audioEngine->goToMarker(markerNumber);
            return true;
        }
        
        // Ctrl+Right = Next marker
        if (key.getKeyCode() == juce::KeyPress::rightKey && key.getModifiers().isCtrlDown())
        {
            audioEngine->goToNextMarker();
            return true;
        }
        
        // Ctrl+Left = Previous marker
        if (key.getKeyCode() == juce::KeyPress::leftKey && key.getModifiers().isCtrlDown())
        {
            audioEngine->goToPreviousMarker();
            return true;
        }
        
        // Delete = Remove marker at current position (within 0.5s tolerance)
        if (key.getKeyCode() == juce::KeyPress::deleteKey)
        {
            double currentTime = audioEngine->getTransportState().currentTime;
            auto* markerManager = audioEngine->getMarkerManager();
            if (markerManager)
            {
                markerManager->removeMarkerAtTime(currentTime, 0.5);
                if (markerWindow)
                    markerWindow->refreshMarkerList();
            }
            return true;
        }
        
        // Up Arrow = Skip to next beat
        if (key.getKeyCode() == juce::KeyPress::upKey && !key.getModifiers().isAnyModifierKeyDown())
        {
            audioEngine->skipToNextBeat();
            return true;
        }
        
        // Down Arrow = Skip to previous beat
        if (key.getKeyCode() == juce::KeyPress::downKey && !key.getModifiers().isAnyModifierKeyDown())
        {
            audioEngine->skipToPreviousBeat();
            return true;
        }
        
        // Shift+Up Arrow = Skip to next bar
        if (key.getKeyCode() == juce::KeyPress::upKey && key.getModifiers().isShiftDown())
        {
            audioEngine->skipToNextBar();
            return true;
        }
        
        // Shift+Down Arrow = Skip to previous bar
        if (key.getKeyCode() == juce::KeyPress::downKey && key.getModifiers().isShiftDown())
        {
            audioEngine->skipToPreviousBar();
            return true;
        }
        
        return false;
    }
    
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override
    {
        // Check if mouse is over timeline or tracks
        auto timelineBounds = timeline.getBounds();
        auto tracksBounds = tracksViewport.getBounds();
        auto mousePos = event.getPosition();
        
        // Vertical scroll wheel (normal) = scroll timeline up/down
        if (std::abs(wheel.deltaY) > std::abs(wheel.deltaX))
        {
            if (timelineBounds.contains(mousePos))
            {
                // Scrolling over timeline - seek through time
                if (audioEngine)
                {
                    auto state = audioEngine->getTransportState();
                    double currentTime = state.currentTime;
                    
                    // Scroll sensitivity: 1 wheel tick = 0.5 seconds
                    double deltaTime = -wheel.deltaY * 0.5;
                    double newTime = currentTime + deltaTime;
                    
                    // Don't go below 0
                    if (newTime < 0.0)
                        newTime = 0.0;
                    
                    audioEngine->seek(newTime);
                }
            }
        }
        // Horizontal scroll wheel (shift+scroll or horizontal trackpad) = scroll tracks left/right
        else
        {
            if (tracksBounds.contains(mousePos))
            {
                // Scrolling over tracks - scroll horizontally
                auto& scrollBar = tracksViewport.getHorizontalScrollBar();
                if (scrollBar.isVisible())
                {
                    double currentPos = scrollBar.getCurrentRangeStart();
                    double deltaScroll = -wheel.deltaX * 50.0;  // Sensitivity
                    scrollBar.setCurrentRangeStart(currentPos + deltaScroll);
                }
            }
        }
    }
    
    void layoutTracks()
    {
        const int trackWidth  = 100;
        const int trackSpacing = 8;
        const int padding     = 20;

        // Total width accounts for mixdown tracks being double-width
        int totalWidth = padding * 2;
        for (auto& track : tracks)
        {
            int w = track->isMixdownTrack() ? trackWidth * 2 + trackSpacing : trackWidth;
            totalWidth += w + trackSpacing;
        }
        int trackHeight = tracksViewport.getHeight() - padding * 2;

        tracksContainer.setSize(totalWidth, trackHeight + padding * 2);

        int x = padding;
        int y = padding;
        for (auto& track : tracks)
        {
            int w = track->isMixdownTrack() ? trackWidth * 2 + trackSpacing : trackWidth;
            track->setBounds(x, y, w, trackHeight);
            x += w + trackSpacing;
        }
    }
    
    AudioEngine* audioEngine;
    Settings* settings;
    
    VintageLookAndFeel vintageLF;
    TransportComponent transport;
    TimelineComponent timeline;
    juce::Viewport tracksViewport;
    juce::Component tracksContainer;
    std::vector<std::unique_ptr<TrackComponent>> tracks;
    
    // Marker system
    std::unique_ptr<MarkerWindow> markerWindow;
    juce::TextButton markerButton;
    juce::TextButton fullscreenButton;
    bool markerWindowVisible = false;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TapeUI)
};
