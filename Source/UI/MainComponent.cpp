#include "MainComponent.h"

#ifndef HAWTHORNE_VERSION
 #define HAWTHORNE_VERSION "dev"
#endif

namespace hawthorne
{

//==============================================================================
void TransportButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const bool isRec = icon == Icon::record;

    auto fill = lit ? (isRec ? Colours::red : Colours::mustard) : Colours::panelLight;
    if (down)             fill = fill.darker (0.3f);
    else if (highlighted) fill = fill.brighter (0.1f);

    g.setColour (fill);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (Colours::edge);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);

    const auto ic = r.withSizeKeepingCentre (r.getHeight() * 0.42f, r.getHeight() * 0.42f);
    g.setColour (lit ? Colours::black : (isRec ? Colours::red : Colours::text));

    auto tri = [] (juce::Rectangle<float> b, bool right)
    {
        juce::Path p;
        if (right) p.addTriangle (b.getX(), b.getY(), b.getRight(), b.getCentreY(), b.getX(), b.getBottom());
        else       p.addTriangle (b.getRight(), b.getY(), b.getX(), b.getCentreY(), b.getRight(), b.getBottom());
        return p;
    };

    switch (icon)
    {
        case Icon::play:   g.fillPath (tri (ic, true)); break;
        case Icon::stop:   g.fillRect (ic.reduced (ic.getWidth() * 0.06f)); break;
        case Icon::record: g.fillEllipse (ic); break;
        case Icon::rewind:
            g.fillPath (tri (ic.withWidth (ic.getWidth() * 0.55f), false));
            g.fillPath (tri (ic.withTrimmedLeft (ic.getWidth() * 0.45f), false));
            break;
        case Icon::forward:
            g.fillPath (tri (ic.withWidth (ic.getWidth() * 0.55f), true));
            g.fillPath (tri (ic.withTrimmedLeft (ic.getWidth() * 0.45f), true));
            break;
        case Icon::rtz:
            g.fillRect (ic.withWidth (ic.getWidth() * 0.16f));
            g.fillPath (tri (ic.withTrimmedLeft (ic.getWidth() * 0.22f), false));
            break;
    }
}

//==============================================================================
MainComponent::MainComponent (juce::ApplicationProperties& p) : props (p)
{
    setLookAndFeel (&lnf);
    juce::LookAndFeel::setDefaultLookAndFeel (&lnf);
    setWantsKeyboardFocus (true);

    for (auto* b : { &newButton, &openButton, &audioButton })
        addAndMakeVisible (b);

    newButton.onClick   = [this] { newSession(); };
    openButton.onClick  = [this] { openSession(); };
    audioButton.onClick = [this] { showAudioSettings(); };

    for (auto* b : { &rtzButton, &rewButton, &ffwButton, &stopButton, &playButton, &recButton })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }

    rtzButton.onClick  = [this] { if (! engine.isRecording()) engine.locate (0); };
    rewButton.onClick  = [this] { nudge (-5.0); };
    ffwButton.onClick  = [this] { nudge (5.0); };
    stopButton.onClick = [this] { doStop(); };
    playButton.onClick = [this] { doPlay(); };
    recButton.onClick  = [this] { doRecord(); };

    for (int t = 0; t < kNumTracks; ++t)
    {
        auto* s = strips.add (new TrackStrip (t));

        s->onArm = [this] (int track, bool arm)
        {
            if (engine.isRecording())
                return; // arming is locked during a take
            engine.setArmed (track, arm);
        };

        s->onInputChanged = [this] (int track, int ch)
        {
            engine.setInput (track, ch);
            refreshInputChoices();
        };

        s->onRename = [this] (int track, const juce::String& name)
        {
            if (session == nullptr) return;
            session->tracks[(size_t) track].name = name;
            juce::String err;
            if (! session->save (err)) showAlert ("Could not save session", err);
        };

        addAndMakeVisible (s);
    }

    masterL.setTooltip ("Output L");
    masterR.setTooltip ("Output R");
    addAndMakeVisible (masterL);
    addAndMakeVisible (masterR);

    engine.onAlert = [this] (const juce::String& t, const juce::String& m) { showAlert (t, m); };

    initialiseAudio();
    openInitialSession();

    setSize (1400, 820);
    startTimerHz (30);
}

MainComponent::~MainComponent()
{
    shutdown();
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

    const auto err = deviceManager.initialise (kNumTracks, 2, saved.get(), true);

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

    refreshInputChoices();
}

void MainComponent::matchDeviceToSession()
{
    if (session == nullptr || deviceManager.getCurrentAudioDevice() == nullptr)
        return;

    auto setup = deviceManager.getAudioDeviceSetup();

    if (std::abs (setup.sampleRate - session->sampleRate) > 0.5)
    {
        setup.sampleRate = session->sampleRate;
        deviceManager.setAudioDeviceSetup (setup, true);
    }
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

    for (int t = 0; t < kNumTracks; ++t)
        strips[t]->setInputChoices (names, active, session != nullptr ? session->tracks[(size_t) t].input : -1);
}

void MainComponent::showAudioSettings()
{
    auto* selector = new juce::AudioDeviceSelectorComponent (deviceManager, 0, 256, 2, 256, false, false, false, false);
    selector->setSize (560, 520);

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

void MainComponent::openInitialSession()
{
    const auto last = props.getUserSettings()->getValue ("lastSession");
    juce::String err;

    if (last.isNotEmpty() && juce::File (last).existsAsFile())
        if (auto s = Session::load (juce::File (last), err))
        {
            setSession (std::move (s));
            return;
        }

    auto root = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Hawthorne");
    root.createDirectory();

    auto rate = deviceManager.getCurrentAudioDevice() != nullptr
              ? deviceManager.getCurrentAudioDevice()->getCurrentSampleRate() : 48000.0;

    auto name = "Untitled " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H%M");
    if (auto s = Session::create (root, name, rate, err))
        setSession (std::move (s));
    else
        showAlert ("Could not create a session", err);
}

void MainComponent::setSession (std::unique_ptr<Session> s)
{
    engine.setSession (nullptr);
    session = std::move (s);
    engine.setSession (session.get());

    if (session != nullptr)
    {
        props.getUserSettings()->setValue ("lastSession", session->getFile().getFullPathName());
        props.saveIfNeeded();

        for (int t = 0; t < kNumTracks; ++t)
        {
            strips[t]->setTrackName (session->tracks[(size_t) t].name);
            strips[t]->resetMeter();
        }

        matchDeviceToSession();

        if (! session->recoveryLog.isEmpty())
            showAlert ("Recovered after an unexpected shutdown",
                       session->recoveryLog.joinIntoString ("\n"));
    }

    refreshInputChoices();
    repaint();
}

void MainComponent::newSession()
{
    doStop();
    auto root = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Hawthorne");
    root.createDirectory();

    chooser = std::make_unique<juce::FileChooser> ("Name the new session", root.getChildFile ("New Session"), "*.hawthorne");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (f == juce::File())
            return;

        auto rate = deviceManager.getCurrentAudioDevice() != nullptr
                  ? deviceManager.getCurrentAudioDevice()->getCurrentSampleRate() : sessionRate();

        juce::String err;
        if (auto s = Session::create (f.getParentDirectory(), f.getFileNameWithoutExtension(), rate, err))
            setSession (std::move (s));
        else
            showAlert ("Could not create session", err);
    });
}

void MainComponent::openSession()
{
    doStop();
    auto root = session != nullptr ? session->getFolder().getParentDirectory()
                                   : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);

    chooser = std::make_unique<juce::FileChooser> ("Open session", root, "*.hawthorne");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (! f.existsAsFile())
            return;

        juce::String err;
        if (auto s = Session::load (f, err))
            setSession (std::move (s));
        else
            showAlert ("Could not open session", err);
    });
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
    if (engine.isRecording())
    {
        engine.punchOut();
        return;
    }

    if (auto err = engine.record(); err.isNotEmpty())
        showAlert ("Cannot record", err);
}

void MainComponent::nudge (double seconds)
{
    if (engine.isRecording())
        return;
    engine.locate (engine.getPosition() + (juce::int64) (seconds * sessionRate()));
}

bool MainComponent::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::spaceKey)
    {
        if (engine.isPlaying()) doStop(); else doPlay();
        return true;
    }
    if (k.getTextCharacter() == 'r' || k.getTextCharacter() == 'R' || k.getKeyCode() == juce::KeyPress::numberPadMultiply)
    {
        doRecord();
        return true;
    }
    if (k == juce::KeyPress::returnKey || k == juce::KeyPress::homeKey)
    {
        if (! engine.isRecording()) engine.locate (0);
        return true;
    }
    if (k == juce::KeyPress::leftKey)  { nudge (-5.0); return true; }
    if (k == juce::KeyPress::rightKey) { nudge (5.0);  return true; }
    return false;
}

//==============================================================================
// UI refresh

void MainComponent::timerCallback()
{
    engine.tick();
    ++frame;

    const bool rolling = engine.isPlaying();
    const bool rec = engine.isRecording();
    const bool flash = (frame / 12) % 2 == 0;

    if (rolling != lastRolling)
    {
        juce::Desktop::setScreenSaverEnabled (! rolling);
        lastRolling = rolling;
    }

    playButton.setLit (rolling);
    recButton.setLit (rec);
    stopButton.setLit (! rolling);

    for (int t = 0; t < kNumTracks; ++t)
    {
        strips[t]->setArmed (engine.isArmed (t), rec, flash);
        strips[t]->pushPeak (engine.popTrackPeak (t));
        if (frame % 15 == 0)
            strips[t]->setTapeSeconds ((int) ((double) engine.getTapeLength (t) / sessionRate()));
    }

    masterL.pushPeak (engine.popMasterPeak (0));
    masterR.pushPeak (engine.popMasterPeak (1));

    if (frame % 10 == 0)
        statusText = buildStatus();

    repaint (counterArea);
    repaint (statusArea);
    repaint (headerArea);
}

juce::String MainComponent::formatTime (juce::int64 samples) const
{
    const auto totalMs = (juce::int64) ((double) samples / sessionRate() * 1000.0);
    const int ms = (int) (totalMs % 1000);
    const int s  = (int) ((totalMs / 1000) % 60);
    const int m  = (int) ((totalMs / 60000) % 60);
    const int h  = (int) (totalMs / 3600000);
    return juce::String (h).paddedLeft ('0', 2) + ":" + juce::String (m).paddedLeft ('0', 2) + ":"
         + juce::String (s).paddedLeft ('0', 2) + "." + juce::String (ms).paddedLeft ('0', 3);
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

    if (session != nullptr)
    {
        if (std::abs (sr - session->sampleRate) > 0.5)
            parts.add ("!! SESSION IS " + juce::String (session->sampleRate / 1000.0, 1) + " kHz");

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
    g.setFont (monoFont (24.0f, true));
    g.drawText ("H A W T H O R N E", headerArea.withTrimmedLeft (18).withWidth (300), juce::Justification::centredLeft);

    g.setFont (monoFont (14.0f, true));
    g.setColour (Colours::text);
    auto sessionArea = headerArea.withTrimmedLeft (330).withTrimmedRight (420);
    g.drawText (session != nullptr ? session->getName() : "No session", sessionArea, juce::Justification::centredLeft);
    g.setColour (Colours::textDim);
    g.setFont (monoFont (11.0f));
    g.drawText ("24-TRACK  v" HAWTHORNE_VERSION, sessionArea, juce::Justification::centredRight);

    // Transport
    g.setColour (Colours::panel);
    g.fillRect (transportArea);

    // Counter
    g.setColour (Colours::black);
    g.fillRoundedRectangle (counterArea.toFloat(), 5.0f);
    g.setColour (Colours::edge);
    g.drawRoundedRectangle (counterArea.toFloat(), 5.0f, 1.0f);

    const bool rec = engine.isRecording();
    g.setColour (rec ? Colours::red : Colours::mustard);
    g.setFont (monoFont ((float) counterArea.getHeight() * 0.6f, true));
    g.drawText (formatTime (engine.getPosition()), counterArea.reduced (14, 0), juce::Justification::centred);

    g.setFont (monoFont (11.0f, true));
    g.setColour (rec ? Colours::red : (engine.isPlaying() ? Colours::mustard : Colours::textDim));
    g.drawText (rec ? "RECORDING" : (engine.isPlaying() ? "PLAY" : "STOP"),
                counterArea.reduced (10, 4), juce::Justification::topLeft);

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

    headerArea = r.removeFromTop (52);
    {
        auto h = headerArea.reduced (12, 11);
        audioButton.setBounds (h.removeFromRight (150));
        h.removeFromRight (8);
        openButton.setBounds (h.removeFromRight (90));
        h.removeFromRight (8);
        newButton.setBounds (h.removeFromRight (90));
    }

    statusArea = r.removeFromBottom (26);

    transportArea = r.removeFromTop (96);
    {
        auto t = transportArea.reduced (14, 12);
        counterArea = t.removeFromLeft (340);
        t.removeFromLeft (24);

        const int bw = 64;
        for (auto* b : { &rtzButton, &rewButton, &ffwButton })
        {
            b->setBounds (t.removeFromLeft (bw).reduced (0, 4));
            t.removeFromLeft (6);
        }
        t.removeFromLeft (18);
        for (auto* b : { &stopButton, &playButton, &recButton })
        {
            b->setBounds (t.removeFromLeft (bw + 12));
            t.removeFromLeft (8);
        }

        auto meters = t.removeFromRight (40);
        masterL.setBounds (meters.removeFromLeft (16));
        meters.removeFromLeft (6);
        masterR.setBounds (meters.removeFromLeft (16));
    }

    auto tracks = r.reduced (8, 8);
    const float w = (float) tracks.getWidth() / (float) kNumTracks;

    for (int t = 0; t < kNumTracks; ++t)
    {
        const int x0 = tracks.getX() + juce::roundToInt (w * (float) t);
        const int x1 = tracks.getX() + juce::roundToInt (w * (float) (t + 1));
        strips[t]->setBounds (x0, tracks.getY(), x1 - x0, tracks.getHeight());
    }
}

} // namespace hawthorne
