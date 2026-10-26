#include "SyncWindow.h"
#include "UIHelpers.h"

namespace hawthorne
{

SyncPanel::SyncPanel (Page p, TapeEngine& e, std::function<Reel*()> gr, std::function<Settings()> gs)
    : page (p), engine (e), getReel (std::move (gr)), getSettings (std::move (gs))
{
    auto setupEditor = [this] (juce::TextEditor& ed, const juce::String& tip)
    {
        ed.setFont (monoFont (15.0f, true));
        ed.setJustification (juce::Justification::centred);
        ed.setTooltip (tip);
        ed.setColour (juce::TextEditor::backgroundColourId, Colours::black);
        ed.setColour (juce::TextEditor::textColourId, Colours::mustard);
        ed.setColour (juce::TextEditor::outlineColourId, Colours::edge);
        addAndMakeVisible (ed);
    };

    // Tempo
    setupEditor (bpm, "Tempo in beats per minute (press Enter)");
    bpm.setInputRestrictions (6, "0123456789.");
    bpm.onReturnKey = [this] { applyTempo(); };
    bpm.onFocusLost = [this] { applyTempo(); };
    for (int b = 1; b <= 12; ++b) beats.addItem (juce::String (b) + " / 4", b);
    beats.onChange = [this] { if (! updating) applyTempo(); };
    beats.setTooltip ("Beats in a bar");
    barOneButton.setTooltip ("Bar 1 starts where the tape is now (e.g. after a count-in)");
    barOneButton.onClick = [this]
    {
        if (auto* r = getReel()) { r->barOne = (double) engine.getPosition(); if (onTempoChanged) onTempoChanged(); refresh(); }
    };
    tapButton.setTooltip ("Tap the tempo");
    tapButton.onClick = [this] { tap(); };
    clearTempo.setTooltip ("This reel has no tempo (no bars and beats, no click)");
    clearTempo.onClick = [this]
    {
        if (auto* r = getReel()) { r->tempoBpm = 0.0; if (onTempoChanged) onTempoChanged(); refresh(); }
    };
    for (juce::Component* c : { (juce::Component*) &beats, (juce::Component*) &barOneButton, (juce::Component*) &tapButton, (juce::Component*) &clearTempo })
        addAndMakeVisible (c);

    // Click
    clickOn.onClick = [this] { if (! updating) engine.setClick (clickOn.getToggleState(), clickLevel.getValue()); };
    clickLevel.setRange (-40.0, 0.0, 0.5);
    clickLevel.setTextValueSuffix (" dB");
    clickLevel.onValueChange = [this] { if (! updating) engine.setClick (clickOn.getToggleState(), clickLevel.getValue()); };
    addAndMakeVisible (clickOn);
    addAndMakeVisible (clickLevel);

    // Timecode
    for (int r = 0; r < 5; ++r) tcRate.addItem (timecode::name ((timecode::Rate) r) + " fps", r + 1);
    tcRate.onChange = [this] { if (! updating) { engine.setTimecodeFormat ((timecode::Rate) (tcRate.getSelectedId() - 1), engine.getTimecodeStart()); refresh(); } };
    setupEditor (tcStart, "Timecode at the start of the tape (hh:mm:ss:ff), e.g. 01:00:00:00");
    auto applyStart = [this]
    {
        timecode::Time t;
        if (timecode::parse (tcStart.getText(), t))
        {
            const auto rate = engine.getTimecodeRate();
            engine.setTimecodeFormat (rate, (double) timecode::toFrames (t, rate) / timecode::fps (rate));
        }
        refresh();
    };
    tcStart.onReturnKey = applyStart;
    tcStart.onFocusLost = applyStart;
    ltcLevel.setRange (-40.0, 0.0, 0.5);
    ltcLevel.setTextValueSuffix (" dB");
    ltcLevel.onValueChange = [this] { if (auto* r = getReel(); r != nullptr && ! updating) engine.setLtcOutputs (r->ltcOutputs, ltcLevel.getValue()); };
    mtcOut.onChange = [this] { if (! updating) applySettings(); };
    for (juce::Component* c : { (juce::Component*) &tcRate, (juce::Component*) &ltcLevel, (juce::Component*) &mtcOut })
        addAndMakeVisible (c);

    // Chase
    chase.addItem ("Off - Hawthorne runs the tape", 1);
    chase.addItem ("Chase LTC (an audio input)", 2);
    chase.addItem ("Chase MTC (a MIDI input)", 3);
    for (auto* c : { &chase, &ltcIn, &mtcIn })
    {
        c->onChange = [this] { if (! updating) applySettings(); };
        addAndMakeVisible (c);
    }

    // Each window shows only its own controls
    const bool isTempo = page == Page::tempo;
    for (juce::Component* c : { (juce::Component*) &bpm, (juce::Component*) &beats, (juce::Component*) &barOneButton,
                                (juce::Component*) &tapButton, (juce::Component*) &clearTempo, (juce::Component*) &clickOn,
                                (juce::Component*) &clickLevel })
        c->setVisible (isTempo);
    for (juce::Component* c : { (juce::Component*) &tcRate, (juce::Component*) &tcStart, (juce::Component*) &ltcLevel,
                                (juce::Component*) &mtcOut, (juce::Component*) &chase })
        c->setVisible (! isTempo);

    fillMidi();
    refresh();
    if (! isTempo) startTimerHz (10);
}

void SyncPanel::fillMidi()
{
    midiOuts = juce::MidiOutput::getAvailableDevices();
    midiIns = juce::MidiInput::getAvailableDevices();
    const auto s = getSettings();
    updating = true;
    mtcOut.clear (juce::dontSendNotification);
    mtcOut.addItem ("No MTC out", 1);
    for (int i = 0; i < midiOuts.size(); ++i) mtcOut.addItem ("MTC out to " + midiOuts[i].name, i + 2);
    mtcOut.setSelectedId (1, juce::dontSendNotification);
    for (int i = 0; i < midiOuts.size(); ++i) if (midiOuts[i].identifier == s.mtcOut) mtcOut.setSelectedId (i + 2, juce::dontSendNotification);

    mtcIn.clear (juce::dontSendNotification);
    mtcIn.addItem ("(choose a MIDI input)", 1);
    for (int i = 0; i < midiIns.size(); ++i) mtcIn.addItem (midiIns[i].name, i + 2);
    mtcIn.setSelectedId (1, juce::dontSendNotification);
    for (int i = 0; i < midiIns.size(); ++i) if (midiIns[i].identifier == s.mtcIn) mtcIn.setSelectedId (i + 2, juce::dontSendNotification);
    updating = false;
}

void SyncPanel::setInputNames (const juce::StringArray& names, const juce::BigInteger& active)
{
    inputNames = names;
    inputActive = active;
    refresh();
}

void SyncPanel::refresh()
{
    updating = true;
    auto* r = getReel();
    const auto s = getSettings();

    if (! bpm.hasKeyboardFocus (true))
        bpm.setText (r != nullptr && r->tempoBpm > 0.0 ? juce::String (r->tempoBpm, 2) : juce::String(), juce::dontSendNotification);
    beats.setSelectedId (r != nullptr ? r->beatsPerBar : 4, juce::dontSendNotification);
    clickOn.setToggleState (r != nullptr && r->clickOn, juce::dontSendNotification);
    clickLevel.setValue (r != nullptr ? r->clickLevelDb : -12.0, juce::dontSendNotification);
    ltcLevel.setValue (r != nullptr ? r->ltcLevelDb : -10.0, juce::dontSendNotification);

    const auto rate = engine.getTimecodeRate();
    tcRate.setSelectedId ((int) rate + 1, juce::dontSendNotification);
    if (! tcStart.hasKeyboardFocus (true))
    {
        const auto start = (juce::int64) std::llround (engine.getTimecodeStart() * timecode::fps (rate));
        tcStart.setText (timecode::fromFrames (start, rate).toString (timecode::isDrop (rate)), juce::dontSendNotification);
    }

    chase.setSelectedId ((int) s.chase + 1, juce::dontSendNotification);
    const int keep = s.ltcIn;
    ltcIn.clear (juce::dontSendNotification);
    ltcIn.addItem ("(choose the input LTC comes in on)", 1);
    for (int ch = 0; ch < inputNames.size(); ++ch)
        if (inputActive[ch]) ltcIn.addItem ("In " + juce::String (ch + 1) + "  " + inputNames[ch], ch + 2);
    ltcIn.setSelectedId (keep >= 0 ? keep + 2 : 1, juce::dontSendNotification);
    ltcIn.setVisible (page == Page::timecode && s.chase == TapeEngine::ChaseSource::ltc);
    mtcIn.setVisible (page == Page::timecode && s.chase == TapeEngine::ChaseSource::mtc);

    for (juce::Component* c : { (juce::Component*) &bpm, (juce::Component*) &beats, (juce::Component*) &barOneButton,
                                (juce::Component*) &tapButton, (juce::Component*) &clearTempo, (juce::Component*) &clickOn,
                                (juce::Component*) &clickLevel, (juce::Component*) &tcRate, (juce::Component*) &tcStart,
                                (juce::Component*) &ltcLevel })
        c->setEnabled (r != nullptr);
    updating = false;
    repaint();
}

void SyncPanel::applyTempo()
{
    auto* r = getReel();
    if (r == nullptr || updating) return;
    const double b = bpm.getText().getDoubleValue();
    const int bpb = juce::jmax (1, beats.getSelectedId());
    if (b >= 20.0 && b <= 400.0)
    {
        r->tempoBpm = b;
        r->beatsPerBar = bpb;
    }
    else if (bpm.getText().trim().isEmpty())
    {
        r->beatsPerBar = bpb;
    }
    if (onTempoChanged) onTempoChanged();
    refresh();
}

void SyncPanel::tap()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (! taps.isEmpty() && now - taps.getLast() > 2000.0)
        taps.clear();
    taps.add (now);
    if (taps.size() > 8) taps.remove (0);
    if (taps.size() >= 3)
    {
        const double perBeat = (taps.getLast() - taps.getFirst()) / (taps.size() - 1);
        bpm.setText (juce::String (60000.0 / perBeat, 1), juce::dontSendNotification);
        applyTempo();
    }
}

void SyncPanel::applySettings()
{
    Settings s = getSettings();
    const int mo = mtcOut.getSelectedId() - 2;
    s.mtcOut = juce::isPositiveAndBelow (mo, midiOuts.size()) ? midiOuts[mo].identifier : juce::String();
    s.chase = (TapeEngine::ChaseSource) juce::jlimit (0, 2, chase.getSelectedId() - 1);
    s.ltcIn = ltcIn.getSelectedId() >= 2 ? ltcIn.getSelectedId() - 2 : -1;
    const int mi = mtcIn.getSelectedId() - 2;
    s.mtcIn = juce::isPositiveAndBelow (mi, midiIns.size()) ? midiIns[mi].identifier : juce::String();
    if (onSettingsChanged) onSettingsChanged (s);
    refresh();
}

void SyncPanel::timerCallback()
{
    repaint (statusArea);
}

void SyncPanel::paint (juce::Graphics& g)
{
    g.fillAll (Colours::panel);

    auto heading = [&] (juce::Rectangle<int> area, const juce::String& title, const juce::String& note)
    {
        g.setColour (Colours::mustard);
        g.setFont (monoFont (14.0f, true));
        g.drawText (title, area.removeFromTop (22), juce::Justification::centredLeft);
        g.setColour (Colours::textDim);
        g.setFont (uiFont (12.0f));
        g.drawText (note, area.removeFromBottom (18), juce::Justification::centredLeft, true);
    };

    g.setFont (monoFont (11.0f, true));
    if (page == Page::tempo)
    {
        heading (tempoArea, "TEMPO", "Bars and beats show on the counter and the markers. A synced THOREAU sets this too.");
        heading (clickArea, "CLICK", "Plays while the tape rolls, on the beats of the tempo above. Choose where it goes: ROUTING > CLICK.");
        g.setFont (monoFont (11.0f, true));
        g.setColour (Colours::textDim);
        g.drawText ("BPM", bpm.getBounds().translated (0, -16).withHeight (14), juce::Justification::centredLeft);
        g.drawText ("LEVEL", clickLevel.getBounds().translated (0, -16).withHeight (14), juce::Justification::centredLeft);
        return;
    }

    heading (tcArea, "TIMECODE OUT", "LTC goes to the outputs ticked in ROUTING > LTC OUT. MTC goes to the MIDI port chosen here.");
    heading (chaseArea, "CHASE", "Hawthorne follows incoming timecode: it locates, rolls, stays in sync and stops with it.");
    g.setFont (monoFont (11.0f, true));
    g.setColour (Colours::textDim);
    g.drawText ("TAPE STARTS AT", tcStart.getBounds().translated (0, -16).withHeight (14), juce::Justification::centredLeft);
    g.drawText ("LTC LEVEL", ltcLevel.getBounds().translated (0, -16).withHeight (14), juce::Justification::centredLeft);

    // Chase status
    const auto st = engine.getChaseStatus();
    juce::String line;
    juce::Colour col = Colours::textDim;
    if (engine.getChase() == TapeEngine::ChaseSource::off)
        line = "Not chasing.";
    else if (! st.receiving)
    {
        line = "No timecode coming in.";
        col = Colours::red;
    }
    else
    {
        const auto rate = engine.getTimecodeRate();
        const auto tc = timecode::fromFrames ((juce::int64) std::floor (st.tcSeconds * timecode::fps (rate) + 1.0e-9), rate);
        line = "IN " + tc.toString (timecode::isDrop (rate)) + "   " + (st.running ? (st.locked ? "LOCKED" : "LOCKING") : "PARKED");
        if (st.running) line << "   " << juce::String (st.errorMs, 2) << " ms";
        col = st.locked ? Colours::mustard : Colours::text;
    }
    g.setColour (col);
    g.setFont (monoFont (14.0f, true));
    g.drawText (line, statusArea, juce::Justification::centredLeft);
}

void SyncPanel::resized()
{
    auto r = getLocalBounds().reduced (20, 14);

    auto section = [&r] (int h) { auto a = r.removeFromTop (h); r.removeFromTop (14); return a; };

    if (page == Page::timecode)
    {
        tcArea = section (110);
        auto a = tcArea.withTrimmedTop (42).removeFromTop (30);
        tcRate.setBounds (a.removeFromLeft (150));       a.removeFromLeft (10);
        tcStart.setBounds (a.removeFromLeft (140));      a.removeFromLeft (10);
        ltcLevel.setBounds (a.removeFromLeft (180));     a.removeFromLeft (10);
        mtcOut.setBounds (a);

        chaseArea = section (140);
        auto c = chaseArea.withTrimmedTop (28);
        auto row = c.removeFromTop (28);
        chase.setBounds (row.removeFromLeft (300));      row.removeFromLeft (10);
        ltcIn.setBounds (row);
        mtcIn.setBounds (row);
        c.removeFromTop (12);
        statusArea = c.removeFromTop (26);
        return;
    }

    tempoArea = section (110);
    {
        auto a = tempoArea.withTrimmedTop (42).removeFromTop (30);
        bpm.setBounds (a.removeFromLeft (100));          a.removeFromLeft (10);
        beats.setBounds (a.removeFromLeft (90));         a.removeFromLeft (10);
        tapButton.setBounds (a.removeFromLeft (70));     a.removeFromLeft (10);
        barOneButton.setBounds (a.removeFromLeft (170)); a.removeFromLeft (10);
        clearTempo.setBounds (a.removeFromLeft (110));
    }

    clickArea = section (100);
    {
        auto a = clickArea.withTrimmedTop (42).removeFromTop (28);
        clickOn.setBounds (a.removeFromLeft (100));      a.removeFromLeft (10);
        clickLevel.setBounds (a.removeFromLeft (300));
    }

}

} // namespace hawthorne
