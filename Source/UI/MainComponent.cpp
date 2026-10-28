#include "MainComponent.h"
#include "AlignmentPanel.h"
#include "UIHelpers.h"
#include "TapeChoice.h"
#include "MachineSkin.h"
#include "MachinePicker.h"
#include "../Engine/TrackTransfer.h"
#include "BinaryData.h"

#ifndef HAWTHORNE_VERSION
 #define HAWTHORNE_VERSION "dev"
#endif

namespace hawthorne
{

//==============================================================================
void TransportKey::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    // Keys look like the machine's (MACHINE): rubber keys with LEDs, cream keys with
    // coloured stripes, bakelite push buttons, illuminated keys, chrome-edged keys.
    const auto& sk = MachineSkin::current();
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const bool isRec = getButtonText() == "REC";
    const auto label = getButtonText();
    auto text = [&] (juce::Colour c, juce::Rectangle<float> area, float size = 13.0f)
    {
        g.setColour (c);
        g.setFont (uiFont (size, true));
        g.drawText (label, area, juce::Justification::centred);
    };
    auto pressed = [&] (float radius)
    {
        if (down)             { g.setColour (juce::Colour (0x40000000)); g.fillRoundedRectangle (r, radius); }
        else if (highlighted) { g.setColour (juce::Colour (0x14ffffff)); g.fillRoundedRectangle (r, radius); }
    };

    switch (sk.keys)
    {
        case MachineSkin::Keys::cream:
        {
            juce::ColourGradient body (sk.key.brighter (0.08f), r.getX(), r.getY(), sk.key.darker (0.12f), r.getX(), r.getBottom(), false);
            g.setGradientFill (body);
            g.fillRoundedRectangle (r, 2.0f);
            pressed (2.0f);
            g.setColour (sk.key.darker (0.5f));
            g.drawRoundedRectangle (r, 2.0f, 1.0f);
            auto stripe = r.removeFromBottom (6.0f).reduced (3.0f, 1.0f);
            g.setColour (lit ? lamp : lamp.withSaturation (0.35f).withBrightness (0.75f));
            g.fillRect (stripe);
            if (lit) { g.setColour (lamp.withAlpha (0.25f)); g.fillRect (stripe.expanded (2.0f, 2.0f)); }
            text (isRec ? juce::Colour (0xffb02a1e) : sk.keyText, r);
            break;
        }
        case MachineSkin::Keys::bakelite:
        {
            const float rad = juce::jmin (r.getHeight(), r.getWidth()) * 0.22f;
            juce::ColourGradient body (sk.key.brighter (0.35f), r.getCentreX(), r.getY(), sk.key, r.getCentreX(), r.getBottom(), false);
            g.setGradientFill (body);
            g.fillRoundedRectangle (r, rad);
            g.setColour (juce::Colour (0x22ffffff));
            g.fillRoundedRectangle (r.withHeight (r.getHeight() * 0.4f).reduced (3.0f, 2.0f), rad * 0.7f);
            pressed (rad);
            g.setColour (juce::Colours::black);
            g.drawRoundedRectangle (r, rad, 1.2f);
            // jewel lamp
            auto j = juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ r.getCentreX(), r.getY() + 7.0f });
            if (lit) { g.setColour (lamp.withAlpha (0.35f)); g.fillEllipse (j.expanded (3.0f)); }
            g.setColour (lit ? lamp : lamp.darker (2.6f));
            g.fillEllipse (j);
            text (isRec ? juce::Colour (0xffe0503c) : sk.keyText, r.withTrimmedTop (10.0f), 12.0f);
            break;
        }
        case MachineSkin::Keys::illuminated:
        {
            const juce::Colour base = lit ? lamp.withMultipliedBrightness (0.95f) : sk.key;
            juce::ColourGradient body (base.brighter (lit ? 0.25f : 0.12f), r.getX(), r.getY(), base.darker (0.3f), r.getX(), r.getBottom(), false);
            g.setGradientFill (body);
            g.fillRoundedRectangle (r, 3.0f);
            if (lit) { g.setColour (lamp.withAlpha (0.25f)); g.drawRoundedRectangle (r.expanded (1.0f), 4.0f, 2.0f); }
            pressed (3.0f);
            g.setColour (juce::Colours::black);
            g.drawRoundedRectangle (r, 3.0f, 1.0f);
            text (lit ? juce::Colour (0xff141414) : (isRec ? juce::Colour (0xffe0503c) : sk.keyText), r);
            break;
        }
        case MachineSkin::Keys::chrome:
        {
            juce::ColourGradient bevel (juce::Colour (0xffe6e6e2), r.getX(), r.getY(), juce::Colour (0xff6c6c68), r.getX(), r.getBottom(), false);
            g.setGradientFill (bevel);
            g.fillRoundedRectangle (r, 3.0f);
            auto in = r.reduced (2.5f);
            juce::ColourGradient body (sk.key.brighter (0.2f), in.getX(), in.getY(), sk.key, in.getX(), in.getBottom(), false);
            g.setGradientFill (body);
            g.fillRoundedRectangle (in, 2.0f);
            pressed (3.0f);
            auto lampRect = in.removeFromTop (5.0f).reduced (6.0f, 1.0f);
            if (lit) { g.setColour (lamp.withAlpha (0.35f)); g.fillRoundedRectangle (lampRect.expanded (2.0f, 2.0f), 2.0f); }
            g.setColour (lit ? lamp : lamp.darker (2.4f));
            g.fillRoundedRectangle (lampRect, 1.5f);
            text (isRec ? juce::Colour (0xffe0503c) : sk.keyText, in);
            break;
        }
        case MachineSkin::Keys::coloured:
        {
            // MCI-style colour-coded caps
            const auto cap = isRec ? juce::Colour (0xffc8322a)
                           : label == "PLAY" ? juce::Colour (0xff2f8f4e)
                           : label == "STOP" ? juce::Colour (0xffe9e6dc)
                           : (label == "<<" || label == ">>") ? juce::Colour (0xff2f5f9e)
                           : label == "RTZ" ? juce::Colour (0xffd98a2b)
                           : sk.key;
            auto capR = r.reduced (2.0f);
            juce::ColourGradient body (cap.brighter (0.2f), capR.getX(), capR.getY(), cap.darker (0.25f), capR.getX(), capR.getBottom(), false);
            g.setColour (juce::Colour (0xff1b1916));
            g.fillRoundedRectangle (r, 3.0f);
            g.setGradientFill (body);
            g.fillRoundedRectangle (capR, 2.0f);
            pressed (2.0f);
            if (lit)
            {
                g.setColour (cap.brighter (0.6f).withAlpha (0.5f));
                g.drawRoundedRectangle (capR.expanded (1.0f), 3.0f, 2.0f);
                g.setColour (juce::Colours::white.withAlpha (0.18f));
                g.fillRoundedRectangle (capR, 2.0f);
            }
            const bool darkCap = cap.getPerceivedBrightness() < 0.55f;
            text (darkCap ? juce::Colours::white : juce::Colour (0xff1b1916), capR);
            break;
        }
        case MachineSkin::Keys::ivory:
        {
            // J37: ivory push keys, the record key orange, an engaged key lit amber around its edge
            const auto cap = isRec ? juce::Colour (0xffd9682c) : sk.key;
            juce::ColourGradient body (cap.brighter (0.1f), r.getX(), r.getY(), cap.darker (0.15f), r.getX(), r.getBottom(), false);
            g.setGradientFill (body);
            g.fillRoundedRectangle (r, 2.0f);
            pressed (2.0f);
            g.setColour (lit ? lamp.brighter (0.3f) : juce::Colour (0xff3a3b38));
            g.drawRoundedRectangle (r.reduced (0.5f), 2.0f, lit ? 2.5f : 1.0f);
            if (lit) { g.setColour (lamp.withAlpha (0.3f)); g.drawRoundedRectangle (r.expanded (1.5f), 3.0f, 2.0f); }
            text (sk.keyText, r);
            break;
        }
        case MachineSkin::Keys::square:
        default:
        {
            juce::ColourGradient body (sk.key.brighter (0.15f), r.getX(), r.getY(), sk.key.darker (0.25f), r.getX(), r.getBottom(), false);
            g.setGradientFill (body);
            g.fillRoundedRectangle (r, 3.0f);
            pressed (3.0f);
            g.setColour (juce::Colours::black);
            g.drawRoundedRectangle (r, 3.0f, 1.0f);
            // LED in the key
            auto led = juce::Rectangle<float> (12.0f, 4.0f).withCentre ({ r.getCentreX(), r.getY() + 6.0f });
            if (lit) { g.setColour (lamp.withAlpha (0.35f)); g.fillRoundedRectangle (led.expanded (2.5f, 2.0f), 2.0f); }
            g.setColour (lit ? lamp : lamp.darker (2.4f));
            g.fillRoundedRectangle (led, 1.5f);
            text (isRec ? juce::Colour (0xffe0503c) : sk.keyText, r.withTrimmedTop (5.0f));
            break;
        }
    }
}

//==============================================================================
int MeterBridge::layoutForWidth (int width, int minHeight)
{
    const int pad = 10, gap = 8;
    const float moduleW = ((float) (width - 2 * pad) - (float) (perRow - 1) * gap) / (float) perRow;
    const int meterH = (int) (moduleW / ChannelModule::meterAspect);
    const int moduleH = meterH + ChannelModule::controlsHeight + (nrStrip ? ChannelModule::nrHeight : 0) + 4;
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
    const auto& sk = MachineSkin::current();
    g.fillAll (sk.bridge);
    for (auto& p : rowPanels)
    {
        g.setColour (sk.bridgeRow);
        g.fillRoundedRectangle (p.toFloat(), 4.0f);
        g.setColour (sk.bridgeEdge);
        g.drawRoundedRectangle (p.toFloat().reduced (0.5f), 4.0f, 1.0f);
    }
}

//==============================================================================
TempoBox::TempoBox()
{
    tapButton.setWantsKeyboardFocus (false);
    tapButton.setTooltip ("Tap the tempo");
    tapButton.onClick = [this]
    {
        const double now = juce::Time::getMillisecondCounterHiRes();
        if (! taps.isEmpty() && now - taps.getLast() > 2000.0) taps.clear();
        taps.add (now);
        if (taps.size() > 8) taps.remove (0);
        if (taps.size() >= 3)
            setBpm (60000.0 / ((taps.getLast() - taps.getFirst()) / (taps.size() - 1)), true);
    };
    addAndMakeVisible (tapButton);

    editor.setFont (monoFont (16.0f, true));
    editor.setJustification (juce::Justification::centred);
    editor.setInputRestrictions (6, "0123456789.");
    editor.setColour (juce::TextEditor::backgroundColourId, Colours::black);
    editor.setColour (juce::TextEditor::textColourId, Colours::mustard);
    auto commit = [this]
    {
        if (! editor.isVisible()) return;
        const double v = editor.getText().getDoubleValue();
        editor.setVisible (false);
        if (v >= 20.0 && v <= 400.0) setBpm (v, true);
    };
    editor.onReturnKey = commit;
    editor.onFocusLost = commit;
    editor.onEscapeKey = [this] { editor.setVisible (false); };
    addChildComponent (editor);
    setTooltip ("Tempo: drag up / down (shift = fine), double-click to type. Click the 4/4 to change the bar.");
}

void TempoBox::setBpm (double bpm, bool save)
{
    auto* r = getReel ? getReel() : nullptr;
    if (r == nullptr) return;
    r->tempoBpm = juce::jlimit (20.0, 400.0, std::round (bpm * 100.0) / 100.0);
    if (onChanged) onChanged (save);
    repaint();
}

void TempoBox::paint (juce::Graphics& g)
{
    auto* r = getReel ? getReel() : nullptr;
    const bool has = r != nullptr && r->tempoBpm > 0.0;

    const auto& sk = MachineSkin::current();
    for (auto a : { bpmArea, sigArea })
    {
        g.setColour (sk.counterBg);
        g.fillRoundedRectangle (a.toFloat(), 3.0f);
        g.setColour (sk.deckEdge);
        g.drawRoundedRectangle (a.toFloat(), 3.0f, 1.0f);
    }

    g.setColour (has ? sk.counterInk : sk.counterInk.withAlpha (0.45f));
    g.setFont (monoFont (16.0f, true));
    auto b = bpmArea.reduced (6, 0);
    g.drawText (has ? juce::String (r->tempoBpm, 2) : juce::String ("---.--"), b, juce::Justification::centredLeft);
    g.setFont (monoFont (9.0f, true));
    g.setColour (sk.counterInk.withAlpha (0.5f));
    g.drawText ("BPM", b, juce::Justification::centredRight);

    g.setColour (has ? sk.counterInk : sk.counterInk.withAlpha (0.45f));
    g.setFont (monoFont (15.0f, true));
    g.drawText (juce::String (r != nullptr ? r->beatsPerBar : 4) + "/4", sigArea, juce::Justification::centred);
}

void TempoBox::resized()
{
    auto a = getLocalBounds();
    tapButton.setBounds (a.removeFromRight (40));
    a.removeFromRight (4);
    sigArea = a.removeFromRight (44);
    a.removeFromRight (4);
    bpmArea = a;
    editor.setBounds (bpmArea);
}

void TempoBox::mouseDown (const juce::MouseEvent& e)
{
    auto* r = getReel ? getReel() : nullptr;
    if (r == nullptr) return;

    if (sigArea.contains (e.getPosition()))
    {
        juce::PopupMenu m;
        for (int b = 1; b <= 12; ++b) m.addItem (b, juce::String (b) + "/4", true, b == r->beatsPerBar);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this] (int b)
        {
            if (auto* rr = getReel ? getReel() : nullptr; rr != nullptr && b > 0)
            {
                rr->beatsPerBar = b;
                if (rr->tempoBpm <= 0.0) rr->tempoBpm = 120.0;
                if (onChanged) onChanged (true);
                repaint();
            }
        });
        return;
    }

    if (bpmArea.contains (e.getPosition()))
    {
        dragStart = r->tempoBpm > 0.0 ? r->tempoBpm : 120.0;
        dragging = true;
    }
}

void TempoBox::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging || e.getDistanceFromDragStartY() == 0) return;
    const double step = e.mods.isShiftDown() ? 0.01 : 0.25;
    setBpm (dragStart - e.getDistanceFromDragStartY() * step, false);
}

void TempoBox::mouseUp (const juce::MouseEvent& e)
{
    if (dragging && e.getDistanceFromDragStartY() != 0 && onChanged)
        onChanged (true);     // save once, at the end of the drag
    dragging = false;
}

void TempoBox::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! bpmArea.contains (e.getPosition())) return;
    auto* r = getReel ? getReel() : nullptr;
    if (r == nullptr) return;
    editor.setText (r->tempoBpm > 0.0 ? juce::String (r->tempoBpm, 2) : juce::String ("120"), juce::dontSendNotification);
    editor.setVisible (true);
    editor.grabKeyboardFocus();
    editor.selectAll();
}

//==============================================================================
MainComponent::MainComponent (juce::ApplicationProperties& p) : props (p)
{
    setLookAndFeel (&lnf);
    juce::LookAndFeel::setDefaultLookAndFeel (&lnf);
    setWantsKeyboardFocus (true);

    for (auto* b : { &libraryButton, &sheetButton, &routingButton, &networkButton, &syncButton, &timecodeButton, &audioButton, &machineButton, &helpButton })
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
    machineButton.onClick = [this] { showMachineMenu(); };
    helpButton.onClick = [this] { showManual(); };
    helpButton.setTooltip ("The Hawthorne manual (F1)");
    machineButton.setTooltip ("Tape machine models: the multitrack, and the 2-track for the master");
    timecodeButton.setTooltip ("Timecode out (LTC / MTC) and chase");
    fullScreenButton.onClick = [this] { toggleFullScreen(); };
    fullScreenButton.setTooltip ("Full screen (F11)");
    fullScreenButton.setWantsKeyboardFocus (false);
    addAndMakeVisible (fullScreenButton);
    networkButton.setTooltip ("Hawthorne Link: record audio sent from other PCs");
    audioButton.onClick   = [this] { showAudioSettings(); };

    for (auto* k : { &rtzKey, &rewKey, &ffwKey, &liftKey, &stopKey, &playKey, &recKey, &markKey, &clickKey })
        addAndMakeVisible (k);
    rtzKey.onClick  = [this] { locate (0); };
    rewKey.onClick  = [this] { nudge (-5.0); };
    ffwKey.onClick  = [this] { nudge (5.0); };
    stopKey.onClick = [this] { doStop(); };
    playKey.onClick = [this] { doPlay(); };
    recKey.onClick  = [this] { doRecord(); };
    markKey.onClick = [this] { addMarker(); };

    // Autolocator
    for (auto* k : { &inKey, &outKey, &autoKey, &rehKey, &loopKey })
        addAndMakeVisible (k);
    inKey.onClick   = [this] { setPunchHere (true); };
    outKey.onClick  = [this] { setPunchHere (false); };
    autoKey.onClick = [this] { if (! engine.isRecording()) engine.setAutoPunch (! engine.getAutoPunch()); };
    rehKey.onClick  = [this] { if (! engine.isRecording()) engine.setRehearse (! engine.getRehearse()); };
    loopKey.onClick = [this] { engine.setCycle (! engine.getCycle()); };
    liftKey.onClick = [this]
    {
        const bool on = ! engine.getLifterDefeat();
        engine.setLifterDefeat (on);
        props.getUserSettings()->setValue ("lifterDefeat", on);
        props.saveIfNeeded();
    };

    // Tempo and sync in the transport
    tempoBox.getReel = [this] { return reel.get(); };
    tempoBox.onChanged = [this] (bool save)
    {
        if (reel == nullptr) return;
        engine.setTempo (reel->tempoBpm, reel->beatsPerBar, reel->barOne);
        if (save) tempoEdited();
        for (auto* w : { tempoWindow.get(), timecodeWindow.get() }) if (w != nullptr) w->getPanel().refresh();
        markers.refresh();
        repaint (counterArea);
    };
    addAndMakeVisible (tempoBox);
    syncSourceButton.setWantsKeyboardFocus (false);
    syncSourceButton.setTooltip ("Sync: INTERNAL (Hawthorne runs the tape) or EXTERNAL (chase incoming LTC / MTC)");
    syncSourceButton.onClick = [this] { showSyncMenu(); };
    addAndMakeVisible (syncSourceButton);
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

    masterL.setMaster (true);
    masterR.setMaster (true);
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
        if (reel->activeMaster < 0)
        {
            newMaster ([this] { engine.setMasterArmed (true); refreshMasterSection(); });
            return;
        }
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

    // (development: open a reel straight away, for screenshots)
    if (auto open = juce::SystemStats::getEnvironmentVariable ("HAWTHORNE_OPEN_REEL", {}); open.isNotEmpty())
        juce::MessageManager::callAsync ([this, open]
        {
            loadReel (juce::File (open));
            if (juce::SystemStats::getEnvironmentVariable ("HAWTHORNE_OPEN_ALIGN", {}).isNotEmpty()) showAlignment();
        });

    counterMode = juce::jlimit (0, 2, props.getUserSettings()->getIntValue ("counterMode", 0));
    engine.setWindPhysics (props.getUserSettings()->getBoolValue ("windPhysics", true));
    MachineSkin::meterChoice() = juce::jlimit (0, 2, props.getUserSettings()->getIntValue ("meterStyle", 0));
    engine.setLifterDefeat (props.getUserSettings()->getBoolValue ("lifterDefeat", false));
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

namespace
{
    /** Settings: the audio device, plus transport options. */
    struct SettingsPanel : juce::Component
    {
        SettingsPanel (juce::AudioDeviceManager& dm, TapeEngine& e, juce::PropertiesFile& p, std::function<void()> metersChanged)
            : selector (dm, 0, 256, 2, 256, false, false, false, false), engine (e), props (p), onMeters (std::move (metersChanged))
        {
            meters.addItemList ({ "Auto - as the machine had them (VU, or PPM on the EMI machines)", "VU", "PPM" }, 1);
            meters.setSelectedItemIndex (MachineSkin::meterChoice(), juce::dontSendNotification);
            meters.onChange = [this]
            {
                MachineSkin::meterChoice() = meters.getSelectedItemIndex();
                props.setValue ("meterStyle", MachineSkin::meterChoice());
                props.saveIfNeeded();
                if (onMeters) onMeters();
            };
            addAndMakeVisible (meters);

            routeButton.onClick = [this]
            {
                const bool installed = RouteHost::isDriverInstalled();
                const auto err = installed ? RouteHost::uninstallDriver() : RouteHost::installDriver();
                if (err.isNotEmpty())
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Hawthorne Route", err);
                else if (! installed)
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Hawthorne Route",
                        "Installed. In the other program (Reaper...), choose \"Hawthorne Route\" as the ASIO device.\n"
                        "Its outputs 1-32 arrive here as inputs ROUTE 1-32 - patch them in ROUTING. Its inputs 1-2 are Hawthorne's master.");
                refreshRoute();
            };
            addAndMakeVisible (routeButton);
            refreshRoute();
            addAndMakeVisible (selector);
            windToggle.setToggleState (engine.getWindPhysics(), juce::dontSendNotification);
            windToggle.setTooltip ("On: locating (RTZ, markers, REW / FFW) spools the tape there like a real machine - "
                                   "it speeds up, brakes, overshoots a touch and creeps back.\nOff: jumps there instantly.");
            windToggle.onClick = [this]
            {
                engine.setWindPhysics (windToggle.getToggleState());
                props.setValue ("windPhysics", windToggle.getToggleState());
                props.saveIfNeeded();
            };
            addAndMakeVisible (windToggle);

            for (auto* sl : { &preRoll, &postRoll })
            {
                sl->setRange (0.0, 10.0, 0.5);
                sl->setTextValueSuffix (" s");
                sl->setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 20);
                sl->onValueChange = [this] { engine.setRolls (preRoll.getValue(), postRoll.getValue()); };
                addAndMakeVisible (sl);
            }
            preRoll.setValue (engine.getPreRoll(), juce::dontSendNotification);
            postRoll.setValue (engine.getPostRoll(), juce::dontSendNotification);
            setSize (560, 780);
        }
        void paint (juce::Graphics& g) override
        {
            g.setColour (Colours::textDim);
            g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            g.drawText ("TRANSPORT", 16, getHeight() - 192, 200, 16, juce::Justification::centredLeft);
            g.setFont (juce::FontOptions (13.0f));
            g.drawText ("Pre-roll (auto punch)", preRoll.getBounds().withX (16).withWidth (170), juce::Justification::centredLeft);
            g.drawText ("Post-roll", postRoll.getBounds().withX (16).withWidth (170), juce::Justification::centredLeft);
            g.drawText ("Meters", meters.getBounds().withX (16).withWidth (170), juce::Justification::centredLeft);
            g.drawText ("Hawthorne Route", routeButton.getBounds().withX (16).withWidth (170), juce::Justification::centredLeft);
            g.setColour (Colours::textDim);
            g.setFont (juce::FontOptions (12.0f));
            g.drawText (routeStatus, routeButton.getBounds().withX (routeButton.getRight() + 10).withRight (getWidth() - 10),
                        juce::Justification::centredLeft);
        }
        void resized() override
        {
            auto b = getLocalBounds();
            routeButton.setBounds (b.removeFromBottom (40).reduced (16, 7).withTrimmedLeft (180).withWidth (150));
            meters.setBounds (b.removeFromBottom (36).reduced (16, 5).withTrimmedLeft (180));
            postRoll.setBounds (b.removeFromBottom (30).reduced (16, 4).withTrimmedLeft (180));
            preRoll.setBounds (b.removeFromBottom (30).reduced (16, 4).withTrimmedLeft (180));
            windToggle.setBounds (b.removeFromBottom (36).reduced (16, 4));
            b.removeFromBottom (24);
            selector.setBounds (b);
        }
        juce::AudioDeviceSelectorComponent selector;
        juce::ToggleButton windToggle { "Tape wind physics: spool to locate points (realistic FFWD / REW)" };
        juce::ComboBox meters;
        juce::TextButton routeButton;
        juce::String routeStatus;
        void refreshRoute()
        {
            const bool installed = RouteHost::isDriverInstalled();
            routeButton.setButtonText (installed ? "UNINSTALL" : "INSTALL DRIVER");
            routeStatus = ! installed ? juce::String ("virtual ASIO device for other programs")
                        : engine.isRouteConnected() ? juce::String ("installed - a program is playing in")
                                                    : juce::String ("installed - pick it in the other program");
            repaint();
        }
        std::function<void()> onMeters;
        juce::Slider preRoll { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight },
                     postRoll { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
        TapeEngine& engine;
        juce::PropertiesFile& props;
    };
}

void MainComponent::showAudioSettings()
{
    auto* selector = new SettingsPanel (deviceManager, engine, *props.getUserSettings(), [this] { applySkin(); });

    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (selector);
    o.dialogTitle = "Settings";
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
    MachineSkin::select (reel != nullptr ? reel->machine : 0, reel != nullptr ? reel->masterMachine : 0);
    rebuildModules();
    applySkin();
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
        m->onMenuRequest   = [this] (int track) { showTrackMenu (track); };
        m->onNrMode        = [this] (int track, int mode) { engine.setTrackNr (track, mode); };
        bridge.addAndMakeVisible (m);
    }

    bridge.nrStrip = engine.getNoiseReduction() != nr::none;
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
        m.addItem (1 + i, reel->masters[(size_t) i].name + "   (" + tape::info ((tape::Stock) reel->masters[(size_t) i].stock).label
                              + (reel->masters[(size_t) i].nr != 0 ? juce::String (", ") + nr::info (reel->masters[(size_t) i].nr).label : juce::String()) + ")",
                   ! rec, i == reel->activeMaster);
    if (reel->masters.empty())
        m.addItem (999, "(none yet)", false);
    m.addSeparator();
    m.addItem (1001, "New master", ! rec);
    const bool has = reel->activeMaster >= 0;
    m.addItem (1002, "Rename...", has && ! rec);
    m.addItem (1003, "Delete...", has && ! rec);
    m.addSeparator();
    m.addItem (1004, juce::String ("2-track machine: ") + tape::masterMachine (reel->masterMachine).name + "...", ! rec);
    if (has)
    {
        juce::PopupMenu nrMenu;
        for (int s = 0; s < nr::numSystems; ++s)
            nrMenu.addItem (1100 + s, nr::info (s).name, ! rec, engine.getMasterNr() == s);
        m.addSubMenu (juce::String ("Noise reduction: ") + nr::info (engine.getMasterNr()).name, nrMenu, ! rec);
    }

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
        else if (r == 1004) showMachines();
        else if (r >= 1100 && r < 1100 + nr::numSystems) { engine.setMasterNr (r - 1100); refreshMasterSection(); repaint(); }
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

void MainComponent::newMaster (std::function<void()> then)
{
    if (reel == nullptr || engine.isRecording()) return;

    // A new master tape: pick its stock (mastering tapes are for the 2-track only).
    auto* w = new ReelDialog ("New master", "A fresh 2-track master tape, recorded in step with this reel.",
                              juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", reel->nextMasterName(), "Name:");
    const int lastStock = reel->masters.empty() ? (int) tape::Stock::mastering499 : reel->masters.back().stock;
    w->addStockPicker ("stock", "Master tape (fixed for the life of this master):", tape::forMaster, lastStock);
    w->addMachinePicker ("machine", "2-track machine (can be changed later):", true, reel->masterMachine);
    w->addNrPicker ("nr", reel->masters.empty() ? 0 : reel->masters.back().nr);
    w->addButton ("Create", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w, then] (int result)
    {
        if (result != 1 || reel == nullptr || engine.isRecording()) return;
        auto name = w->getTextEditorContents ("name").trim();
        const int idx = reel->addMaster (name.isNotEmpty() ? name : reel->nextMasterName(), w->stockAt ("stock"),
                                         w->getComboBoxComponent ("nr")->getSelectedItemIndex());
        engine.setMasterMachine (w->getComboBoxComponent ("machine")->getSelectedItemIndex());
        applySkin();
        engine.mastersChanged();
        engine.setActiveMaster (idx);
        refreshMasterSection();
        if (then) then();
    }), true);
}

//==============================================================================
// Tape jobs: import, export, flip

void MainComponent::applySkin()
{
    MachineSkin::select (reel != nullptr ? reel->machine : 0, reel != nullptr ? reel->masterMachine : 0);
    for (auto* m : bridge.modules) { m->getMeter().skinChanged(); m->repaint(); }
    masterL.skinChanged();
    masterR.skinChanged();
    bridge.repaint();
    tempoBox.repaint();
    repaint();
}

void MainComponent::showManual()
{
    // The manual ships inside the app; open it in the browser.
    auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Hawthorne Manual " HAWTHORNE_VERSION ".html");
    if (! f.existsAsFile())
        f.replaceWithData (BinaryData::manual_html, (size_t) BinaryData::manual_htmlSize);
    f.startAsProcess();
}

void MainComponent::showMachineMenu()
{
    if (reel == nullptr) return;
    const bool rec = engine.isRecording();
    juce::PopupMenu m;
    m.addItem (1, juce::String ("Machines...   (") + tape::multitrackMachine (reel->machine).name + ")", ! rec);
    m.addItem (5, "Alignment (calibration)...", ! rec);
    {
        juce::PopupMenu nrMenu;
        for (int s = 0; s < nr::numSystems; ++s)
            nrMenu.addItem (100 + s, nr::info (s).name, ! rec, reel->noiseReduction == s);
        nrMenu.addSeparator();
        for (int md = 0; md < nr::numModes; ++md)
            nrMenu.addItem (200 + md, juce::String ("All tracks: ") + nr::modeName (md), ! rec && reel->noiseReduction != nr::none);
        m.addSubMenu (juce::String ("Noise reduction   (") + nr::info (reel->noiseReduction).name + ")", nrMenu);
    }
    m.addSeparator();
    m.addItem (2, "Import audio onto a track...", ! rec);
    m.addItem (3, "Export all tracks (stems)...", ! rec);
    m.addSeparator();
    m.addItem (4, reel->flipped ? "Turn the reel back over (play forwards)..." : "Flip the reel over (reverse tape)...", ! rec);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (machineButton), [this] (int r)
    {
        if (reel == nullptr) return;
        if (r == 1) showMachines();
        else if (r == 5) showAlignment();
        else if (r == 2) importAudio (0);
        else if (r == 3) { juce::Array<int> all; for (int t = 0; t < reel->numTracks; ++t) all.add (t); exportTracks (all); }
        else if (r == 4) flipReel();
        else if (r >= 100 && r < 100 + nr::numSystems) setNoiseReduction (r - 100);
        else if (r >= 200 && r < 200 + nr::numModes)
            for (int t = 0; t < reel->numTracks; ++t) engine.setTrackNr (t, r - 200);
    });
}

void MainComponent::setNoiseReduction (int system)
{
    if (reel == nullptr || engine.isRecording()) return;
    engine.setNoiseReduction (system);
    bridge.nrStrip = system != nr::none;
    layoutBridge();
    for (auto* m : bridge.modules) m->resized();
    if (sheetWindow != nullptr) sheetWindow->getSheet().refresh();
    repaint();
}

void MainComponent::showTrackMenu (int track)
{
    if (reel == nullptr || engine.isRecording()) return;
    juce::PopupMenu m;
    const auto name = juce::String (track + 1) + " " + reel->tracks[(size_t) track].name;
    m.addSectionHeader ("TRACK " + name.toUpperCase());
    m.addItem (1, "Import audio onto this track...");
    m.addItem (2, "Export this track...", reel->getTapeFile (track).existsAsFile());
    m.addItem (3, "Export all tracks (stems)...");
    if (reel->noiseReduction != nr::none)
    {
        m.addSeparator();
        juce::PopupMenu nrMenu;
        for (int md = 0; md < nr::numModes; ++md)
            nrMenu.addItem (200 + md, nr::modeName (md), true, engine.getTrackNr (track) == md);
        m.addSubMenu (juce::String (nr::info (reel->noiseReduction).name) + "  (" + nr::modeLabel (engine.getTrackNr (track)) + ")", nrMenu);
    }
    m.addSeparator();
    m.addItem (4, "Rename...");
    m.showMenuAsync ({}, [this, track] (int r)
    {
        if (reel == nullptr) return;
        if (r == 1) importAudio (track);
        else if (r == 2) exportTracks ({ track });
        else if (r == 3) { juce::Array<int> all; for (int t = 0; t < reel->numTracks; ++t) all.add (t); exportTracks (all); }
        else if (r == 4) renameTrack (track);
        else if (r >= 200 && r < 200 + nr::numModes) engine.setTrackNr (track, r - 200);
    });
}

void MainComponent::runTapeJob (const juce::String& title, std::function<juce::String (std::function<bool (double)>)> work,
                                std::function<void (bool)> after)
{
    if (reel == nullptr || engine.isRecording()) return;
    const auto pos = engine.getPosition();
    engine.stop();
    engine.setReel (nullptr);          // let go of the tape files while they change

    struct Job : juce::ThreadWithProgressWindow
    {
        Job (const juce::String& t) : juce::ThreadWithProgressWindow (t, true, true) {}
        void run() override { error = work ([this] (double p) { setProgress (p); return ! threadShouldExit(); }); }
        void threadComplete (bool cancelled) override { if (done) done (error, cancelled); delete this; }
        std::function<juce::String (std::function<bool (double)>)> work;
        std::function<void (const juce::String&, bool)> done;
        juce::String error;
    };
    auto* job = new Job (title);
    job->work = std::move (work);
    job->done = [this, pos, title, after] (const juce::String& err, bool cancelled)
    {
        if (reel == nullptr) return;
        if (after) after (err.isEmpty() && ! cancelled);
        juce::String e2;
        reel->equaliseTapeLengths (e2);
        saveReel();
        engine.setReel (reel.get());
        engine.locate (juce::jmin (pos, juce::jmax ((juce::int64) 0, engine.getReelLength())));
        markers.setReel (reel.get());
        rebuildModules();
        if (sheetWindow != nullptr) sheetWindow->getSheet().refresh();
        repaint();
        if (err.isNotEmpty()) showAlert (title, err);
    };
    job->launchThread();
}

void MainComponent::importAudio (int track)
{
    if (reel == nullptr || engine.isRecording()) return;
    chooser = std::make_unique<juce::FileChooser> ("Import audio onto a track",
                                                   juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                   "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, track] (const juce::FileChooser& fc)
    {
        const auto src = fc.getResult();
        if (src == juce::File() || reel == nullptr) return;

        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (src));
        if (rd == nullptr) { showAlert ("Import", "Can't read " + src.getFileName() + " (WAV, AIFF, FLAC, OGG or MP3)."); return; }
        const bool stereo = rd->numChannels > 1;
        const double srcRate = rd->sampleRate;
        const auto srcLen = rd->lengthInSamples;
        rd.reset();

        auto* w = new ReelDialog ("Import audio", src.getFileName() + "\n" + juce::String ((double) srcLen / srcRate, 1) + " s, "
                                      + (stereo ? "stereo" : "mono") + ", " + juce::String (srcRate / 1000.0, 1) + " kHz"
                                      + (std::abs (srcRate - reel->sampleRate) > 0.5 ? "  (converted to " + juce::String (reel->sampleRate / 1000.0, 1) + " kHz)" : juce::String()),
                                  juce::MessageBoxIconType::NoIcon, this);
        juce::StringArray tracks;
        for (int t = 0; t < reel->numTracks; ++t) tracks.add (juce::String (t + 1) + "  " + reel->tracks[(size_t) t].name);
        w->addComboBox ("track", tracks, "Onto track:");
        w->getComboBoxComponent ("track")->setSelectedItemIndex (track);
        if (stereo)
            w->addComboBox ("stereo", { "Left / right onto this track and the next", "Mixed to mono on this track" }, "Stereo file:");
        w->addComboBox ("where", { "From the start of the reel", "From the playhead (" + formatShortTime (engine.getPosition(), reel->sampleRate) + ")" }, "Place it:");
        w->getComboBoxComponent ("where")->setSelectedItemIndex (0);
        if (reel->tapeStock != 0)
        {
            w->addComboBox ("print", { juce::String ("Print it through the tape (") + tape::info ((tape::Stock) reel->tapeStock).label + " on "
                                           + tape::multitrackMachine (reel->machine).label + ")",
                                       "Straight onto the tape (no tape sound)" }, "Tape:");
            w->getComboBoxComponent ("print")->setSelectedItemIndex (0);
        }
        w->addTextBlock ("This records over what's on the track from that point on.");
        w->addButton ("Import", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        const auto here = engine.getPosition();
        w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w, src, stereo, srcLen, srcRate, here] (int result)
        {
            if (result != 1 || reel == nullptr) return;
            const int t0 = w->getComboBoxComponent ("track")->getSelectedItemIndex();
            const bool two = stereo && w->getComboBoxComponent ("stereo")->getSelectedItemIndex() == 0 && t0 + 1 < reel->numTracks;
            const juce::int64 at = w->getComboBoxComponent ("where")->getSelectedItemIndex() == 1 ? here : 0;
            const bool print = reel->tapeStock != 0 && w->getComboBoxComponent ("print")->getSelectedItemIndex() == 0;

            juce::Array<juce::File> files { reel->getTapeFile (t0) };
            if (two) files.add (reel->getTapeFile (t0 + 1));
            auto sh = std::make_shared<tape::Shared>();
            if (print) sh->set ((tape::Stock) reel->tapeStock, reel->ips, reel->sampleRate, tape::multitrackMachine (reel->machine));
            const double rate = reel->sampleRate;
            const auto len = (juce::int64) ((double) srcLen * rate / srcRate);
            const int nrEnc = nr::encodes (reel->tracks[(size_t) t0].nrMode) ? reel->noiseReduction : nr::none;

            runTapeJob ("Importing " + src.getFileName() + "...",
                        [src, files, rate, at, sh, print, nrEnc] (std::function<bool (double)> progress)
                        { return transfer::importFile (src, files, rate, at, print ? sh.get() : nullptr, progress, nrEnc); },
                        [this, src, t0, two, at, len] (bool ok)
                        {
                            if (! ok || reel == nullptr) return;
                            TakeEntry e;
                            e.take = reel->nextTake++;
                            e.when = juce::Time::getCurrentTime().formatted ("%d %b %Y  %H:%M");
                            e.start = at;
                            e.end = at + len;
                            e.tracks = two ? juce::String (t0 + 1) + "-" + juce::String (t0 + 2) : juce::String (t0 + 1);
                            e.comments = "Imported: " + src.getFileName();
                            reel->takeLog.push_back (e);
                            if (reel->tracks[(size_t) t0].name.startsWith ("Track "))
                                reel->tracks[(size_t) t0].name = src.getFileNameWithoutExtension().substring (0, 24);
                        });
        }), true);
    });
}

void MainComponent::exportTracks (juce::Array<int> tracks)
{
    if (reel == nullptr || engine.isRecording()) return;
    juce::Array<int> withTape;
    for (auto t : tracks) if (reel->getTapeFile (t).existsAsFile()) withTape.add (t);
    if (withTape.isEmpty()) { showAlert ("Export", "Nothing has been recorded on " + juce::String (tracks.size() == 1 ? "this track" : "these tracks") + " yet."); return; }

    auto go = [this, withTape] (bool repro)
    {
        chooser = std::make_unique<juce::FileChooser> ("Export to this folder", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory));
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [this, withTape, repro] (const juce::FileChooser& fc)
        {
            const auto folder = fc.getResult();
            if (folder == juce::File() || reel == nullptr) return;
            const auto length = engine.getReelLength();
            const double rate = reel->sampleRate;
            const auto prefix = juce::File::createLegalFileName ((session != nullptr ? session->getName() + " - " : juce::String()) + reel->getName());
            struct Item { juce::File src, dest; int nrDecode; };
            std::vector<Item> items;
            for (auto t : withTape)
                items.push_back ({ reel->getTapeFile (t), folder.getChildFile (prefix + " - " + juce::String (t + 1).paddedLeft ('0', 2) + " "
                                                                          + juce::File::createLegalFileName (reel->tracks[(size_t) t].name) + ".wav"),
                                   nr::decodes (reel->tracks[(size_t) t].nrMode) ? reel->noiseReduction : nr::none });
            auto sh = std::make_shared<tape::Shared>();
            if (repro) sh->set ((tape::Stock) reel->tapeStock, reel->ips * reel->getSpeedRatio(), rate, tape::multitrackMachine (reel->machine));

            runTapeJob ("Exporting tracks...", [items, length, rate, sh, repro] (std::function<bool (double)> progress)
            {
                for (size_t i = 0; i < items.size(); ++i)
                {
                    auto err = transfer::exportTrack (items[i].src, items[i].dest, rate, length, repro ? sh.get() : nullptr,
                                                      [&] (double p) { return progress (((double) i + p) / (double) items.size()); },
                                                      items[i].nrDecode);
                    if (err.isNotEmpty()) return err;
                }
                return juce::String();
            }, [folder] (bool ok) { if (ok) folder.revealToUser(); });
        });
    };

    if (reel->tapeStock == 0) { go (false); return; }
    auto* w = new ReelDialog ("Export tracks", juce::String (withTape.size()) + " track(s) as 24-bit WAV, the length of the reel.",
                              juce::MessageBoxIconType::NoIcon, this);
    w->addComboBox ("sound", { "As printed on the tape", "With the machine's playback sound (head bump, top end, hiss)" }, "Sound:");
    w->addButton ("Export", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([w, go] (int result)
    {
        if (result == 1) go (w->getComboBoxComponent ("sound")->getSelectedItemIndex() == 1);
    }), true);
}

void MainComponent::flipReel()
{
    if (reel == nullptr || engine.isRecording()) return;
    auto* w = new ReelDialog (reel->flipped ? "Turn the reel back over" : "Flip the reel over",
                              juce::String ("Turns the tape over so everything on it plays backwards - record onto it "
                                            "(reverse guitar, backwards cymbals), then turn it back.\n"
                                            "Markers, takes and the punch points flip with it."),
                              juce::MessageBoxIconType::NoIcon, this);
    w->addComboBox ("swap", { "Keep the track numbers", juce::String ("Swap the track order, like a real machine (track 1 plays on track ") + juce::String (reel->numTracks) + ")" },
                    "Tracks:");
    w->getComboBoxComponent ("swap")->setSelectedItemIndex (0);
    w->addButton (reel->flipped ? "Turn back" : "Flip", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w] (int result)
    {
        if (result != 1 || reel == nullptr) return;
        const bool swap = w->getComboBoxComponent ("swap")->getSelectedItemIndex() == 1;
        const auto length = engine.getReelLength();
        if (length <= 0) { showAlert ("Flip", "There's nothing on this reel yet."); return; }

        juce::Array<juce::File> files;
        for (int t = 0; t < reel->numTracks; ++t) files.add (reel->getTapeFile (t));
        juce::Array<juce::File> masters;
        for (int m = 0; m < (int) reel->masters.size(); ++m)
            for (int side = 0; side < 2; ++side) masters.add (reel->getMasterFile (m, side));
        const double rate = reel->sampleRate;
        const int n = reel->numTracks;

        runTapeJob ("Flipping the reel...", [files, masters, rate, length, swap, n] (std::function<bool (double)> progress)
        {
            juce::Array<juce::File> all;
            for (auto& f : files)   if (f.existsAsFile()) all.add (f);
            for (auto& f : masters) if (f.existsAsFile()) all.add (f);
            for (int i = 0; i < all.size(); ++i)
            {
                auto err = transfer::reverseFile (all[i], rate, length, [&] (double p) { return progress (((double) i + p) / (double) all.size()); });
                if (err.isNotEmpty()) return err;
            }
            if (swap)
                for (int a = 0, b = n - 1; a < b; ++a, --b)
                {
                    // the tape's track 1 now passes the head for the last track
                    const auto fa = files[a], fb = files[b];
                    const auto tmp = fa.getSiblingFile (fa.getFileName() + ".swap");
                    if (fa.existsAsFile() && ! fa.moveFileTo (tmp)) return juce::String ("Could not swap the tracks.");
                    if (fb.existsAsFile() && ! fb.moveFileTo (fa)) return juce::String ("Could not swap the tracks.");
                    if (tmp.existsAsFile() && ! tmp.moveFileTo (fb)) return juce::String ("Could not swap the tracks.");
                }
            return juce::String();
        }, [this, length, swap] (bool ok)
        {
            if (ok && reel != nullptr) reel->flipMetadata (length, swap);
        });
    }), true);
}

void MainComponent::showAlignment()
{
    if (reel == nullptr || engine.isRecording()) return;
    auto* panel = new AlignmentPanel (engine, *reel);
    panel->onRecordTones = [this, panel]
    {
        if (auto* dw = panel->findParentComponentOfClass<juce::DialogWindow>()) dw->exitModalState (0);
        juce::MessageManager::callAsync ([this] { recordTones(); });
    };
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (panel);
    o.dialogTitle = "Alignment";
    o.dialogBackgroundColour = Colours::bg;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.launchAsync();
}

void MainComponent::recordTones()
{
    if (reel == nullptr || engine.isRecording()) return;
    const double tone = 30.0;
    auto* w = new ReelDialog ("Record line-up tones",
                              "Records 1 kHz, 10 kHz and 100 Hz at 0 VU, " + juce::String ((int) tone) + " s each, from the start of the reel on all "
                                  + juce::String (reel->numTracks) + " tracks - over anything already there.\n"
                                  "They go through the tape and its alignment (no noise reduction), and markers are added.",
                              juce::MessageBoxIconType::NoIcon, this);
    w->addButton ("Record tones", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, tone] (int result)
    {
        if (result != 1 || reel == nullptr) return;
        const double rate = reel->sampleRate;
        auto src = reel->getAudioFolder().getChildFile ("lineup-tones.wav");
        {
            src.deleteFile();
            std::unique_ptr<juce::OutputStream> os (src.createOutputStream().release());
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatWriter> wr (os != nullptr ? wav.createWriterFor (os.get(), rate, 1, 24, {}, 0) : nullptr);
            if (wr == nullptr) { showAlert ("Line-up tones", "Could not write the tones."); return; }
            os.release();
            const int len = (int) (tone * rate);
            juce::AudioBuffer<float> b (1, len);
            for (double f : { 1000.0, 10000.0, 100.0 })
            {
                for (int k = 0; k < len; ++k)
                {
                    const double fade = juce::jmin (1.0, juce::jmin ((double) k, (double) (len - 1 - k)) / (0.01 * rate));
                    b.setSample (0, k, (float) (0.17783 * fade * std::sin (juce::MathConstants<double>::twoPi * f * k / rate)));
                }
                wr->writeFromAudioSampleBuffer (b, 0, len);
            }
        }
        juce::Array<juce::File> files;
        for (int t = 0; t < reel->numTracks; ++t) files.add (reel->getTapeFile (t));
        auto sh = std::make_shared<tape::Shared>();
        const bool print = reel->tapeStock != 0 && reel->machine != tape::noMultitrackMachine;
        if (print)
        {
            const auto st = (tape::Stock) reel->tapeStock;
            sh->set (st, reel->ips, rate, tape::multitrackMachine (reel->machine),
                     (double) (reel->alignLevel - tape::recommendedLevel (st)), reel->alignBias - tape::recommendedBias (st));
        }
        runTapeJob ("Recording line-up tones...", [src, files, rate, sh, print] (std::function<bool (double)> progress)
        {
            for (int i = 0; i < files.size(); ++i)
            {
                auto err = transfer::importFile (src, { files[i] }, rate, 0, print ? sh.get() : nullptr,
                                                 [&] (double p) { return progress (((double) i + p) / (double) files.size()); });
                if (err.isNotEmpty()) return err;
            }
            return juce::String();
        }, [this, src, tone, rate] (bool ok)
        {
            src.deleteFile();
            if (! ok || reel == nullptr) return;
            const auto len = (juce::int64) (tone * rate);
            const char* names[] = { "1 kHz TONE", "10 kHz TONE", "100 Hz TONE" };
            for (int i = 0; i < 3; ++i)
            {
                bool have = false;
                for (auto& mk : reel->markers) have = have || mk.position == len * i;
                if (! have) reel->markers.push_back ({ len * i, names[i] });
            }
            std::sort (reel->markers.begin(), reel->markers.end(), [] (auto& a, auto& b) { return a.position < b.position; });
            TakeEntry e;
            e.take = reel->nextTake++;
            e.when = juce::Time::getCurrentTime().formatted ("%d %b %Y  %H:%M");
            e.start = 0;
            e.end = len * 3;
            e.tracks = "1-" + juce::String (reel->numTracks);
            e.comments = "Line-up tones 1k / 10k / 100 Hz @ 0 VU";
            reel->takeLog.push_back (e);
            reel->info.set ("Tones", "1k / 10k / 100 Hz @ 0 VU");
        });
    }), true);
}

void MainComponent::showMachines()
{
    if (reel == nullptr || engine.isRecording()) return;
    auto* picker = new MachinePicker (reel->machine, reel->masterMachine, [this] (int mt, int ms)
    {
        if (reel == nullptr || engine.isRecording()) return;
        engine.setMachine (mt);
        engine.setMasterMachine (ms);
        applySkin();
    });
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (picker);
    o.dialogTitle = "Machines - click one to put the reel on it";
    o.dialogBackgroundColour = Colours::bg;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = true;
    o.launchAsync();
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
                // noise reduction: decoded, starting a little early so the decoder has settled
                nr::Settings nrs;
                nrs.set (nrSystem, rate);
                std::array<nr::Channel, 2> dec;
                for (auto& d : dec) d.reset();
                juce::int64 pre = nrSystem != 0 ? juce::jmin (start, (juce::int64) rate) : 0;
                for (auto pos = start - pre; pos < end && ! threadShouldExit();)
                {
                    const int n = (int) juce::jmin ((juce::int64) buf.getNumSamples(), (pos < start ? start : end) - pos);
                    buf.clear();
                    float* l = buf.getWritePointer (0);
                    float* r = buf.getWritePointer (1);
                    rl->read (&l, 1, pos, n);
                    rr->read (&r, 1, pos, n);
                    if (nrSystem != 0) { dec[0].decode (nrs, l, n); dec[1].decode (nrs, r, n); }
                    if (pos < start) { pos += n; continue; }
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
            int nrSystem = 0;
            juce::String error;
        };

        auto* job = new Job (fileL, fileR, dest, start, end, reel->sampleRate);
        job->nrSystem = juce::isPositiveAndBelow (reel->activeMaster, (int) reel->masters.size()) ? reel->masters[(size_t) reel->activeMaster].nr : 0;
        job->launchThread();
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

void MainComponent::showSyncMenu()
{
    // Like a DAW's transport sync: internal, or follow external timecode
    juce::PopupMenu m;
    const auto src = engine.getChase();
    m.addSectionHeader ("SYNC");
    m.addItem (1, "Internal - Hawthorne runs the tape", true, src == TapeEngine::ChaseSource::off);

    juce::PopupMenu ltc, mtc;
    if (auto* dev = deviceManager.getCurrentAudioDevice())
    {
        const auto names = dev->getInputChannelNames();
        const auto active = dev->getActiveInputChannels();
        for (int ch = 0; ch < names.size(); ++ch)
            if (active[ch])
                ltc.addItem (1000 + ch, "In " + juce::String (ch + 1) + "  " + names[ch], true,
                             src == TapeEngine::ChaseSource::ltc && syncSettings.ltcIn == ch);
    }
    const auto ins = juce::MidiInput::getAvailableDevices();
    for (int i = 0; i < ins.size(); ++i)
        mtc.addItem (2000 + i, ins[i].name, true, src == TapeEngine::ChaseSource::mtc && syncSettings.mtcIn == ins[i].identifier);
    if (ins.isEmpty()) mtc.addItem (-1, "(no MIDI inputs)", false);

    m.addSubMenu ("External - chase LTC on", ltc, true, nullptr, src == TapeEngine::ChaseSource::ltc);
    m.addSubMenu ("External - chase MTC from", mtc, true, nullptr, src == TapeEngine::ChaseSource::mtc);
    m.addSeparator();
    m.addItem (3, "Timecode settings...");

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (syncSourceButton), [this, ins] (int r)
    {
        auto s = syncSettings;
        if (r == 1) s.chase = TapeEngine::ChaseSource::off;
        else if (r >= 1000 && r < 2000) { s.chase = TapeEngine::ChaseSource::ltc; s.ltcIn = r - 1000; }
        else if (r >= 2000 && r - 2000 < ins.size()) { s.chase = TapeEngine::ChaseSource::mtc; s.mtcIn = ins[r - 2000].identifier; }
        else if (r == 3) { showSync (SyncPanel::Page::timecode); return; }
        else return;
        applySyncSettings (s);
        for (auto* w : { tempoWindow.get(), timecodeWindow.get() }) if (w != nullptr) w->getPanel().refresh();
    });
}

void MainComponent::showCounterMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader ("MAIN DISPLAY");
    m.addItem (1, "Minutes : seconds", true, counterMode == 0);
    m.addItem (2, "Bars . beats", reel != nullptr && reel->tempoBpm > 0.0, counterMode == 1);
    m.addItem (3, "Timecode", true, counterMode == 2);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (counterArea)), [this] (int r)
    {
        if (r <= 0) return;
        counterMode = r - 1;
        props.getUserSettings()->setValue ("counterMode", counterMode);
        repaint (counterArea);
    });
}

void MainComponent::mouseMove (const juce::MouseEvent& e)
{
    const bool overBadge = deckBadgeArea.contains (e.getPosition()) || masterBadgeArea.contains (e.getPosition());
    setMouseCursor (overBadge || counterArea.contains (e.getPosition()) ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void MainComponent::mouseUp (const juce::MouseEvent& e)
{
    // (on release, so the release doesn't pick an item from the menu it opened)
    if (counterArea.contains (e.getPosition()) && counterArea.contains (e.getMouseDownPosition()))
        showCounterMenu();
    // the machine badges: change machines
    for (auto b : { deckBadgeArea, masterBadgeArea })
        if (b.contains (e.getPosition()) && b.contains (e.getMouseDownPosition()))
            showMachines();
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

    // Hawthorne Route: another program's outputs, while it plays into the driver
    // (or while a track is patched to one, so the patch stays visible)
    bool routeUsed = false;
    if (reel != nullptr)
        for (int t = 0; t < reel->numTracks; ++t)
            routeUsed = routeUsed || route::isRouteInput (reel->tracks[(size_t) t].input);
    const bool routeLive = engine.isRouteConnected();
    if (routeLive || routeUsed)
    {
        for (int ch = 0; ch < route::toHawthorneChannels; ++ch)
            inputs.push_back ({ route::inputBase + ch, "ROUTE " + juce::String (ch + 1) + " (Hawthorne Route)", -1, ch, routeLive });
        key << "route" << (routeLive ? "L" : "-");
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
        engine.punchOutSmooth();
        return;
    }

    if (engine.getAutoPunch())
    {
        if (engine.isAutoPunching()) { engine.punchOutSmooth(); return; }   // REC again: cancel the punch
        if (auto err = engine.autoRecord(); err.isNotEmpty())
            showAlert ("Cannot auto punch", err);
        return;
    }

    if (auto err = engine.record(); err.isNotEmpty())
        showAlert ("Cannot record", err);
}

void MainComponent::setPunchHere (bool isIn)
{
    if (reel == nullptr) return;
    auto in = engine.getPunchIn(), out = engine.getPunchOut();
    const auto target = isIn ? in : out;

    if (juce::ModifierKeys::currentModifiers.isShiftDown() || juce::ModifierKeys::currentModifiers.isCommandDown())
    {
        if (target >= 0) locate (target);
        return;
    }
    if (engine.isRecording() || engine.isAutoPunching()) return;

    const auto here = engine.getWindTarget();
    if (isIn) { in = here; if (out >= 0 && out <= in) out = -1; }
    else      { out = here; if (in >= 0 && in >= out) in = -1; }
    engine.setPunchPoints (in, out);
    repaint (locatorArea);
}

void MainComponent::locate (juce::int64 pos)
{
    if (! engine.isRecording())
        engine.windTo (pos);
}

void MainComponent::nudge (double seconds)
{
    locate (engine.getWindTarget() + (juce::int64) (seconds * sessionRate()));
}

bool MainComponent::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::F11Key
        || (k == juce::KeyPress::escapeKey && juce::Desktop::getInstance().getKioskModeComponent() != nullptr))
    {
        toggleFullScreen();
        return true;
    }

    if (k == juce::KeyPress::F1Key) { showManual(); return true; }

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
    const int windDir = engine.getWindDirection();
    rewKey.setLit (windDir < 0);
    liftKey.setLit (engine.getLifterDefeat());
    inKey.setLit (engine.getPunchIn() >= 0);
    outKey.setLit (engine.getPunchOut() >= 0);
    autoKey.setLit (engine.getAutoPunch() && (! engine.isAutoPunching() || flash));
    rehKey.setLit (engine.getRehearse());
    loopKey.setLit (engine.getCycle());
    if (frame % 10 == 0) repaint (locatorArea);
    ffwKey.setLit (windDir > 0);
    clickKey.setLit (reel != nullptr && reel->clickOn && reel->tempoBpm > 0.0);
    if (frame % 5 == 0)
    {
        const auto src = engine.getChase();
        juce::String t = "SYNC: INTERNAL";
        auto bgc = Colours::panelLight, txt = Colours::text;
        if (src != TapeEngine::ChaseSource::off)
        {
            const auto st = engine.getChaseStatus();
            t = juce::String ("SYNC: EXT ") + (src == TapeEngine::ChaseSource::mtc ? "MTC" : "LTC")
              + (st.locked ? "  LOCKED" : (st.receiving ? "" : "  - NO TC"));
            bgc = st.locked ? Colours::mustard : Colours::panelLight;
            txt = st.locked ? Colours::black : (st.receiving ? Colours::mustard : Colours::red);
        }
        if (syncSourceButton.getButtonText() != t) syncSourceButton.setButtonText (t);
        syncSourceButton.setColour (juce::TextButton::buttonColourId, bgc);
        syncSourceButton.setColour (juce::TextButton::textColourOffId, txt);
        tempoBox.repaint();
    }
    recKey.setLit (rec);
    stopKey.setLit (! rolling && windDir == 0);

    for (int t = 0; t < bridge.modules.size(); ++t)
    {
        auto* m = bridge.modules[t];
        m->setArmed (engine.isArmed (t), rec, flash);
        {
            const int sys = engine.getNoiseReduction(), mode = engine.getTrackNr (t);
            const bool armedT = engine.isArmed (t);
            int lamp = 0;
            if (sys != nr::none)
            {
                if (armedT && rec)                         lamp = nr::encodes (mode) ? 1 : 0;
                else if (rolling && ! (armedT && rec))     lamp = nr::decodes (mode) ? 2 : 0;
            }
            m->setNr (sys, mode, lamp);
        }
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
    g.drawText ((reel != nullptr ? juce::String (reel->flipped ? "FLIPPED  " : "") + tape::multitrackMachine (reel->machine).label + "  "
                                     + tape::info ((tape::Stock) reel->tapeStock).label + "  " : juce::String())
                  + juce::String (reel != nullptr ? reel->numTracks : 0) + "-TRACK  v" HAWTHORNE_VERSION,
                sessionArea, juce::Justification::centredRight);

    // Transport deck, styled after the machine
    const auto& sk = MachineSkin::current();
    {
        juce::ColourGradient deckGrad (sk.deck, 0.0f, (float) deckArea.getY(), sk.deckLow, 0.0f, (float) deckArea.getBottom(), false);
        g.setGradientFill (deckGrad);
        g.fillRect (deckArea);
        if (sk.brushed)
        {
            juce::Random rnd (1234);
            for (int y = deckArea.getY(); y < deckArea.getBottom(); ++y)
            {
                g.setColour ((rnd.nextBool() ? juce::Colours::white : juce::Colours::black).withAlpha (0.02f + 0.04f * rnd.nextFloat()));
                g.drawHorizontalLine (y, (float) deckArea.getX(), (float) deckArea.getRight());
            }
        }
        g.setColour (sk.deckEdge);
        g.drawHorizontalLine (deckArea.getY(), 0.0f, (float) getWidth());
        g.setColour (sk.deck.brighter (0.25f).withAlpha (0.5f));
        g.drawHorizontalLine (deckArea.getY() + 1, 0.0f, (float) getWidth());
    }

    // counter window
    const bool drum = sk.counter == MachineSkin::Counter::drum;
    g.setColour (sk.deckEdge.darker (0.4f));
    g.fillRoundedRectangle (counterArea.toFloat().expanded (2.0f), 5.0f);
    g.setColour (sk.counterBg);
    g.fillRoundedRectangle (counterArea.toFloat(), 4.0f);

    const bool rec = engine.isRecording();
    const juce::Colour ink = sk.counterInk;
    g.setColour (rec && ! drum ? Colours::red : ink);
    {
        // top: state and which readout is big.  middle: the chosen readout (click the clock to
        // choose).  bottom: the other two.
        auto c = counterArea.reduced (8, 3);
        auto top = c.removeFromTop (13), bottom = c.removeFromBottom (15);
        const auto pos = engine.getPosition();

        const juce::String ms = formatTime (pos, sessionRate());
        const juce::String bbt = reel != nullptr ? reel->bbtString (pos) : juce::String();
        const auto rate = engine.getTimecodeRate();
        const juce::String tc = timecode::fromFrames ((juce::int64) std::floor (engine.timecodeFramesAt ((double) pos) + 1.0e-9), rate)
                                    .toString (timecode::isDrop (rate));
        const int mode = (counterMode == 1 && bbt.isEmpty()) ? 0 : counterMode;
        const juce::String big = mode == 1 ? bbt : (mode == 2 ? tc : ms);

        const auto bigFont = monoFont (juce::jmin ((float) c.getHeight() * 0.95f, (float) c.getWidth() / 7.6f), true);
        if (drum)
        {
            // mechanical counter: white figures on turning drums
            const float cw = (float) c.getWidth() / (float) juce::jmax (1, big.length()) * 0.98f;
            const float ch = (float) c.getHeight() * 0.92f;
            auto cell = juce::Rectangle<float> (cw * (float) big.length(), ch).withCentre (c.toFloat().getCentre());
            g.setFont (bigFont.withHeight (ch * 0.86f));
            for (int i = 0; i < big.length(); ++i)
            {
                auto d = cell.removeFromLeft (cw);
                const auto chr = juce::String::charToString (big[i]);
                if (juce::CharacterFunctions::isDigit (big[i]))
                {
                    juce::ColourGradient drumGrad (juce::Colour (0xff0b0b0b), d.getX(), d.getY(), juce::Colour (0xff0b0b0b), d.getX(), d.getBottom(), false);
                    drumGrad.addColour (0.5, juce::Colour (0xff2c2c2a));
                    g.setGradientFill (drumGrad);
                    g.fillRect (d.reduced (1.0f, 0.0f));
                }
                g.setColour (ink);
                g.drawText (chr, d, juce::Justification::centred, false);
            }
            g.setColour (juce::Colour (0x30000000));
            g.fillRect (c.toFloat().withHeight ((float) c.getHeight() * 0.18f));
            g.fillRect (c.toFloat().withTrimmedTop ((float) c.getHeight() * 0.82f));
        }
        else
        {
            // LED / fluorescent display: unlit segments behind, a glow around the lit ones
            g.setFont (bigFont);
            juce::String ghost;
            for (auto ch : big) ghost << (juce::CharacterFunctions::isDigit (ch) ? juce::juce_wchar ('8') : ch);
            g.setColour (ink.withAlpha (0.08f));
            g.drawText (ghost, c, juce::Justification::centred);
            const auto lit = rec ? Colours::red : ink;
            g.setColour (lit.withAlpha (0.22f));
            g.drawText (big, c.translated (1, 0), juce::Justification::centred);
            g.drawText (big, c.translated (-1, 0), juce::Justification::centred);
            g.setColour (lit);
            g.drawText (big, c, juce::Justification::centred);
        }

        g.setFont (monoFont (10.0f, true));
        g.setColour (rec ? Colours::red : (engine.isPlaying() ? ink : ink.withAlpha (0.5f)));
        const int wd = engine.getWindDirection();
        g.drawText (rec ? "REC" : (engine.isPlaying() ? "PLAY" : wd < 0 ? "REW" : wd > 0 ? "F FWD" : "STOP"), top, juce::Justification::centredLeft);
        g.setColour (ink.withAlpha (0.5f));
        g.drawText (mode == 1 ? juce::String ("BARS . BEATS") : (mode == 2 ? "TIMECODE  " + timecode::name (rate) : juce::String ("MIN : SEC")),
                    top, juce::Justification::centredRight);

        g.setFont (monoFont (13.0f, true));
        const juce::String bbtLine = bbt.isNotEmpty() ? "BAR " + bbt : juce::String ("NO TEMPO");
        g.setColour (ink.withAlpha (0.7f));
        g.drawText (mode == 0 ? bbtLine : ms, bottom, juce::Justification::centredLeft);
        g.setColour (ink.withAlpha (0.5f));
        g.drawText (mode == 2 ? bbtLine : "TC " + tc, bottom, juce::Justification::centredRight);
    }

    if (reel != nullptr)
    {
        // the maker's badge
        {
            g.setFont (juce::Font (juce::FontOptions ("Arial", 15.0f, juce::Font::bold)));
            const juce::String badgeText = juce::String (sk.brand) + "  " + sk.model;
            const int bw = juce::jmin (reelInfoArea.getWidth(), (int) juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), badgeText) + 22);
            auto badge = juce::Rectangle<int> (bw, 22).withPosition (reelInfoArea.getRight() - bw, deckArea.getY() + 8);
            deckBadgeArea = badge;
            g.setColour (sk.badge);
            g.fillRoundedRectangle (badge.toFloat(), 3.0f);
            g.setColour (sk.badgeText.withAlpha (0.35f));
            g.drawRoundedRectangle (badge.toFloat().reduced (1.5f), 2.0f, 1.0f);
            g.setColour (sk.badgeText);
            g.drawText (badgeText, badge, juce::Justification::centred, false);
        }
        g.setColour (sk.deckTextDim);
        g.setFont (monoFont (12.0f, true));
        g.drawFittedText ("REEL  " + formatTime (engine.getReelLength(), sessionRate()).dropLastCharacters (4)
                              + "\n" + juce::String (reel->numTracks) + " TRACKS  -  "
                              + juce::String (reel->sampleRate / 1000.0, 1) + " kHz / 24-bit"
                              + "\n" + speedText() + (reel->noiseReduction != nr::none ? juce::String ("  -  ") + nr::info (reel->noiseReduction).label : juce::String()),
                          reelInfoArea.withTrimmedTop (24), juce::Justification::centredLeft, 3);
        g.setColour (sk.deckTextDim);
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
    {
        // the 2-track the master is on
        const auto& ms = MachineSkin::currentMaster();
        const juce::String t = juce::String (ms.brand) + "  " + ms.model;
        g.setFont (juce::Font (juce::FontOptions ("Arial", 11.0f, juce::Font::bold)));
        const int bw = (int) juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), t) + 14;
        auto badge = juce::Rectangle<int> (masterLabelArea.getRight() - bw, masterLabelArea.getCentreY() - 9, bw, 18);
        masterBadgeArea = badge;
        g.setColour (ms.badge);
        g.fillRoundedRectangle (badge.toFloat(), 3.0f);
        g.setColour (ms.badgeText.withAlpha (0.35f));
        g.drawRoundedRectangle (badge.toFloat().reduced (1.0f), 2.0f, 1.0f);
        g.setColour (ms.badgeText);
        g.drawText (t, badge, juce::Justification::centred, false);
        if (const int mnr = engine.getMasterNr(); mnr != 0)
        {
            // the master's noise reduction, beside the badge
            const juce::String nt = nr::info (mnr).label;
            const int nw = (int) juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), nt) + 12;
            auto nb = badge.withWidth (nw).translated (-nw - 6, 0);
            g.setColour (juce::Colour (0xff1e1e1e));
            g.fillRoundedRectangle (nb.toFloat(), 3.0f);
            g.setColour (juce::Colour (0xffdddddd));
            g.drawRoundedRectangle (nb.toFloat().reduced (1.0f), 2.0f, 1.0f);
            g.drawText (nt, nb, juce::Justification::centred, false);
        }
    }

    // Autolocator
    g.setColour (Colours::panel);
    g.fillRect (locatorArea);
    g.setColour (Colours::edge);
    g.drawVerticalLine (locatorArea.getX(), (float) locatorArea.getY(), (float) locatorArea.getBottom());
    g.drawHorizontalLine (locatorArea.getBottom() - 1, (float) locatorArea.getX(), (float) locatorArea.getRight());
    g.setColour (Colours::mustard);
    g.setFont (monoFont (12.0f, true));
    g.drawText ("AUTOLOCATOR", locatorLabelArea, juce::Justification::centredLeft);
    {
        const double sr = sessionRate();
        auto t = [&] (juce::int64 v) { return v >= 0 ? formatShortTime (v, sr) : juce::String ("--:--"); };
        g.setColour (Colours::text);
        g.setFont (monoFont (13.0f, true));
        g.drawText (t (engine.getPunchIn()), inTimeArea, juce::Justification::centredLeft);
        g.drawText (t (engine.getPunchOut()), outTimeArea, juce::Justification::centredLeft);
        if (engine.isAutoPunching())
        {
            g.setColour (Colours::red);
            g.setFont (monoFont (10.0f, true));
            g.drawText (engine.getRehearse() ? "REHEARSING" : (engine.isRecording() ? "PUNCHED IN" : "PUNCH ARMED"),
                        locatorLabelArea, juce::Justification::centredRight);
        }
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
        fullScreenButton.setBounds (h.removeFromRight (150));
        h.removeFromRight (8);
        helpButton.setBounds (h.removeFromRight (40));
        h.removeFromRight (8);
        audioButton.setBounds (h.removeFromRight (120));
        h.removeFromRight (8);
        machineButton.setBounds (h.removeFromRight (100));
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
        d.removeFromLeft (8);

        // tempo (top) and sync (bottom), right by the clock
        {
            auto ts = d.removeFromLeft (compact ? 170 : 190).reduced (0, 4);
            tempoBox.setBounds (ts.removeFromTop (ts.getHeight() / 2 - 2));
            ts.removeFromTop (4);
            syncSourceButton.setBounds (ts);
        }
        d.removeFromLeft (compact ? 12 : 18);

        const int kw = compact ? 56 : 64, kwBig = compact ? 62 : 72, gap = compact ? 10 : 14;
        auto key = [&] (TransportKey& k, int w)
        {
            k.setBounds (d.removeFromLeft (w).reduced (0, 6));
            d.removeFromLeft (6);
        };
        key (rtzKey, kw); key (rewKey, kw); key (ffwKey, kw); key (liftKey, kw);
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
    {
        auto la = sidebar.removeFromTop (94);
        locatorArea = la;
        la = la.reduced (12, 6).withTrimmedLeft (1);
        locatorLabelArea = la.removeFromTop (16);
        la.removeFromTop (4);
        auto row1 = la.removeFromTop (28);
        la.removeFromTop (6);
        auto row2 = la.removeFromTop (28);
        const int kwL = 52;
        inKey.setBounds (row1.removeFromLeft (kwL));  row1.removeFromLeft (6);
        inTimeArea = row1.removeFromLeft ((row1.getWidth() - kwL - 6) / 2);
        row1.removeFromLeft (0);
        outKey.setBounds (row1.removeFromLeft (kwL)); row1.removeFromLeft (6);
        outTimeArea = row1;
        const int w3 = (row2.getWidth() - 12) / 3;
        autoKey.setBounds (row2.removeFromLeft (w3)); row2.removeFromLeft (6);
        rehKey.setBounds (row2.removeFromLeft (w3));  row2.removeFromLeft (6);
        loopKey.setBounds (row2);
    }
    markers.setBounds (sidebar);

    bridgeView.setBounds (r);
    layoutBridge();

    if (library.isVisible())
        library.setBounds (getLocalBounds());
}

} // namespace hawthorne
