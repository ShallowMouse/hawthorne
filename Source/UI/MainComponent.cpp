#include "MainComponent.h"
#include "UIHelpers.h"

#ifndef HAWTHORNE_VERSION
 #define HAWTHORNE_VERSION "dev"
#endif

namespace hawthorne
{

//==============================================================================
void TransportKey::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);

    // key body
    juce::ColourGradient body (Colours::panelLight.brighter (0.15f), r.getX(), r.getY(),
                               Colours::panelLight.darker (0.25f), r.getX(), r.getBottom(), false);
    g.setGradientFill (body);
    g.fillRoundedRectangle (r, 3.0f);
    if (down)             { g.setColour (juce::Colour (0x40000000)); g.fillRoundedRectangle (r, 3.0f); }
    else if (highlighted) { g.setColour (juce::Colour (0x12ffffff)); g.fillRoundedRectangle (r, 3.0f); }
    g.setColour (Colours::black);
    g.drawRoundedRectangle (r, 3.0f, 1.0f);

    // lamp strip
    auto lampRect = r.removeFromTop (5.0f).reduced (6.0f, 1.0f);
    if (lit)
    {
        g.setColour (lamp.withAlpha (0.35f));
        g.fillRoundedRectangle (lampRect.expanded (2.0f, 2.0f), 2.0f);
    }
    g.setColour (lit ? lamp : lamp.darker (2.4f));
    g.fillRoundedRectangle (lampRect, 1.5f);

    g.setColour (getButtonText() == "REC" ? Colours::red.brighter (0.2f) : Colours::text);
    g.setFont (uiFont (13.0f, true));
    g.drawText (getButtonText(), r, juce::Justification::centred);
}

//==============================================================================
int MeterBridge::layoutForWidth (int width, int minHeight)
{
    const int pad = 10, gap = 8;
    const float moduleW = ((float) (width - 2 * pad) - (float) (perRow - 1) * gap) / (float) perRow;
    const int meterH = (int) (moduleW / ChannelModule::meterAspect);
    const int moduleH = meterH + ChannelModule::controlsHeight + 4;
    const int rows = (modules.size() + perRow - 1) / perRow;
    const int needed = pad + rows * (moduleH + 2 * gap);
    const int top = juce::jmax (0, (minHeight - needed) / 2);   // centre when there's room

    rowPanels.clear();
    for (int row = 0; row < rows; ++row)
    {
        const int y = top + pad + row * (moduleH + 2 * gap);
        rowPanels.push_back ({ pad / 2, y - gap / 2, width - pad, moduleH + gap });

        for (int col = 0; col < perRow; ++col)
        {
            const int i = row * perRow + col;
            if (i >= modules.size()) break;
            const int x = pad + juce::roundToInt ((float) col * (moduleW + (float) gap));
            modules[i]->setBounds (x, y, juce::roundToInt (moduleW), moduleH);
        }
    }

    const int h = juce::jmax (needed, minHeight);
    setSize (width, h);
    return h;
}

void MeterBridge::paint (juce::Graphics& g)
{
    g.fillAll (Colours::bg);
    for (auto& p : rowPanels)
    {
        g.setColour (Colours::black.brighter (0.04f));
        g.fillRoundedRectangle (p.toFloat(), 4.0f);
        g.setColour (Colours::edge.withAlpha (0.6f));
        g.drawRoundedRectangle (p.toFloat().reduced (0.5f), 4.0f, 1.0f);
    }
}

//==============================================================================
MainComponent::MainComponent (juce::ApplicationProperties& p) : props (p)
{
    setLookAndFeel (&lnf);
    juce::LookAndFeel::setDefaultLookAndFeel (&lnf);
    setWantsKeyboardFocus (true);

    for (auto* b : { &libraryButton, &sheetButton, &routingButton, &networkButton, &syncButton, &timecodeButton, &audioButton })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }
    libraryButton.onClick = [this] { showLibrary (true); };
    sheetButton.onClick   = [this] { showSheet(); };
    routingButton.onClick = [this] { showRouting(); };
    networkButton.onClick = [this] { showNetwork(); };
    syncButton.onClick = [this] { showSync (SyncPanel::Page::tempo); };
    syncButton.setTooltip ("Tempo, bars and beats, click");
    timecodeButton.onClick = [this] { showSync (SyncPanel::Page::timecode); };
    timecodeButton.setTooltip ("Timecode out (LTC / MTC) and chase");
    fullScreenButton.onClick = [this] { toggleFullScreen(); };
    fullScreenButton.setTooltip ("Full screen (F11)");
    fullScreenButton.setWantsKeyboardFocus (false);
    addAndMakeVisible (fullScreenButton);
    networkButton.setTooltip ("Hawthorne Link: record audio sent from other PCs");
    audioButton.onClick   = [this] { showAudioSettings(); };

    for (auto* k : { &rtzKey, &rewKey, &ffwKey, &stopKey, &playKey, &recKey, &markKey, &clickKey })
        addAndMakeVisible (k);
    rtzKey.onClick  = [this] { locate (0); };
    rewKey.onClick  = [this] { nudge (-5.0); };
    ffwKey.onClick  = [this] { nudge (5.0); };
    stopKey.onClick = [this] { doStop(); };
    playKey.onClick = [this] { doPlay(); };
    recKey.onClick  = [this] { doRecord(); };
    markKey.onClick = [this] { addMarker(); };
    clickKey.onClick = [this]
    {
        if (reel == nullptr) return;
        if (reel->tempoBpm <= 0.0) { showSync (SyncPanel::Page::tempo); return; }      // needs a tempo first
        engine.setClick (! reel->clickOn, reel->clickLevelDb);
        for (auto* w : { tempoWindow.get(), timecodeWindow.get() }) if (w != nullptr) w->getPanel().refresh();
    };

    bridgeView.setViewedComponent (&bridge, false);
    bridgeView.setWantsKeyboardFocus (false);
    bridgeView.setScrollBarsShown (true, false);
    bridgeView.setScrollBarThickness (10);
    addAndMakeVisible (bridgeView);

    masterL.setTooltip ("Master L");
    masterR.setTooltip ("Master R");
    addAndMakeVisible (masterL);
    addAndMakeVisible (masterR);

    masterButton.setWantsKeyboardFocus (false);
    masterButton.setTooltip ("Masters on this reel: choose, add, rename, delete, export");
    masterButton.onClick = [this] { showMasterMenu(); };
    addAndMakeVisible (masterButton);
    addAndMakeVisible (masterArmKey);
    addAndMakeVisible (monitorKey);
    masterArmKey.onClick = [this]
    {
        if (reel == nullptr || engine.isRecording()) return;
        if (reel->activeMaster < 0) { newMaster(); if (reel->activeMaster < 0) return; }
        engine.setMasterArmed (! engine.isMasterArmed());
    };
    monitorKey.onClick = [this] { engine.setMonitorMaster (! engine.isMonitoringMaster()); };

    // Speed: IPS switch + varispeed
    for (auto* k : { &ips7Key, &ips15Key, &ips30Key, &variKey })
        addAndMakeVisible (k);
    ips7Key.onClick  = [this] { if (reel != nullptr) changeSpeed (7.5,  reel->variOn, reel->variPercent); };
    ips15Key.onClick = [this] { if (reel != nullptr) changeSpeed (15.0, reel->variOn, reel->variPercent); };
    ips30Key.onClick = [this] { if (reel != nullptr) changeSpeed (30.0, reel->variOn, reel->variPercent); };
    variKey.onClick  = [this] { if (reel != nullptr) changeSpeed (reel->deckIps, ! reel->variOn, reel->variPercent); };

    variKnob.setRange (-TapeEngine::maxVarispeedPercent, TapeEngine::maxVarispeedPercent, 0.0);
    variKnob.setDoubleClickReturnValue (true, 0.0);
    variKnob.setMouseDragSensitivity (800);              // 0.125 % per pixel; type a value for exact
    variKnob.setVelocityModeParameters (0.4, 1, 0.0, true, juce::ModifierKeys::shiftModifier);   // shift = fine
    variKnob.setWantsKeyboardFocus (false);
    variKnob.setTooltip ("Varispeed -50 % .. +50 %. Drag (shift = fine), double-click for 0.");
    variKnob.onValueChange = [this] { if (reel != nullptr) changeSpeed (reel->deckIps, true, variKnob.getValue(), false); };
    variKnob.onDragEnd = [this] { saveReel(); };
    addAndMakeVisible (variKnob);

    // Readouts: % and semitones (each can still be double-clicked to type an exact value)
    for (auto* l : { &variPctLabel, &variStLabel })
    {
        l->setEditable (false, true, false);
        l->setJustificationType (juce::Justification::centred);
        l->setFont (monoFont (13.0f, true));
        l->setColour (juce::Label::backgroundColourId, Colours::black);
        l->setColour (juce::Label::outlineColourId, Colours::edge);
        l->setWantsKeyboardFocus (false);
        addAndMakeVisible (l);
    }
    variPctLabel.setTooltip ("Varispeed in percent (double-click to type one)");
    variStLabel.setTooltip ("Varispeed in semitones (double-click to type one)");
    variPctLabel.onTextChange = [this]
    {
        if (reel == nullptr) return;
        const double v = variPctLabel.getText().retainCharacters ("+-0123456789.").getDoubleValue();
        changeSpeed (reel->deckIps, true, juce::jlimit (-TapeEngine::maxVarispeedPercent, TapeEngine::maxVarispeedPercent, v));
    };
    variStLabel.onTextChange = [this]
    {
        if (reel == nullptr) return;
        const double st = variStLabel.getText().retainCharacters ("+-0123456789.").getDoubleValue();
        changeSpeed (reel->deckIps, true, juce::jlimit (-TapeEngine::maxVarispeedPercent, TapeEngine::maxVarispeedPercent,
                                                        (std::pow (2.0, st / 12.0) - 1.0) * 100.0));
    };

    // Semitone steps and reset
    for (auto* b : { &stDownButton, &stUpButton, &variResetButton })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }
    stDownButton.setTooltip ("Down to the next whole semitone");
    stUpButton.setTooltip ("Up to the next whole semitone");
    variResetButton.setTooltip ("Back to 0 (normal speed)");
    stDownButton.onClick    = [this] { stepSemitone (-1); };
    stUpButton.onClick      = [this] { stepSemitone (+1); };
    variResetButton.onClick = [this] { if (reel != nullptr) changeSpeed (reel->deckIps, reel->variOn, 0.0); };

    markers.onLocate    = [this] (juce::int64 pos) { locate (pos); };
    markers.onAddMarker = [this] { addMarker(); };
    markers.onChanged   = [this] { saveReel(); };
    markers.getPlayhead = [this] { return engine.getPosition(); };
    addAndMakeVisible (markers);

    engine.onAlert = [this] (const juce::String& t, const juce::String& m) { showAlert (t, m); };
    engine.onTakeLogged = [this] (const TakeEntry&)
    {
        if (sheetWindow != nullptr)
            sheetWindow->getSheet().refresh();
    };

    // Updates: checked in the background every time Hawthorne starts (like PEQUOD Host)
    updateButton.setColour (juce::TextButton::buttonColourId, Colours::mustard);
    updateButton.setColour (juce::TextButton::textColourOffId, Colours::black);
    updateButton.setWantsKeyboardFocus (false);
    updateButton.onClick = [this] { confirmUpdate(); };
    addChildComponent (updateButton);
    updater.onChange = [this] { refreshUpdateButton(); };
    updater.onStatus = [this] (const juce::String& s) { showAlert ("Update", s); };
   #if JUCE_WINDOWS
    updater.checkSoon();
   #endif

    library.onLoadReel    = [this] (const juce::File& f) { loadReel (f); };
    library.onBackToDeck  = [this] { showLibrary (false); };
    library.onColourChanged = [this] (const juce::File& f, juce::Colour c)
    {
        // the loaded reel / session must not save its old colour back
        const auto argb = c.isTransparent() ? 0u : c.getARGB();
        if (reel != nullptr && reel->getFile() == f)          reel->colour = argb;
        if (session != nullptr && session->getFile() == f)    session->colour = argb;
    };
    library.getLoadedReel = [this] { return reel != nullptr ? reel->getFile() : juce::File(); };
    library.getSampleRate = [this]
    {
        auto* dev = deviceManager.getCurrentAudioDevice();
        return dev != nullptr ? dev->getCurrentSampleRate() : 48000.0;
    };
    addChildComponent (library);

    // Hawthorne Link: network inputs, ready before the audio starts
    engine.setNetworkReceiver (&netRx);
    {
        juce::String netErr;
        netRx.start (netErr);
        loadNetworkSettings();
    }

    initialiseAudio();
    loadSyncSettings();
    showLibrary (true);   // the tape library is the first thing you see

    setSize (1600, 900);
    startTimerHz (30);
}

MainComponent::~MainComponent()
{
    shutdown();
    sheetWindow = nullptr;
    routingWindow = nullptr;
    tempoWindow = nullptr;
    timecodeWindow = nullptr;
    networkWindow = nullptr;
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void MainComponent::shutdown()
{
    stopTimer();
    engine.stop();                      // finalises any take in progress
    deviceManager.removeAudioCallback (&engine);
    deviceManager.removeChangeListener (this);
    deviceManager.closeAudioDevice();
    mtcOutput.close();
    mtcInput.close();
    saveNetworkSettings();
    netRx.stop();
    juce::Desktop::setScreenSaverEnabled (true);
}

//==============================================================================
// Audio device

void MainComponent::initialiseAudio()
{
    auto saved = props.getUserSettings()->getXmlValue ("audioDeviceState");

    const auto err = deviceManager.initialise (kDefaultTracks, 2, saved.get(), true);

    if (saved == nullptr)
    {
        // First run on Windows: prefer ASIO when a driver is installed.
        for (auto* type : deviceManager.getAvailableDeviceTypes())
            if (type->getTypeName() == "ASIO")
            {
                type->scanForDevices();
                if (! type->getDeviceNames().isEmpty())
                    deviceManager.setCurrentAudioDeviceType ("ASIO", true);
            }
    }

    deviceManager.addAudioCallback (&engine);
    deviceManager.addChangeListener (this);

    if (err.isNotEmpty())
        showAlert ("Audio device", "Could not open the audio device:\n" + err + "\n\nOpen Audio Settings to choose one.");
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (auto xml = deviceManager.createStateXml())
    {
        props.getUserSettings()->setValue ("audioDeviceState", xml.get());
        props.saveIfNeeded();
    }

    applyStoredLatency();
    updateRoutingDevice();
    refreshInputChoices();
}

juce::String MainComponent::latencyKey() const
{
    auto* dev = deviceManager.getCurrentAudioDevice();
    if (dev == nullptr) return {};
    return juce::String::toHexString ((dev->getTypeName() + "|" + dev->getName()).hashCode64());
}

void MainComponent::applyStoredLatency()
{
    const auto key = latencyKey();
    auto* s = props.getUserSettings();
    engine.setMeasuredRoundTrip (key.isNotEmpty() ? s->getIntValue ("rtt_" + key, 0) : 0);
}

void MainComponent::matchDeviceToSession()
{
    if (reel == nullptr || deviceManager.getCurrentAudioDevice() == nullptr)
        return;

    auto setup = deviceManager.getAudioDeviceSetup();

    if (std::abs (setup.sampleRate - reel->sampleRate) > 0.5)
    {
        setup.sampleRate = reel->sampleRate;
        deviceManager.setAudioDeviceSetup (setup, true);
    }
}

void MainComponent::showAudioSettings()
{
    auto* selector = new juce::AudioDeviceSelectorComponent (deviceManager, 0, 256, 2, 256, false, false, false, false);
    selector->setSize (560, 560);

    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (selector);
    o.dialogTitle = "Audio Settings";
    o.dialogBackgroundColour = Colours::panel;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = true;
    o.launchAsync();
}

//==============================================================================
// Sessions

void MainComponent::showLibrary (bool show)
{
    if (show)
    {
        library.refresh();
        if (reel != nullptr)
            library.select (reel->getFile());
        else
            library.select (juce::File (props.getUserSettings()->getValue ("lastReel")));
        library.setBounds (getLocalBounds());
        library.setVisible (true);
        library.toFront (true);
        library.grabKeyboardFocus();
    }
    else if (reel != nullptr)
    {
        library.setVisible (false);
        grabKeyboardFocus();
    }
}

void MainComponent::loadReel (const juce::File& reelFile)
{
    if (engine.isRecording())
    {
        showAlert ("Library", "Stop recording before changing reels.");
        return;
    }

    juce::String err;
    auto newReel = Reel::load (reelFile, err);
    if (newReel == nullptr)
    {
        showAlert ("Could not load the reel", err);
        return;
    }

    auto newSession = Session::openFolder (reelFile.getParentDirectory().getParentDirectory(), err);
    if (newSession == nullptr)
    {
        showAlert ("Could not load the session", err);
        return;
    }

    setReel (std::move (newReel), std::move (newSession));
    showLibrary (false);
}

void MainComponent::setReel (std::unique_ptr<Reel> r, std::unique_ptr<Session> s)
{
    engine.stop();
    engine.setReel (nullptr);
    markers.setReel (nullptr);
    reel = std::move (r);
    session = std::move (s);
    engine.setReel (reel.get());
    markers.setReel (reel.get());

    if (reel != nullptr)
    {
        props.getUserSettings()->setValue ("lastReel", reel->getFile().getFullPathName());
        props.saveIfNeeded();
        matchDeviceToSession();

        if (! reel->recoveryLog.isEmpty())
            showAlert ("Recovered after an unexpected shutdown", reel->recoveryLog.joinIntoString ("\n"));
    }

    pushReelSync();
    rebuildModules();
    refreshMasterSection();
    refreshSpeedSection();
    for (auto* w : { tempoWindow.get(), timecodeWindow.get() }) if (w != nullptr) w->getPanel().refresh();

    if (routingWindow != nullptr) { routingWindow->getMatrix().tracksChanged(); routingWindow->getLatencyPanel().refresh(); }
    if (sheetWindow != nullptr)   sheetWindow->getSheet().refresh();
    repaint();
}

void MainComponent::saveReel()
{
    juce::String err;
    if (reel != nullptr && ! reel->save (err))
        showAlert ("Could not save the reel", err);
}

//==============================================================================
// Hawthorne Sync

void MainComponent::pushReelSync()
{
    if (reel != nullptr)
        netRx.setReelSync (true, reel->getName(), reel->tempoBpm, reel->beatsPerBar, reel->barOne);
    else
        netRx.setReelSync (false, {}, 0.0, 4, 0.0);
}

void MainComponent::handleTempoRequests()
{
    int slot = -1;
    net::TempoRequest tr;
    bool changed = false;
    while (netRx.popTempoRequest (slot, tr))
    {
        if (reel == nullptr) continue;
        if (reel->tempoBpm != tr.bpm || reel->beatsPerBar != tr.beatsPerBar || reel->barOne != tr.barOne)
        {
            reel->tempoBpm = tr.bpm;
            reel->beatsPerBar = tr.beatsPerBar;
            reel->barOne = tr.barOne;
            changed = true;
        }
    }

    if (changed)
    {
        engine.setTempo (reel->tempoBpm, reel->beatsPerBar, reel->barOne);
        for (auto* w : { tempoWindow.get(), timecodeWindow.get() }) if (w != nullptr) w->getPanel().refresh();
        markers.refresh();
        pushReelSync();
        tempoSaveDueMs = juce::Time::getMillisecondCounterHiRes() + 1500.0;   // a knob may still be turning
        repaint (reelInfoArea);
    }

    if (tempoSaveDueMs > 0.0 && juce::Time::getMillisecondCounterHiRes() >= tempoSaveDueMs)
    {
        tempoSaveDueMs = 0.0;
        saveReel();
    }
}

juce::String MainComponent::tempoText() const
{
    int bar = 0, beat = 0;
    if (reel == nullptr || ! reel->barBeatAt (engine.getPosition(), bar, beat))
        return {};
    return "BAR " + juce::String (bar) + "." + juce::String (beat) + "  " + juce::String (reel->tempoBpm, 1) + " BPM";
}

void MainComponent::saveSessionInfo()
{
    juce::String err;
    if (session != nullptr && ! session->save (err))
        showAlert ("Could not save the session", err);
}

//==============================================================================
// Bridge

void MainComponent::rebuildModules()
{
    bridge.modules.clear();
    const int n = reel != nullptr ? reel->numTracks : 0;

    for (int t = 0; t < n; ++t)
    {
        auto* m = bridge.modules.add (new ChannelModule (t));
        m->onArm = [this] (int track, bool arm)
        {
            if (! engine.isRecording())     // arming is locked during a take
                engine.setArmed (track, arm);
        };
        m->onRenameRequest = [this] (int track) { renameTrack (track); };
        bridge.addAndMakeVisible (m);
    }

    refreshTrackNames();
    refreshInputChoices();
    layoutBridge();
}

void MainComponent::layoutBridge()
{
    const int w = bridgeView.getWidth() - bridgeView.getScrollBarThickness();
    if (w > 100)
        bridge.layoutForWidth (w, bridgeView.getHeight());
}

void MainComponent::refreshInputChoices()
{
    if (routingWindow != nullptr)
        routingWindow->getMatrix().repaint();
}

void MainComponent::refreshTrackNames()
{
    if (reel == nullptr)
        return;

    for (int t = 0; t < bridge.modules.size(); ++t)
        bridge.modules[t]->setTrackName (reel->tracks[(size_t) t].name);

    if (routingWindow != nullptr)
        routingWindow->getMatrix().repaint();
}

void MainComponent::renameTrack (int track)
{
    if (reel == nullptr || ! juce::isPositiveAndBelow (track, reel->numTracks))
        return;

    auto* w = new juce::AlertWindow ("Track " + juce::String (track + 1), "Name:", juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", reel->tracks[(size_t) track].name);
    w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w, track] (int r)
    {
        if (r != 1 || reel == nullptr) return;
        auto name = w->getTextEditorContents ("name").trim();
        reel->tracks[(size_t) track].name = name.isEmpty() ? "Track " + juce::String (track + 1) : name;
        saveReel();
        refreshTrackNames();
        if (sheetWindow != nullptr) sheetWindow->getSheet().refresh();
    }), true);
}

//==============================================================================
// Markers

void MainComponent::addMarker()
{
    if (reel == nullptr)
        return;

    reel->addMarker (engine.getPosition(), "Marker " + juce::String ((int) reel->markers.size() + 1));
    saveReel();
    markers.refresh();
}

void MainComponent::jumpToMarker (int direction)
{
    if (reel == nullptr || reel->markers.empty() || engine.isRecording())
        return;

    const auto pos = engine.getPosition();
    const auto slack = (juce::int64) (reel->sampleRate * 0.25);   // so "previous" while rolling isn't the one just passed

    if (direction > 0)
    {
        for (auto& m : reel->markers)
            if (m.position > pos) { locate (m.position); return; }
    }
    else
    {
        for (auto it = reel->markers.rbegin(); it != reel->markers.rend(); ++it)
            if (it->position < pos - slack) { locate (it->position); return; }
        locate (reel->markers.front().position);
    }
}

//==============================================================================
// Speed

void MainComponent::changeSpeed (double deckIps, bool variOn, double variPercent, bool save)
{
    if (reel == nullptr)
        return;

    if (auto err = engine.setSpeed (deckIps, variOn, variPercent, save); err.isNotEmpty())
        showAlert ("Speed", err);
    refreshSpeedSection();
}

void MainComponent::refreshSpeedSection()
{
    const bool has = reel != nullptr;
    const double deck = has ? reel->deckIps : 15.0;
    ips7Key.setLit (has && deck == 7.5);
    ips15Key.setLit (has && deck == 15.0);
    ips30Key.setLit (has && deck == 30.0);
    variKey.setLit (has && reel->variOn);

    const double pct = has ? reel->variPercent : 0.0;
    variKnob.setValue (pct, juce::dontSendNotification);

    const double st = 12.0 * std::log2 (1.0 + pct / 100.0);
    auto signedFixed = [] (double v) { return (v >= 0.0 ? "+" : "") + juce::String (v, 2); };
    if (! variPctLabel.isBeingEdited()) variPctLabel.setText (signedFixed (pct) + " %", juce::dontSendNotification);
    if (! variStLabel.isBeingEdited())  variStLabel.setText (signedFixed (st) + " st", juce::dontSendNotification);
    for (auto* l : { &variPctLabel, &variStLabel })
        l->setColour (juce::Label::textColourId, has && reel->variOn ? Colours::mustard : Colours::textDim);
    repaint (reelInfoArea);
}

void MainComponent::stepSemitone (int direction)
{
    if (reel == nullptr)
        return;

    // From wherever it is now to the next whole semitone up / down.
    const double cur = reel->variOn ? 12.0 * std::log2 (1.0 + reel->variPercent / 100.0) : 0.0;
    double target = direction > 0 ? std::floor (cur + 1.0e-6) + 1.0 : std::ceil (cur - 1.0e-6) - 1.0;
    const double maxSt = 12.0 * std::log2 (1.0 + TapeEngine::maxVarispeedPercent / 100.0);
    const double minSt = 12.0 * std::log2 (1.0 - TapeEngine::maxVarispeedPercent / 100.0);
    target = juce::jlimit (std::ceil (minSt - 1.0e-6), std::floor (maxSt + 1.0e-6), target);
    changeSpeed (reel->deckIps, true, (std::pow (2.0, target / 12.0) - 1.0) * 100.0);
}

juce::String MainComponent::speedText() const
{
    if (reel == nullptr)
        return {};
    const double now = engine.isPlaying() && engine.isDeviceRunning() ? engine.getSpeed() : engine.getTargetSpeed();
    const double ips = reel->ips * now;
    return Reel::formatIps (reel->ips) + " IPS REEL  -  TAPE " + juce::String (ips, ips < 10.0 ? 2 : 1) + " ips  x" + juce::String (now, 4);
}

//==============================================================================
// Masters

void MainComponent::refreshMasterSection()
{
    const bool has = reel != nullptr && juce::isPositiveAndBelow (reel->activeMaster, (int) reel->masters.size());
    masterButton.setButtonText (has ? reel->masters[(size_t) reel->activeMaster].name + juce::String (juce::CharPointer_UTF8 ("  \xe2\x96\xbe"))
                                    : juce::String (juce::CharPointer_UTF8 ("NO MASTER  \xe2\x96\xbe")));
    masterButton.setEnabled (reel != nullptr);
    masterArmKey.setEnabled (reel != nullptr);
    monitorKey.setEnabled (has);
    repaint (masterLabelArea);
}

void MainComponent::showMasterMenu()
{
    if (reel == nullptr)
        return;

    const bool rec = engine.isRecording();
    juce::PopupMenu m;
    m.addSectionHeader ("MASTERS ON THIS REEL");
    for (int i = 0; i < (int) reel->masters.size(); ++i)
        m.addItem (1 + i, reel->masters[(size_t) i].name, ! rec, i == reel->activeMaster);
    if (reel->masters.empty())
        m.addItem (999, "(none yet)", false);
    m.addSeparator();
    m.addItem (1001, "New master", ! rec);
    const bool has = reel->activeMaster >= 0;
    m.addItem (1002, "Rename...", has && ! rec);
    m.addItem (1003, "Delete...", has && ! rec);

    juce::PopupMenu ex;
    ex.addItem (2000, "Whole reel...");
    for (int i = 0; i < (int) reel->markers.size(); ++i)
    {
        const auto& mk = reel->markers[(size_t) i];
        const auto to = i + 1 < (int) reel->markers.size() ? reel->markers[(size_t) i + 1].name : juce::String ("end");
        ex.addItem (2100 + i, mk.name + "  ->  " + to + "...");
    }
    m.addSeparator();
    m.addSubMenu ("Export stereo WAV", ex, has && ! rec);

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (masterButton), [this] (int r)
    {
        if (reel == nullptr || r == 0) return;
        if (r >= 1 && r <= (int) reel->masters.size()) { engine.setActiveMaster (r - 1); refreshMasterSection(); }
        else if (r == 1001) newMaster();
        else if (r == 1002) renameMaster();
        else if (r == 1003) deleteMaster();
        else if (reel->activeMaster >= 0)
        {
            const auto& name = reel->masters[(size_t) reel->activeMaster].name;
            const auto reelLen = engine.getReelLength();
            if (r == 2000)
                exportMaster (0, reelLen, (session != nullptr ? session->getName() + " - " : juce::String()) + name);
            else if (r >= 2100 && r - 2100 < (int) reel->markers.size())
            {
                const int i = r - 2100;
                const auto start = reel->markers[(size_t) i].position;
                const auto end = i + 1 < (int) reel->markers.size() ? reel->markers[(size_t) i + 1].position : reelLen;
                exportMaster (start, end, reel->markers[(size_t) i].name + " - " + name);
            }
        }
    });
}

void MainComponent::newMaster()
{
    if (reel == nullptr || engine.isRecording()) return;
    const int idx = reel->addMaster (reel->nextMasterName());
    engine.mastersChanged();
    engine.setActiveMaster (idx);
    refreshMasterSection();
}

void MainComponent::renameMaster()
{
    if (reel == nullptr || reel->activeMaster < 0) return;
    const int idx = reel->activeMaster;

    auto* w = new juce::AlertWindow ("Rename master", "Name:", juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", reel->masters[(size_t) idx].name);
    w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w, idx] (int r)
    {
        if (r != 1 || reel == nullptr || engine.isRecording()) return;
        juce::String err;
        engine.stop();
        // Files are renamed, so the playback readers must let go of them first.
        engine.setActiveMaster (-1);
        if (! reel->renameMaster (idx, w->getTextEditorContents ("name"), err) && err.isNotEmpty())
            showAlert ("Could not rename the master", err);
        engine.mastersChanged();
        engine.setActiveMaster (idx);
        refreshMasterSection();
    }), true);
}

void MainComponent::deleteMaster()
{
    if (reel == nullptr || reel->activeMaster < 0) return;
    const int idx = reel->activeMaster;
    const auto name = reel->masters[(size_t) idx].name;

    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::WarningIcon, "Delete " + name + "?",
        "The master's audio goes to the Recycle Bin. The tracks are not touched.", "Delete", "Cancel", this,
        juce::ModalCallbackFunction::create ([this, idx] (int r)
        {
            if (r != 1 || reel == nullptr || engine.isRecording()) return;
            engine.stop();
            engine.setActiveMaster (-1);
            juce::String err;
            if (! reel->removeMaster (idx, err) && err.isNotEmpty())
                showAlert ("Could not delete the master", err);
            engine.mastersChanged();
            engine.setActiveMaster (reel->masters.empty() ? -1 : 0);
            refreshMasterSection();
        }));
}

void MainComponent::exportMaster (juce::int64 start, juce::int64 end, const juce::String& suggestedName)
{
    if (reel == nullptr || reel->activeMaster < 0 || end <= start) return;

    const auto fileL = reel->getMasterFile (reel->activeMaster, 0);
    const auto fileR = reel->getMasterFile (reel->activeMaster, 1);
    if (! fileL.existsAsFile() || ! fileR.existsAsFile())
    {
        showAlert ("Nothing to export", "This master hasn't been recorded yet.");
        return;
    }

    chooser = std::make_unique<juce::FileChooser> ("Export master",
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (juce::File::createLegalFileName (suggestedName) + ".wav"),
        "*.wav");

    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this, fileL, fileR, start, end] (const juce::FileChooser& fc)
    {
        auto dest = fc.getResult();
        if (dest == juce::File()) return;
        dest = dest.withFileExtension ("wav");

        struct Job : juce::ThreadWithProgressWindow
        {
            Job (juce::File l, juce::File r, juce::File d, juce::int64 s, juce::int64 e, double sr)
                : juce::ThreadWithProgressWindow ("Exporting master...", true, true), L (l), R (r), dest (d), start (s), end (e), rate (sr) {}

            void run() override
            {
                juce::AudioFormatManager fm;
                fm.registerBasicFormats();
                std::unique_ptr<juce::AudioFormatReader> rl (fm.createReaderFor (L)), rr (fm.createReaderFor (R));
                if (rl == nullptr || rr == nullptr) { error = "Could not read the master files."; return; }

                auto tmp = dest.getSiblingFile (dest.getFileNameWithoutExtension() + ".partial.wav");
                tmp.deleteFile();
                std::unique_ptr<juce::OutputStream> os (tmp.createOutputStream().release());
                juce::WavAudioFormat wav;
                std::unique_ptr<juce::AudioFormatWriter> w (os != nullptr ? wav.createWriterFor (os.get(), rate, 2, 24, {}, 0) : nullptr);
                if (w == nullptr) { error = "Could not create " + dest.getFullPathName(); return; }
                os.release(); // owned by the writer now

                juce::AudioBuffer<float> buf (2, 65536);
                for (auto pos = start; pos < end && ! threadShouldExit();)
                {
                    const int n = (int) juce::jmin ((juce::int64) buf.getNumSamples(), end - pos);
                    buf.clear();
                    float* l = buf.getWritePointer (0);
                    float* r = buf.getWritePointer (1);
                    rl->read (&l, 1, pos, n);
                    rr->read (&r, 1, pos, n);
                    if (! w->writeFromAudioSampleBuffer (buf, 0, n)) { error = "Write failed (disk full?)"; break; }
                    pos += n;
                    setProgress ((double) (pos - start) / (double) (end - start));
                }
                w.reset();

                if (threadShouldExit() || error.isNotEmpty()) { tmp.deleteFile(); return; }
                if (! tmp.moveFileTo (dest)) error = "Could not write " + dest.getFullPathName();
            }

            void threadComplete (bool cancelled) override
            {
                if (error.isNotEmpty())
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Export failed", error);
                else if (! cancelled)
                    dest.revealToUser();
                delete this;
            }

            juce::File L, R, dest;
            juce::int64 start, end;
            double rate;
            juce::String error;
        };

        (new Job (fileL, fileR, dest, start, end, reel->sampleRate))->launchThread();
    });
}

//==============================================================================
void MainComponent::toggleFullScreen()
{
    auto& desktop = juce::Desktop::getInstance();
    auto* top = getTopLevelComponent();
    if (desktop.getKioskModeComponent() != nullptr)
        desktop.setKioskModeComponent (nullptr);
    else if (top != nullptr)
        desktop.setKioskModeComponent (top, false);

    const bool full = desktop.getKioskModeComponent() != nullptr;
    fullScreenButton.setButtonText (full ? "EXIT FULL SCREEN" : "FULL SCREEN");
    fullScreenButton.setTooltip (full ? "Leave full screen (F11 / Esc)" : "Full screen (F11)");
}

//==============================================================================
// Tempo, click, timecode

void MainComponent::showSync (SyncPanel::Page page)
{
    auto& w = page == SyncPanel::Page::tempo ? tempoWindow : timecodeWindow;
    if (w == nullptr)
    {
        w = std::make_unique<SyncWindow> (page, engine, [this] { return reel.get(); }, [this] { return syncSettings; });
        w->getPanel().onTempoChanged = [this] { tempoEdited(); };
        w->getPanel().onSettingsChanged = [this] (const SyncPanel::Settings& s) { applySyncSettings (s); };
        if (auto* dev = deviceManager.getCurrentAudioDevice())
            w->getPanel().setInputNames (dev->getInputChannelNames(), dev->getActiveInputChannels());
    }
    w->getPanel().refresh();
    w->setVisible (true);
    w->toFront (true);
}

void MainComponent::tempoEdited()
{
    if (reel == nullptr) return;
    engine.setTempo (reel->tempoBpm, reel->beatsPerBar, reel->barOne);
    pushReelSync();          // a synced drum machine follows
    saveReel();
    markers.refresh();
    repaint();
}

void MainComponent::loadSyncSettings()
{
    auto* p = props.getUserSettings();
    SyncPanel::Settings s;
    s.mtcOut = p->getValue ("mtcOut");
    s.chase = (TapeEngine::ChaseSource) juce::jlimit (0, 2, p->getIntValue ("chase", 0));
    s.ltcIn = p->getIntValue ("ltcIn", -1);
    s.mtcIn = p->getValue ("mtcIn");
    applySyncSettings (s);
}

void MainComponent::applySyncSettings (const SyncPanel::Settings& s)
{
    syncSettings = s;
    auto* p = props.getUserSettings();
    p->setValue ("mtcOut", s.mtcOut);
    p->setValue ("chase", (int) s.chase);
    p->setValue ("ltcIn", s.ltcIn);
    p->setValue ("mtcIn", s.mtcIn);
    props.saveIfNeeded();

    if (s.mtcOut.isNotEmpty()) { if (! mtcOutput.open (s.mtcOut)) showAlert ("MTC out", "Couldn't open that MIDI output."); }
    else                       mtcOutput.close();

    engine.setChase (s.chase, s.ltcIn);
    if (s.chase == TapeEngine::ChaseSource::mtc && s.mtcIn.isNotEmpty())
    {
        if (! mtcInput.open (s.mtcIn)) showAlert ("MTC in", "Couldn't open that MIDI input.");
    }
    else
        mtcInput.close();
}

//==============================================================================
// Network (Hawthorne Link)

void MainComponent::showNetwork()
{
    if (networkWindow == nullptr)
    {
        networkWindow = std::make_unique<NetworkWindow> (netRx);
        networkWindow->getPanel().onChanged = [this] { saveNetworkSettings(); updateNetworkInputs(); };
    }
    networkWindow->setVisible (true);
    networkWindow->toFront (true);
}

void MainComponent::loadNetworkSettings()
{
    auto* s = props.getUserSettings();
    netRx.setTargetLatencyMs (s->getDoubleValue ("netMinBufferMs", 3.0));
    netRx.setManualAddresses (juce::StringArray::fromLines (s->getValue ("netManual")));
    const auto names = juce::StringArray::fromLines (s->getValue ("netSlots"));
    for (int slot = 0; slot < juce::jmin (names.size(), NetReceiver::maxSlots); ++slot)
        if (names[slot].isNotEmpty())
            netRx.connect (slot, names[slot]);
}

void MainComponent::saveNetworkSettings()
{
    auto* s = props.getUserSettings();
    s->setValue ("netMinBufferMs", netRx.getTargetLatencyMs());
    s->setValue ("netManual", netRx.getManualAddresses().joinIntoString ("\n"));
    juce::StringArray names;
    for (int slot = 0; slot < NetReceiver::maxSlots; ++slot)
        names.add (netRx.getSlotName (slot));
    s->setValue ("netSlots", names.joinIntoString ("\n"));
    props.saveIfNeeded();
}

void MainComponent::updateNetworkInputs()
{
    // Network channels become inputs in the routing matrix while their sender is connected.
    std::vector<RoutingMatrix::NetInput> inputs;
    juce::String key;
    for (int slot = 0; slot < NetReceiver::maxSlots; ++slot)
    {
        const auto st = netRx.getStatus (slot);
        if (st.state == NetReceiver::State::off || st.channels <= 0)
            continue;
        const bool live = netRx.isLive (slot);
        for (int ch = 0; ch < st.channels; ++ch)
            inputs.push_back ({ NetReceiver::inputId (slot, ch),
                                "NET " + juce::String (slot + 1) + " " + st.name + " ch " + juce::String (ch + 1), slot, ch, live });
        key << slot << ":" << st.channels << (live ? "L" : "-") << st.name << "|";
    }

    if (key != netInputsKey)
    {
        netInputsKey = key;
        if (routingWindow != nullptr)
        {
            routingWindow->getMatrix().setNetworkInputs (inputs);
        }
    }
}

//==============================================================================
// Windows

void MainComponent::showRouting()
{
    if (routingWindow == nullptr)
    {
        routingWindow = std::make_unique<RoutingWindow> (engine, [this] { return reel.get(); });
        routingWindow->getMatrix().onChanged = [this] { refreshInputChoices(); routingWindow->getLatencyPanel().repaint(); };
        netInputsKey.clear();
        updateNetworkInputs();
        routingWindow->getLatencyPanel().onMeasured = [this] (int rtt)
        {
            const auto key = latencyKey();
            if (key.isEmpty()) return;
            props.getUserSettings()->setValue ("rtt_" + key, rtt);
            props.saveIfNeeded();
        };
        updateRoutingDevice();
        routingWindow->fitToMatrix();
    }

    routingWindow->setVisible (true);
    routingWindow->toFront (true);
}

void MainComponent::updateRoutingDevice()
{
    if (auto* dev = deviceManager.getCurrentAudioDevice())
        for (auto* w : { tempoWindow.get(), timecodeWindow.get() })
            if (w != nullptr) w->getPanel().setInputNames (dev->getInputChannelNames(), dev->getActiveInputChannels());

    if (routingWindow == nullptr)
        return;

    if (auto* dev = deviceManager.getCurrentAudioDevice())
    {
        routingWindow->getMatrix().setDevice (dev->getInputChannelNames(), dev->getActiveInputChannels(),
                                              dev->getOutputChannelNames(), dev->getActiveOutputChannels());
        routingWindow->getLatencyPanel().setDevice (dev->getInputChannelNames(), dev->getActiveInputChannels(),
                                                    dev->getOutputChannelNames(), dev->getActiveOutputChannels());
    }
    else
    {
        routingWindow->getMatrix().setDevice ({}, {}, {}, {});
        routingWindow->getLatencyPanel().setDevice ({}, {}, {}, {});
    }
}

void MainComponent::showSheet (bool takeLog)
{
    if (sheetWindow == nullptr)
    {
        sheetWindow = std::make_unique<SessionSheetWindow> ([this] { return reel.get(); }, [this] { return session.get(); });
        auto& sheet = sheetWindow->getSheet();
        sheet.onSave = [this] { saveReel(); saveSessionInfo(); };
        sheet.onTrackNamesChanged = [this] { refreshTrackNames(); };
    }

    sheetWindow->getSheet().refresh();
    if (takeLog)
        sheetWindow->getSheet().showTakeLog();
    sheetWindow->setVisible (true);
    sheetWindow->toFront (true);
}

//==============================================================================
// Updates

void MainComponent::refreshUpdateButton()
{
    const bool show = updater.isUpdateAvailable();
    const auto version = updater.getLatestVersion();
    updateButton.setButtonText (updater.isDownloading()
                                    ? "DOWNLOADING " + juce::String (juce::roundToInt (updater.getProgress() * 100.0f)) + "%"
                                    : "UPDATE " + version);
    updateButton.setTooltip ("Hawthorne " + version + " is available. Click to download and install it - Hawthorne restarts by itself.");
    updateButton.setEnabled (! updater.isDownloading());

    if (show != updateButton.isVisible())
    {
        updateButton.setVisible (show);
        resized();
    }
}

void MainComponent::confirmUpdate()
{
    if (engine.isRecording())
    {
        showAlert ("Update", "Stop recording first - Hawthorne restarts to install the update.");
        return;
    }

    const auto notes = updater.getReleaseNotes();
    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::NoIcon, "Hawthorne " + updater.getLatestVersion(),
        notes.isNotEmpty() ? "What's new:\n\n" + notes : juce::String ("A new version is ready."),
        "Install", "Later", this,
        juce::ModalCallbackFunction::create ([this] (int r)
        {
            if (r != 1 || engine.isRecording())
                return;

            engine.stop();          // tape is safe on disk before Hawthorne closes
            saveReel();
            updater.install();
        }));
}

void MainComponent::showAlert (const juce::String& title, const juce::String& message)
{
    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, title, message, "OK", this);
}

//==============================================================================
// Transport

void MainComponent::doPlay()
{
    if (engine.isPlaying())
        return;
    if (auto err = engine.play(); err.isNotEmpty())
        showAlert ("Cannot play", err);
}

void MainComponent::doStop()
{
    engine.stop();
}

void MainComponent::doRecord()
{
    if (updater.isDownloading())
    {
        showAlert ("Cannot record", "An update is downloading - Hawthorne will restart when it's ready.");
        return;
    }

    if (engine.isRecording())
    {
        engine.punchOut();
        return;
    }

    if (auto err = engine.record(); err.isNotEmpty())
        showAlert ("Cannot record", err);
}

void MainComponent::locate (juce::int64 pos)
{
    if (! engine.isRecording())
        engine.locate (pos);
}

void MainComponent::nudge (double seconds)
{
    locate (engine.getPosition() + (juce::int64) (seconds * sessionRate()));
}

bool MainComponent::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::F11Key
        || (k == juce::KeyPress::escapeKey && juce::Desktop::getInstance().getKioskModeComponent() != nullptr))
    {
        toggleFullScreen();
        return true;
    }

    if (library.isVisible())
        return false;

    // A text field being typed in keeps its keys.
    if (auto* f = juce::Component::getCurrentlyFocusedComponent())
        if (f != this && (dynamic_cast<juce::TextEditor*> (f) != nullptr))
            return false;

    const auto c = k.getTextCharacter();

    if (k == juce::KeyPress::spaceKey)                       { if (engine.isPlaying()) doStop(); else doPlay(); return true; }
    if (c == 'r' || c == 'R' || k.getKeyCode() == juce::KeyPress::numberPadMultiply) { doRecord(); return true; }
    if (c == 'm' || c == 'M')                                { addMarker(); return true; }
    if (c == ',' || c == '<')                                { jumpToMarker (-1); return true; }
    if (c == '.' || c == '>')                                { jumpToMarker (1); return true; }
    if (k == juce::KeyPress::returnKey || k == juce::KeyPress::homeKey) { locate (0); return true; }
    if (k == juce::KeyPress::leftKey)                        { nudge (-5.0); return true; }
    if (k == juce::KeyPress::rightKey)                       { nudge (5.0);  return true; }
    return false;
}

//==============================================================================
// UI refresh

void MainComponent::timerCallback()
{
    engine.tick();
    ++frame;

    const double nowMs = juce::Time::getMillisecondCounterHiRes();
    const float dt = lastTickMs > 0.0 ? (float) ((nowMs - lastTickMs) / 1000.0) : 1.0f / 30.0f;
    lastTickMs = nowMs;

    const bool rolling = engine.isPlaying();
    const bool rec = engine.isRecording();
    const bool flash = (frame / 12) % 2 == 0;

    if (rolling != lastRolling)
    {
        juce::Desktop::setScreenSaverEnabled (! rolling);
        lastRolling = rolling;
    }

    playKey.setLit (rolling);
    clickKey.setLit (reel != nullptr && reel->clickOn && reel->tempoBpm > 0.0);
    recKey.setLit (rec);
    stopKey.setLit (! rolling);

    for (int t = 0; t < bridge.modules.size(); ++t)
    {
        auto* m = bridge.modules[t];
        m->setArmed (engine.isArmed (t), rec, flash);
        m->getMeter().update (engine.popTrackRms (t), engine.popTrackPeak (t), dt);
    }

    masterArmKey.setLit (engine.isMasterArmed() && (! rec || flash));
    for (juce::Component* c : { (juce::Component*) &ips7Key, (juce::Component*) &ips15Key, (juce::Component*) &ips30Key,
                                (juce::Component*) &variKey, (juce::Component*) &variKnob, (juce::Component*) &variPctLabel,
                                (juce::Component*) &variStLabel, (juce::Component*) &stDownButton, (juce::Component*) &stUpButton,
                                (juce::Component*) &variResetButton })
        c->setEnabled (reel != nullptr && ! rec);       // speed is held while recording
    monitorKey.setLit (engine.isMonitoringMaster());
    masterL.update (engine.popMasterRms (0), engine.popMasterPeak (0), dt);
    masterR.update (engine.popMasterRms (1), engine.popMasterPeak (1), dt);
    markers.setPlayhead (engine.getPosition());

    if (frame % 15 == 0)
        updateNetworkInputs();

    handleTempoRequests();

    if (frame % 10 == 0)
    {
        statusText = buildStatus();
        repaint (statusArea);
        repaint (reelInfoArea);
    }

    repaint (counterArea);
}

juce::String MainComponent::buildStatus() const
{
    juce::StringArray parts;
    auto* dev = deviceManager.getCurrentAudioDevice();

    if (dev == nullptr)
        return "NO AUDIO DEVICE  -  open Audio Settings";

    const auto sr = dev->getCurrentSampleRate();
    parts.add (dev->getTypeName() + ": " + dev->getName());
    parts.add (juce::String (sr / 1000.0, 1) + " kHz");
    parts.add (juce::String (dev->getCurrentBufferSizeSamples()) + " smp");
    parts.add ("latency " + juce::String ((double) engine.getLatencySamples() / sr * 1000.0, 1) + " ms");

    if (reel != nullptr)
    {
        if (std::abs (sr - reel->sampleRate) > 0.5)
            parts.add ("!! SESSION IS " + juce::String (reel->sampleRate / 1000.0, 1) + " kHz");

        const auto secs = engine.getRecordTimeRemainingSeconds();
        const auto hrs = secs / 3600.0;
        parts.add ("disk: " + (hrs >= 1.0 ? juce::String (hrs, 1) + " h" : juce::String (secs / 60.0, 0) + " min")
                   + " @ " + juce::String (juce::jmax (1, engine.getNumArmedTracks())) + " trk");
    }

    parts.add ("disk buffer peak " + juce::String (juce::roundToInt (engine.getDiskBufferPeak() * 100.0f)) + "%");

    for (int slot = 0; slot < NetReceiver::maxSlots; ++slot)
    {
        const auto st = netRx.getStatus (slot);
        if (st.state == NetReceiver::State::off) continue;
        const bool bad = st.state == NetReceiver::State::rateMismatch || st.state == NetReceiver::State::waiting
                      || st.state == NetReceiver::State::searching;
        parts.add ((bad ? "!! " : "") + juce::String ("NET ") + juce::String (slot + 1) + " "
                   + (st.state == NetReceiver::State::locked ? "locked" : NetReceiver::describe (st.state))
                   + (st.dropouts > 0 ? " (" + juce::String (st.dropouts) + " dropouts)" : juce::String()));
    }

    if (engine.getDroppedSamples() > 0)
        parts.add ("!! DROPPED " + juce::String ((double) engine.getDroppedSamples() / sr * 1000.0, 0) + " ms");
    if (engine.getPlaybackUnderruns() > 0)
        parts.add ("playback underruns " + juce::String (engine.getPlaybackUnderruns()));

    return parts.joinIntoString ("   |   ");
}

//==============================================================================
void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (Colours::bg);

    // Header
    g.setColour (Colours::panel);
    g.fillRect (headerArea);
    g.setColour (Colours::mustard);
    g.setFont (monoFont (22.0f, true));
    drawLogoMark (g, headerArea.toFloat().withTrimmedLeft (12).withWidth (52).reduced (0, 4));
    g.setColour (Colours::mustard);
    g.drawText ("H A W T H O R N E", headerArea.withTrimmedLeft (70).withWidth (280), juce::Justification::centredLeft);

    const int firstButtonX = updateButton.isVisible() ? updateButton.getX() : libraryButton.getX();
    auto sessionArea = headerArea.withTrimmedLeft (360).withRight (firstButtonX - 16);
    g.setFont (monoFont (14.0f, true));
    g.setColour (Colours::text);
    g.drawText (reel != nullptr ? (session != nullptr ? session->getName() + "  -  " : juce::String()) + reel->getName()
                                : juce::String ("No reel on the deck"),
                sessionArea, juce::Justification::centredLeft, true);
    g.setColour (Colours::textDim);
    g.setFont (monoFont (11.0f));
    g.drawText (juce::String (reel != nullptr ? reel->numTracks : 0) + "-TRACK  v" HAWTHORNE_VERSION,
                sessionArea, juce::Justification::centredRight);

    // Transport deck
    g.setColour (Colours::panel);
    g.fillRect (deckArea);
    g.setColour (Colours::edge);
    g.drawHorizontalLine (deckArea.getY(), 0.0f, (float) getWidth());

    g.setColour (Colours::black);
    g.fillRoundedRectangle (counterArea.toFloat(), 4.0f);
    g.setColour (Colours::edge);
    g.drawRoundedRectangle (counterArea.toFloat(), 4.0f, 1.0f);

    const bool rec = engine.isRecording();
    g.setColour (rec ? Colours::red : Colours::mustard);
    {
        // top: state, chase, tempo.  middle: minutes / seconds.  bottom: bars / beats and timecode.
        auto c = counterArea.reduced (8, 3);
        auto top = c.removeFromTop (13), bottom = c.removeFromBottom (15);
        const auto pos = engine.getPosition();

        g.setFont (monoFont (juce::jmin ((float) c.getHeight() * 0.95f, (float) c.getWidth() / 7.6f), true));
        g.drawText (formatTime (pos, sessionRate()), c, juce::Justification::centred);

        g.setFont (monoFont (10.0f, true));
        g.setColour (rec ? Colours::red : (engine.isPlaying() ? Colours::mustard : Colours::textDim));
        g.drawText (rec ? "REC" : (engine.isPlaying() ? "PLAY" : "STOP"), top, juce::Justification::centredLeft);

        if (engine.getChase() != TapeEngine::ChaseSource::off)
        {
            const auto cs = engine.getChaseStatus();
            g.setColour (cs.locked ? Colours::mustard : (cs.receiving ? Colours::text : Colours::red));
            g.drawText (cs.locked ? "CHASE LOCKED" : (cs.receiving ? "CHASE" : "CHASE - NO TC"), top.withTrimmedLeft (44),
                        juce::Justification::centredLeft);
        }

        if (reel != nullptr && reel->tempoBpm > 0.0)
        {
            g.setColour (Colours::textDim);
            g.drawText (juce::String (reel->tempoBpm, 1) + " BPM  " + juce::String (reel->beatsPerBar) + "/4", top, juce::Justification::centredRight);
        }

        g.setFont (monoFont (13.0f, true));
        if (reel != nullptr)
        {
            const auto bbt = reel->bbtString (pos);
            g.setColour (Colours::mustardDim.brighter (0.5f));
            g.drawText (bbt.isNotEmpty() ? "BAR " + bbt : juce::String ("NO TEMPO"), bottom, juce::Justification::centredLeft);
            const auto rate = engine.getTimecodeRate();
            const auto tc = timecode::fromFrames ((juce::int64) std::floor (engine.timecodeFramesAt ((double) pos) + 1.0e-9), rate);
            g.setColour (Colours::textDim);
            g.drawText ("TC " + tc.toString (timecode::isDrop (rate)), bottom, juce::Justification::centredRight);
        }
    }

    if (reel != nullptr)
    {
        g.setColour (Colours::textDim);
        g.setFont (monoFont (12.0f, true));
        g.drawFittedText ("REEL  " + formatTime (engine.getReelLength(), sessionRate()).dropLastCharacters (4)
                              + "\n" + juce::String (reel->numTracks) + " TRACKS  -  "
                              + juce::String (reel->sampleRate / 1000.0, 1) + " kHz / 24-bit"
                              + "\n" + speedText(),
                          reelInfoArea, juce::Justification::centredLeft, 3);
        g.setColour (Colours::textDim);
        g.setFont (monoFont (10.0f, true));
        g.drawText ("SPEED", speedLabelArea, juce::Justification::centredLeft);
    }

    // Master section (top of the sidebar)
    g.setColour (Colours::panel);
    g.fillRect (masterPanelArea);
    g.setColour (Colours::edge);
    g.drawVerticalLine (masterPanelArea.getX(), (float) masterPanelArea.getY(), (float) masterPanelArea.getBottom());
    g.drawHorizontalLine (masterPanelArea.getBottom() - 1, (float) masterPanelArea.getX(), (float) masterPanelArea.getRight());
    g.setColour (Colours::mustard);
    g.setFont (monoFont (15.0f, true));
    g.drawText ("MASTER", masterLabelArea, juce::Justification::centredLeft);

    // Status bar
    g.setColour (Colours::black);
    g.fillRect (statusArea);
    const bool warn = statusText.contains ("!!") || statusText.startsWith ("NO AUDIO");
    g.setColour (warn ? Colours::red : Colours::textDim);
    g.setFont (monoFont (12.0f, warn));
    g.drawText (statusText, statusArea.reduced (12, 0), juce::Justification::centredLeft);
}

void MainComponent::resized()
{
    auto r = getLocalBounds();

    headerArea = r.removeFromTop (48);
    {
        auto h = headerArea.reduced (12, 9);
        fullScreenButton.setBounds (h.removeFromRight (150));
        h.removeFromRight (8);
        audioButton.setBounds (h.removeFromRight (140));
        h.removeFromRight (8);
        timecodeButton.setBounds (h.removeFromRight (100));
        h.removeFromRight (8);
        syncButton.setBounds (h.removeFromRight (76));
        h.removeFromRight (8);
        networkButton.setBounds (h.removeFromRight (100));
        h.removeFromRight (8);
        routingButton.setBounds (h.removeFromRight (96));
        h.removeFromRight (8);
        sheetButton.setBounds (h.removeFromRight (130));
        h.removeFromRight (8);
        libraryButton.setBounds (h.removeFromRight (100));

        if (updateButton.isVisible())
        {
            h.removeFromRight (16);
            updateButton.setBounds (h.removeFromRight (170));
        }
    }

    statusArea = r.removeFromBottom (24);

    // Compact transport deck
    deckArea = r.removeFromBottom (96);
    {
        auto d = deckArea.reduced (14, 9);

        const bool compact = getWidth() < 1700;
        counterArea = d.removeFromLeft (compact ? 250 : 300).reduced (0, 2);
        d.removeFromLeft (compact ? 12 : 18);

        const int kw = compact ? 56 : 64, kwBig = compact ? 62 : 72, gap = compact ? 10 : 14;
        auto key = [&] (TransportKey& k, int w)
        {
            k.setBounds (d.removeFromLeft (w).reduced (0, 6));
            d.removeFromLeft (6);
        };
        key (rtzKey, kw); key (rewKey, kw); key (ffwKey, kw);
        d.removeFromLeft (gap);
        key (stopKey, kwBig); key (playKey, kwBig); key (recKey, kwBig);
        d.removeFromLeft (gap);
        key (markKey, kw);
        key (clickKey, kw);
        d.removeFromLeft (gap);

        // Speed: IPS switch, VARI, varispeed knob and readout
        auto sp = d.removeFromLeft (compact ? 320 : 370);
        speedLabelArea = sp.removeFromTop (14);
        variKnob.setBounds (sp.removeFromRight (52).withSizeKeepingCentre (52, 52));
        sp.removeFromRight (6);
        auto row1 = sp.removeFromTop (24);
        sp.removeFromTop (4);
        const int iw = (row1.getWidth() - 3 * 4) / 4;
        for (auto* k : { &ips7Key, &ips15Key, &ips30Key, &variKey })
        {
            k->setBounds (row1.removeFromLeft (iw));
            row1.removeFromLeft (4);
        }
        // [- ST] [ % ] [ st ] [+ ST] [0]
        auto row2 = sp.removeFromTop (24);
        const int g2 = 4, w2 = row2.getWidth() - 4 * g2;
        const int wb = w2 * 17 / 100, wr = w2 * 13 / 100, wl = (w2 - 2 * wb - wr) / 2;
        stDownButton.setBounds (row2.removeFromLeft (wb));   row2.removeFromLeft (g2);
        variPctLabel.setBounds (row2.removeFromLeft (wl));   row2.removeFromLeft (g2);
        variStLabel.setBounds (row2.removeFromLeft (wl));    row2.removeFromLeft (g2);
        stUpButton.setBounds (row2.removeFromLeft (wb));     row2.removeFromLeft (g2);
        variResetButton.setBounds (row2);
        d.removeFromLeft (gap);
        reelInfoArea = d;
    }

    // Sidebar on the right: master section on top, markers below
    auto sidebar = r.removeFromRight (300);
    {
        auto ms = sidebar.removeFromTop (204);
        masterPanelArea = ms;
        ms = ms.reduced (12, 8).withTrimmedLeft (1);
        masterLabelArea = ms.removeFromTop (18);
        ms.removeFromTop (4);
        auto controls = ms.removeFromBottom (26);
        ms.removeFromBottom (6);
        masterButton.setBounds (ms.removeFromBottom (26));
        ms.removeFromBottom (8);
        const int vuW = (ms.getWidth() - 8) / 2;
        const int vuH = juce::jmin (ms.getHeight(), (int) ((float) vuW / ChannelModule::meterAspect));
        auto vus = ms.withSizeKeepingCentre (ms.getWidth(), vuH);
        masterL.setBounds (vus.removeFromLeft (vuW));
        masterR.setBounds (vus.removeFromRight (vuW));
        masterArmKey.setBounds (controls.removeFromLeft ((controls.getWidth() - 6) / 2));
        controls.removeFromLeft (6);
        monitorKey.setBounds (controls);
    }
    markers.setBounds (sidebar);

    bridgeView.setBounds (r);
    layoutBridge();

    if (library.isVisible())
        library.setBounds (getLocalBounds());
}

} // namespace hawthorne
