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

    for (auto* b : { &libraryButton, &sheetButton, &routingButton, &audioButton })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }
    libraryButton.onClick = [this] { showLibrary (true); };
    sheetButton.onClick   = [this] { showSheet(); };
    routingButton.onClick = [this] { showRouting(); };
    audioButton.onClick   = [this] { showAudioSettings(); };

    for (auto* k : { &rtzKey, &rewKey, &ffwKey, &stopKey, &playKey, &recKey, &markKey })
        addAndMakeVisible (k);
    rtzKey.onClick  = [this] { locate (0); };
    rewKey.onClick  = [this] { nudge (-5.0); };
    ffwKey.onClick  = [this] { nudge (5.0); };
    stopKey.onClick = [this] { doStop(); };
    playKey.onClick = [this] { doPlay(); };
    recKey.onClick  = [this] { doRecord(); };
    markKey.onClick = [this] { addMarker(); };

    bridgeView.setViewedComponent (&bridge, false);
    bridgeView.setWantsKeyboardFocus (false);
    bridgeView.setScrollBarsShown (true, false);
    bridgeView.setScrollBarThickness (10);
    addAndMakeVisible (bridgeView);

    masterL.setTooltip ("Master L");
    masterR.setTooltip ("Master R");
    addAndMakeVisible (masterL);
    addAndMakeVisible (masterR);

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
    library.getLoadedReel = [this] { return reel != nullptr ? reel->getFile() : juce::File(); };
    library.getSampleRate = [this]
    {
        auto* dev = deviceManager.getCurrentAudioDevice();
        return dev != nullptr ? dev->getCurrentSampleRate() : 48000.0;
    };
    addChildComponent (library);

    initialiseAudio();
    showLibrary (true);   // the tape library is the first thing you see

    setSize (1600, 900);
    startTimerHz (30);
}

MainComponent::~MainComponent()
{
    shutdown();
    sheetWindow = nullptr;
    routingWindow = nullptr;
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

    updateRoutingDevice();
    refreshInputChoices();
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

    rebuildModules();

    if (routingWindow != nullptr) routingWindow->getMatrix().tracksChanged();
    if (sheetWindow != nullptr)   sheetWindow->getSheet().refresh();
    repaint();
}

void MainComponent::saveReel()
{
    juce::String err;
    if (reel != nullptr && ! reel->save (err))
        showAlert ("Could not save the reel", err);
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
        m->onInputChanged = [this] (int track, int ch)
        {
            engine.setInput (track, ch);
            refreshInputChoices();
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
    juce::StringArray names;
    juce::BigInteger active;

    if (auto* dev = deviceManager.getCurrentAudioDevice())
    {
        names  = dev->getInputChannelNames();
        active = dev->getActiveInputChannels();
    }

    for (int t = 0; t < bridge.modules.size(); ++t)
        bridge.modules[t]->setInputChoices (names, active, reel != nullptr ? reel->tracks[(size_t) t].input : -1);

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
// Windows

void MainComponent::showRouting()
{
    if (routingWindow == nullptr)
    {
        routingWindow = std::make_unique<RoutingWindow> (engine, [this] { return reel.get(); });
        routingWindow->getMatrix().onChanged = [this] { refreshInputChoices(); };
        updateRoutingDevice();
        routingWindow->fitToMatrix();
    }

    routingWindow->setVisible (true);
    routingWindow->toFront (true);
}

void MainComponent::updateRoutingDevice()
{
    if (routingWindow == nullptr)
        return;

    if (auto* dev = deviceManager.getCurrentAudioDevice())
        routingWindow->getMatrix().setDevice (dev->getInputChannelNames(), dev->getActiveInputChannels(),
                                              dev->getOutputChannelNames(), dev->getActiveOutputChannels());
    else
        routingWindow->getMatrix().setDevice ({}, {}, {}, {});
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
    recKey.setLit (rec);
    stopKey.setLit (! rolling);

    for (int t = 0; t < bridge.modules.size(); ++t)
    {
        auto* m = bridge.modules[t];
        m->setArmed (engine.isArmed (t), rec, flash);
        m->getMeter().update (engine.popTrackRms (t), engine.popTrackPeak (t), dt);
    }

    masterL.update (engine.popMasterRms (0), engine.popMasterPeak (0), dt);
    masterR.update (engine.popMasterRms (1), engine.popMasterPeak (1), dt);
    markers.setPlayhead (engine.getPosition());

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
    g.drawText ("H A W T H O R N E", headerArea.withTrimmedLeft (18).withWidth (280), juce::Justification::centredLeft);

    const int firstButtonX = updateButton.isVisible() ? updateButton.getX() : libraryButton.getX();
    auto sessionArea = headerArea.withTrimmedLeft (310).withRight (firstButtonX - 16);
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
    g.setFont (monoFont ((float) counterArea.getHeight() * 0.58f, true));
    g.drawText (formatTime (engine.getPosition(), sessionRate()), counterArea.withTrimmedLeft (12), juce::Justification::centred);

    g.setFont (monoFont (10.0f, true));
    g.setColour (rec ? Colours::red : (engine.isPlaying() ? Colours::mustard : Colours::textDim));
    g.drawText (rec ? "REC" : (engine.isPlaying() ? "PLAY" : "STOP"), counterArea.reduced (7, 4), juce::Justification::topLeft);

    if (reel != nullptr)
    {
        g.setColour (Colours::textDim);
        g.setFont (monoFont (12.0f, true));
        g.drawFittedText ("REEL  " + formatTime (engine.getReelLength(), sessionRate()).dropLastCharacters (4)
                              + "\n" + juce::String (reel->numTracks) + " TRACKS  -  "
                              + juce::String (reel->sampleRate / 1000.0, 1) + " kHz / 24-bit",
                          reelInfoArea, juce::Justification::centredLeft, 2);
    }

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
        audioButton.setBounds (h.removeFromRight (140));
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
    deckArea = r.removeFromBottom (84);
    {
        auto d = deckArea.reduced (14, 9);

        // master VUs on the right
        const int vuH = d.getHeight();
        const int vuW = (int) ((float) vuH * ChannelModule::meterAspect);
        masterR.setBounds (d.removeFromRight (vuW));
        d.removeFromRight (6);
        masterL.setBounds (d.removeFromRight (vuW));
        d.removeFromRight (20);

        counterArea = d.removeFromLeft (250).reduced (0, 4);
        d.removeFromLeft (18);

        auto key = [&] (TransportKey& k, int w = 64)
        {
            k.setBounds (d.removeFromLeft (w).reduced (0, 6));
            d.removeFromLeft (6);
        };
        key (rtzKey); key (rewKey); key (ffwKey);
        d.removeFromLeft (14);
        key (stopKey, 72); key (playKey, 72); key (recKey, 72);
        d.removeFromLeft (14);
        key (markKey);
        d.removeFromLeft (14);
        reelInfoArea = d;
    }

    // Markers sidebar on the right
    markers.setBounds (r.removeFromRight (300));

    bridgeView.setBounds (r);
    layoutBridge();

    if (library.isVisible())
        library.setBounds (getLocalBounds());
}

} // namespace hawthorne
