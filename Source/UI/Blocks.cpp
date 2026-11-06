#include "Blocks.h"
#include "MainComponent.h"
#include "MachineSkin.h"
#include "UIHelpers.h"

namespace hawthorne
{

// The deck's face behind a block, in the machine's style
static void paintDeckFace (juce::Graphics& g, juce::Rectangle<int> r)
{
    const auto& sk = MachineSkin::current();
    g.setGradientFill (juce::ColourGradient (sk.deck, 0.0f, (float) r.getY(), sk.deckLow, 0.0f, (float) r.getBottom(), false));
    g.fillRect (r);
    g.setColour (sk.deckEdge);
    g.drawRect (r, 1);
}

static void paintPanelFace (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& label)
{
    g.setColour (Colours::panel);
    g.fillRect (r);
    g.setColour (Colours::edge);
    g.drawRect (r, 1);
    g.setColour (Colours::mustard);
    g.setFont (monoFont (12.0f, true));
    g.drawText (label, r.reduced (10, 4).withHeight (18), juce::Justification::centredLeft);
}

/** A key on a block that works like one of the deck's own (and lights with it). */
struct MirrorKey : public TransportKey
{
    MirrorKey (TransportKey& src, juce::Colour lamp)
        : TransportKey (src.getButtonText(), src.getTooltip(), lamp), source (src)
    {
        onClick = [this] { source.triggerClick(); };
    }
    void follow()
    {
        setLit (source.isLit());
        setEnabled (source.isEnabled());
    }
    TransportKey& source;
};

//==============================================================================
/** Meters: a meter bridge of its own - which tracks, how many per row, VU / PPM, names. */
class MetersBlock : public Panel
{
public:
    explicit MetersBlock (MainComponent& d) : Panel ("meters", "Meters"), deck (d)
    {
        view.setViewedComponent (&bridge, false);
        view.setScrollBarsShown (true, false);
        view.setScrollBarThickness (8);
        addAndMakeVisible (view);
        onLayout = [this] { view.setBounds (getLocalBounds()); layoutBridge(); };
    }

    void addSettings (juce::PopupMenu& m) override
    {
        juce::PopupMenu rows;
        for (int n : { 4, 6, 8, 12, 16, 24 })
            rows.addItem (10 + n, juce::String (n) + " per row", true, setting ("perRow", 8) == n);
        rows.addItem (10, "Fit (as big as they go, no scrolling)", true, setting ("perRow", 8) == 0);
        m.addSubMenu ("Meters per row", rows);

        const int n = numTracks();
        juce::PopupMenu from, to;
        for (int t = 1; t <= n; ++t)
        {
            from.addItem (100 + t, "Track " + juce::String (t), true, first() == t - 1);
            to.addItem (300 + t, "Track " + juce::String (t), t - 1 >= first(), last() == t - 1);
        }
        m.addSubMenu ("From track  (" + juce::String (first() + 1) + ")", from);
        m.addSubMenu ("To track  (" + juce::String (last() + 1) + ")", to);
        m.addItem (99, "All tracks", true, setting ("from", 1) == 1 && setting ("to", 0) == 0);

        juce::PopupMenu style;
        style.addItem (50, "As in SETTINGS", true, setting ("style", -1) == -1);
        style.addItem (51, "As the machine had them", true, setting ("style", -1) == 0);
        style.addItem (52, "VU", true, setting ("style", -1) == 1);
        style.addItem (53, "PPM", true, setting ("style", -1) == 2);
        m.addSubMenu ("Meter style", style);
        m.addItem (60, "Track names", true, setting ("names", 1) != 0);
    }

    void settingChosen (int r) override
    {
        if (r >= 10 && r < 50)       set ("perRow", r - 10);
        else if (r == 99)            { settings.set ("to", 0); set ("from", 1); }
        else if (r > 100 && r < 300) { if (setting ("to", 0) != 0 && setting ("to", 0) < r - 100) settings.set ("to", r - 100); set ("from", r - 100); }
        else if (r > 300 && r < 500) set ("to", r - 300);
        else if (r >= 50 && r <= 53) set ("style", r - 51);
        else if (r == 60)            set ("names", setting ("names", 1) != 0 ? 0 : 1);
    }

    void settingsChanged() override
    {
        const int per = setting ("perRow", 8);
        bridge.setPerRow (per);
        if (per != 0 && per != 8 && per != 12 && per != 16 && per != 24) bridge.perRowSetting = per;   // (4 and 6 too)
        minW = per > 0 ? juce::jmax (260, per * 64) : 300;
        minH = 160;
        rebuild();
    }

    void reelChanged() override { rebuild(); }

    void refresh() override
    {
        const bool rec = deck.engine.isRecording(), rolling = deck.engine.isPlaying();
        const int sys = deck.engine.getNoiseReduction();
        for (auto* m : bridge.modules)
        {
            const int t = m->getIndex();
            const bool armed = deck.engine.isArmed (t);
            m->setArmed (armed, rec, deck.frameFlash);
            const int mode = deck.engine.getTrackNr (t);
            int lamp = 0;
            if (sys != nr::none)
            {
                if (armed && rec)  lamp = nr::encodes (mode) ? 1 : 0;
                else if (rolling)  lamp = nr::decodes (mode) ? 2 : 0;
            }
            m->setNr (sys, mode, lamp);
            if (juce::isPositiveAndBelow (t, 96))
                m->getMeter().update (deck.frameRms[(size_t) t], deck.framePeak[(size_t) t], deck.frameDt);
        }
    }

private:
    int numTracks() const { return deck.reel != nullptr ? deck.reel->numTracks : 0; }
    int first() const     { return juce::jlimit (0, juce::jmax (0, numTracks() - 1), setting ("from", 1) - 1); }
    int last() const      { const int to = setting ("to", 0); return to <= 0 ? numTracks() - 1 : juce::jlimit (first(), juce::jmax (0, numTracks() - 1), to - 1); }

    void rebuild()
    {
        bridge.modules.clear();
        if (deck.reel != nullptr)
            for (int t = first(); t <= last(); ++t)
            {
                auto* m = bridge.modules.add (new ChannelModule (t));
                deck.wireModule (*m);
                m->setTrackName (deck.reel->tracks[(size_t) t].name);
                m->getMeter().setStyle (setting ("style", -1));
                m->getMeter().showLabel (setting ("names", 1) != 0);
                bridge.addAndMakeVisible (m);
            }
        layoutBridge();
    }

    void layoutBridge()
    {
        const int w = view.getWidth() - view.getScrollBarThickness();
        if (w > 60) bridge.layoutForWidth (w, view.getHeight());
    }

    MainComponent& deck;
    juce::Viewport view;
    MeterBridge bridge;
};

//==============================================================================
/** The time: the deck's counter, in the format chosen for this block. */
class TimeBlock : public Panel
{
public:
    explicit TimeBlock (MainComponent& d) : Panel ("time", "Time"), deck (d)
    {
        minW = 200; minH = 60;
        onPaint = [this] (juce::Graphics& g)
        {
            paintDeckFace (g, getLocalBounds());
            deck.paintCounter (g, getLocalBounds().reduced (8), setting ("format", 0));
        };
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }
    void addSettings (juce::PopupMenu& m) override
    {
        m.addItem (1, "Minutes : seconds", true, setting ("format", 0) == 0);
        m.addItem (2, "Bars . beats", true, setting ("format", 0) == 1);
        m.addItem (3, "Timecode", true, setting ("format", 0) == 2);
    }
    void settingChosen (int r) override { if (r >= 1 && r <= 3) set ("format", r - 1); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mouseWasDraggedSinceMouseDown()) return;
        juce::PopupMenu m;
        addSettings (m);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe = juce::Component::SafePointer<TimeBlock> (this)] (int r)
        {
            if (safe != nullptr && r > 0) safe->settingChosen (r);
        });
    }
    void refresh() override { repaint(); }

private:
    MainComponent& deck;
};

//==============================================================================
/** Transport keys: the ones chosen for this block. */
class TransportBlock : public Panel
{
public:
    explicit TransportBlock (MainComponent& d) : Panel ("transport", "Transport keys"), deck (d)
    {
        TransportKey* src[] = { &d.rtzKey, &d.rewKey, &d.ffwKey, &d.liftKey, &d.stopKey, &d.playKey, &d.recKey, &d.markKey, &d.clickKey };
        for (int i = 0; i < numKeys; ++i)
            keys.add (new MirrorKey (*src[i], src[i] == &d.recKey ? Colours::red : Colours::mustard));
        onPaint = [this] (juce::Graphics& g) { paintDeckFace (g, getLocalBounds()); };
        onLayout = [this] { layoutKeys(); };
    }
    void addSettings (juce::PopupMenu& m) override
    {
        static const char* names[] = { "RTZ", "<<  rewind", ">>  fast forward", "CUE  (lifter defeat)", "STOP", "PLAY", "REC", "MARK", "CLICK" };
        juce::PopupMenu k;
        for (int i = 0; i < numKeys; ++i) k.addItem (10 + i, names[i], true, shown (i));
        k.addSeparator();
        k.addItem (30, "All of them");
        k.addItem (31, "Just STOP / PLAY / REC");
        m.addSubMenu ("Keys", k);
    }
    void settingChosen (int r) override
    {
        int mask = setting ("keys", allKeys);
        if (r >= 10 && r < 10 + numKeys) { mask ^= 1 << (r - 10); if (mask == 0) mask = 1 << (r - 10); }
        else if (r == 30) mask = allKeys;
        else if (r == 31) mask = (1 << 4) | (1 << 5) | (1 << 6);
        set ("keys", mask);
    }
    void settingsChanged() override
    {
        int n = 0;
        for (int i = 0; i < numKeys; ++i) n += shown (i) ? 1 : 0;
        minW = juce::jmax (120, n * 52);
        minH = 44;
        maxH = 240;
        for (int i = 0; i < numKeys; ++i) { addChildComponent (keys[i]); keys[i]->setVisible (shown (i)); }
        layoutKeys();
    }
    void refresh() override { for (auto* k : keys) k->follow(); }

private:
    static constexpr int numKeys = 9, allKeys = (1 << 9) - 1;
    bool shown (int i) const { return (setting ("keys", allKeys) & (1 << i)) != 0; }
    void layoutKeys()
    {
        juce::Array<MirrorKey*> vis;
        for (int i = 0; i < numKeys; ++i) if (shown (i)) vis.add (keys[i]);
        if (vis.isEmpty()) return;
        auto r = getLocalBounds().reduced (8);
        const int gap = 6, w = (r.getWidth() - gap * (vis.size() - 1)) / vis.size();
        for (auto* k : vis) { k->setBounds (r.removeFromLeft (w)); r.removeFromLeft (gap); }
    }
    MainComponent& deck;
    juce::OwnedArray<MirrorKey> keys;
};

//==============================================================================
/** Speed: the IPS switch, VARI, the varispeed knob and its readouts. */
class VarispeedBlock : public Panel
{
public:
    explicit VarispeedBlock (MainComponent& d) : Panel ("varispeed", "Speed / varispeed"), deck (d)
    {
        minW = 300; minH = 70; maxH = 160;
        for (auto* k : { &d.ips7Key, &d.ips15Key, &d.ips30Key, &d.variKey })
            addAndMakeVisible (keys.add (new MirrorKey (*k, Colours::mustard)));
        for (auto* b : { &d.stDownButton, &d.stUpButton, &d.variResetButton })
        {
            auto* nb = buttons.add (new juce::TextButton (b->getButtonText(), b->getTooltip()));
            nb->setWantsKeyboardFocus (false);
            nb->onClick = [b] { b->triggerClick(); };
            addAndMakeVisible (nb);
        }
        for (auto* l : { &pct, &st })
        {
            l->setJustificationType (juce::Justification::centred);
            l->setFont (monoFont (13.0f, true));
            l->setColour (juce::Label::backgroundColourId, Colours::black);
            l->setColour (juce::Label::outlineColourId, Colours::edge);
            addAndMakeVisible (l);
        }
        knob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        knob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        knob.setRange (-TapeEngine::maxVarispeedPercent, TapeEngine::maxVarispeedPercent, 0.0);
        knob.setDoubleClickReturnValue (true, 0.0);
        knob.setMouseDragSensitivity (800);
        knob.setVelocityModeParameters (0.4, 1, 0.0, true, juce::ModifierKeys::shiftModifier);
        knob.setWantsKeyboardFocus (false);
        knob.setTooltip (d.variKnob.getTooltip());
        knob.onValueChange = [this] { if (deck.reel != nullptr) deck.changeSpeed (deck.reel->deckIps, true, knob.getValue(), false); };
        knob.onDragEnd = [this] { deck.saveReel(); };
        addAndMakeVisible (knob);
        onPaint = [this] (juce::Graphics& g) { paintDeckFace (g, getLocalBounds()); };
        onLayout = [this]
        {
            auto r = getLocalBounds().reduced (8, 6);
            r = r.withSizeKeepingCentre (r.getWidth(), juce::jmin (r.getHeight(), 90));
            const int ks = juce::jmin (r.getHeight(), 70);
            knob.setBounds (r.removeFromRight (ks).withSizeKeepingCentre (ks, ks));
            r.removeFromRight (6);
            auto row1 = r.removeFromTop (r.getHeight() / 2 - 2);
            r.removeFromTop (4);
            const int iw = (row1.getWidth() - 12) / 4;
            for (auto* k : keys) { k->setBounds (row1.removeFromLeft (iw)); row1.removeFromLeft (4); }
            auto row2 = r;
            const int w2 = row2.getWidth() - 16;
            const int wb = w2 * 17 / 100, wr = w2 * 13 / 100, wl = (w2 - 2 * wb - wr) / 2;
            buttons[0]->setBounds (row2.removeFromLeft (wb)); row2.removeFromLeft (4);
            pct.setBounds (row2.removeFromLeft (wl));        row2.removeFromLeft (4);
            st.setBounds (row2.removeFromLeft (wl));         row2.removeFromLeft (4);
            buttons[1]->setBounds (row2.removeFromLeft (wb)); row2.removeFromLeft (4);
            buttons[2]->setBounds (row2);
        };
    }
    void refresh() override
    {
        for (auto* k : keys) k->follow();
        for (int i = 0; i < 3; ++i) buttons[i]->setEnabled (deck.variResetButton.isEnabled());
        knob.setEnabled (deck.variKnob.isEnabled());
        if (! knob.isMouseButtonDown()) knob.setValue (deck.variKnob.getValue(), juce::dontSendNotification);
        pct.setText (deck.variPctLabel.getText(), juce::dontSendNotification);
        st.setText (deck.variStLabel.getText(), juce::dontSendNotification);
        for (auto* l : { &pct, &st }) l->setColour (juce::Label::textColourId, deck.variPctLabel.findColour (juce::Label::textColourId));
    }

private:
    MainComponent& deck;
    juce::OwnedArray<MirrorKey> keys;
    juce::OwnedArray<juce::TextButton> buttons;
    juce::Label pct, st;
    juce::Slider knob;
};

//==============================================================================
/** Tempo (drag / type / tap, time signature) and the sync source. */
class TempoBlock : public Panel
{
public:
    explicit TempoBlock (MainComponent& d) : Panel ("tempo", "Tempo and sync"), deck (d)
    {
        minW = 170; minH = 64; maxH = 160;
        tempo.getReel = d.tempoBox.getReel;
        tempo.onChanged = d.tempoBox.onChanged;
        addAndMakeVisible (tempo);
        sync.setWantsKeyboardFocus (false);
        sync.setTooltip (d.syncSourceButton.getTooltip());
        sync.onClick = [this] { deck.showSyncMenu(); };
        addAndMakeVisible (sync);
        onPaint = [this] (juce::Graphics& g) { paintDeckFace (g, getLocalBounds()); };
        onLayout = [this]
        {
            auto r = getLocalBounds().reduced (8, 6);
            tempo.setBounds (r.removeFromTop (r.getHeight() / 2 - 2));
            r.removeFromTop (4);
            sync.setBounds (r);
        };
    }
    void refresh() override
    {
        if (++frame % 5 != 0) return;
        if (sync.getButtonText() != deck.syncSourceButton.getButtonText()) sync.setButtonText (deck.syncSourceButton.getButtonText());
        for (auto id : { juce::TextButton::buttonColourId, juce::TextButton::textColourOffId })
            sync.setColour (id, deck.syncSourceButton.findColour (id));
        tempo.repaint();
    }

private:
    MainComponent& deck;
    TempoBox tempo;
    juce::TextButton sync;
    int frame = 0;
};

//==============================================================================
/** The master: its meters, which master, ARM and TAPE. */
class MasterBlock : public Panel
{
public:
    explicit MasterBlock (MainComponent& d) : Panel ("master", "Master"), deck (d), arm (d.masterArmKey, Colours::red), tape (d.monitorKey, Colours::mustard)
    {
        minW = 240; minH = 170;
        for (auto* m : { &l, &r }) { m->setMaster (true); addAndMakeVisible (m); }
        which.setWantsKeyboardFocus (false);
        which.onClick = [this] { deck.showMasterMenu(); };
        addAndMakeVisible (which);
        addAndMakeVisible (arm);
        addAndMakeVisible (tape);
        onPaint = [this] (juce::Graphics& g)
        {
            paintPanelFace (g, getLocalBounds(), "MASTER");
            const auto& ms = MachineSkin::currentMaster();
            g.setFont (juce::Font (juce::FontOptions ("Arial", 11.0f, juce::Font::bold)));
            g.setColour (Colours::textDim);
            g.drawText (juce::String (ms.brand) + "  " + ms.model, getLocalBounds().reduced (10, 4).withHeight (18), juce::Justification::centredRight);
        };
        onLayout = [this]
        {
            auto a = getLocalBounds().reduced (10, 6);
            a.removeFromTop (20);
            auto controls = a.removeFromBottom (26);
            a.removeFromBottom (6);
            which.setBounds (a.removeFromBottom (26));
            a.removeFromBottom (8);
            const int vuW = (a.getWidth() - 8) / 2;
            const int vuH = juce::jmin (a.getHeight(), (int) ((float) vuW / ChannelModule::meterAspect));
            auto vus = a.withSizeKeepingCentre (a.getWidth(), vuH);
            l.setBounds (vus.removeFromLeft (vuW));
            r.setBounds (vus.removeFromRight (vuW));
            arm.setBounds (controls.removeFromLeft ((controls.getWidth() - 6) / 2));
            controls.removeFromLeft (6);
            tape.setBounds (controls);
        };
    }
    void refresh() override
    {
        l.update (deck.frameMasterRms[0], deck.frameMasterPeak[0], deck.frameDt);
        r.update (deck.frameMasterRms[1], deck.frameMasterPeak[1], deck.frameDt);
        arm.follow();
        tape.follow();
        if (which.getButtonText() != deck.masterButton.getButtonText()) which.setButtonText (deck.masterButton.getButtonText());
        which.setEnabled (deck.masterButton.isEnabled());
    }
    void reelChanged() override { l.skinChanged(); r.skinChanged(); repaint(); }

private:
    MainComponent& deck;
    VUMeter l { "L" }, r { "R" };
    juce::TextButton which;
    MirrorKey arm, tape;
};

//==============================================================================
/** The autolocator: IN / OUT (with their times), AUTO, REH, LOOP. */
class LocatorBlock : public Panel
{
public:
    explicit LocatorBlock (MainComponent& d) : Panel ("locator", "Autolocator"), deck (d)
    {
        minW = 250; minH = 84; maxH = 220;
        for (auto* k : { &d.inKey, &d.outKey, &d.autoKey, &d.rehKey, &d.loopKey })
            addAndMakeVisible (keys.add (new MirrorKey (*k, k == &d.autoKey ? Colours::red : Colours::mustard)));
        onPaint = [this] (juce::Graphics& g)
        {
            paintPanelFace (g, getLocalBounds(), "AUTOLOCATOR");
            const double sr = deck.sessionRate();
            auto t = [&] (juce::int64 v) { return v >= 0 ? formatShortTime (v, sr) : juce::String ("--:--"); };
            g.setColour (Colours::text);
            g.setFont (monoFont (13.0f, true));
            g.drawText (t (deck.engine.getPunchIn()), inTime, juce::Justification::centredLeft);
            g.drawText (t (deck.engine.getPunchOut()), outTime, juce::Justification::centredLeft);
        };
        onLayout = [this]
        {
            auto a = getLocalBounds().reduced (10, 6);
            a.removeFromTop (20);
            a = a.withSizeKeepingCentre (a.getWidth(), juce::jmin (a.getHeight(), 100));
            auto row1 = a.removeFromTop (a.getHeight() / 2 - 3);
            a.removeFromTop (6);
            const int kw = juce::jmin (70, row1.getWidth() / 5);
            keys[0]->setBounds (row1.removeFromLeft (kw)); row1.removeFromLeft (6);
            inTime = row1.removeFromLeft ((row1.getWidth() - kw - 6) / 2);
            keys[1]->setBounds (row1.removeFromLeft (kw)); row1.removeFromLeft (6);
            outTime = row1;
            const int w3 = (a.getWidth() - 12) / 3;
            keys[2]->setBounds (a.removeFromLeft (w3)); a.removeFromLeft (6);
            keys[3]->setBounds (a.removeFromLeft (w3)); a.removeFromLeft (6);
            keys[4]->setBounds (a);
        };
    }
    void refresh() override
    {
        for (auto* k : keys) k->follow();
        if (++frame % 10 == 0) repaint();
    }

private:
    MainComponent& deck;
    juce::OwnedArray<MirrorKey> keys;
    juce::Rectangle<int> inTime, outTime;
    int frame = 0;
};

//==============================================================================
/** The markers list. */
class MarkersBlock : public Panel
{
public:
    explicit MarkersBlock (MainComponent& d) : Panel ("markers", "Markers"), deck (d)
    {
        minW = 220; minH = 140;
        markers.onLocate    = [this] (juce::int64 pos) { deck.locate (pos); };
        markers.onAddMarker = [this] { deck.addMarker(); };
        markers.onChanged   = [this] { deck.saveReel(); deck.markers.refresh(); };
        markers.getPlayhead = [this] { return deck.engine.getPosition(); };
        addAndMakeVisible (markers);
        onLayout = [this] { markers.setBounds (getLocalBounds()); };
        reelChanged();
    }
    void reelChanged() override { markers.setReel (deck.reel.get()); lastKey = {}; }
    void refresh() override
    {
        markers.setPlayhead (deck.engine.getPosition());
        if (++frame % 15 != 0 || deck.reel == nullptr) return;
        // markers added / changed elsewhere
        juce::String key;
        for (auto& m : deck.reel->markers) key << m.position << m.name << ";";
        if (key != lastKey) { lastKey = key; markers.refresh(); }
    }

private:
    MainComponent& deck;
    MarkerPanel markers;
    juce::String lastKey;
    int frame = 0;
};

//==============================================================================
std::unique_ptr<Panel> createDeckBlock (MainComponent& d, const juce::String& type)
{
    if (type == "meters")    return std::make_unique<MetersBlock> (d);
    if (type == "time")      return std::make_unique<TimeBlock> (d);
    if (type == "transport") return std::make_unique<TransportBlock> (d);
    if (type == "varispeed") return std::make_unique<VarispeedBlock> (d);
    if (type == "tempo")     return std::make_unique<TempoBlock> (d);
    if (type == "master")    return std::make_unique<MasterBlock> (d);
    if (type == "locator")   return std::make_unique<LocatorBlock> (d);
    if (type == "markers")   return std::make_unique<MarkersBlock> (d);
    return nullptr;
}

} // namespace hawthorne
