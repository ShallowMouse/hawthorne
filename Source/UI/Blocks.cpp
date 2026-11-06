#include "Blocks.h"
#include "MainComponent.h"
#include "MachineSkin.h"
#include "UIHelpers.h"
#include "Icons.h"

namespace hawthorne
{

/*  Blocks for the displays. They only show the deck - no controls: lamps and readouts in
    the machine's style. (Controlling is done on the main window.) */

// The deck's face behind a block, in the machine's style
static void paintDeckFace (juce::Graphics& g, juce::Rectangle<int> r)
{
    const auto& sk = MachineSkin::current();
    g.setGradientFill (juce::ColourGradient (sk.deck, 0.0f, (float) r.getY(), sk.deckLow, 0.0f, (float) r.getBottom(), false));
    g.fillRect (r);
}

static juce::Rectangle<int> paintLabel (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& label)
{
    const auto& sk = MachineSkin::current();
    g.setColour (sk.deckTextDim);
    g.setFont (monoFont (11.0f, true));
    auto top = r.removeFromTop (18);
    g.drawText (label, top.reduced (10, 0), juce::Justification::centredLeft);
    return r;
}

/** A lamp: a lit (or dark) label, like the keys' lamps on the deck. */
static void drawLamp (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, bool lit, juce::Colour colour, Icon icon = Icon::none)
{
    g.setColour (lit ? colour : juce::Colour (0xff1d1d1c));
    g.fillRoundedRectangle (r, 4.0f);
    if (lit)
    {
        g.setColour (colour.withAlpha (0.35f));
        g.drawRoundedRectangle (r.expanded (1.5f), 5.0f, 2.0f);
    }
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
    const auto ink = lit ? juce::Colours::black : juce::Colour (0xff6c6a64);
    if (icon != Icon::none)
    {
        const float sz = juce::jmin (r.getWidth(), r.getHeight()) * 0.62f;
        drawIcon (g, icon, r.withSizeKeepingCentre (sz, sz), ink);
        return;
    }
    g.setColour (ink);
    g.setFont (monoFont (juce::jlimit (10.0f, 30.0f, r.getHeight() * 0.42f), true));
    g.drawFittedText (text, r.toNearestInt().reduced (3, 0), juce::Justification::centred, 1, 0.6f);
}

/** A row of lamps filling an area (no taller than they need to be). */
static void drawLampRow (juce::Graphics& g, juce::Rectangle<float> r, const juce::StringArray& texts,
                         const juce::Array<bool>& lit, const juce::Array<juce::Colour>& colours, float maxH = 70.0f,
                         const juce::Array<Icon>& icons = {})
{
    if (texts.isEmpty()) return;
    r = r.withSizeKeepingCentre (r.getWidth(), juce::jmin (r.getHeight(), maxH));
    const float gap = 6.0f, w = (r.getWidth() - gap * (float) (texts.size() - 1)) / (float) texts.size();
    for (int i = 0; i < texts.size(); ++i)
    {
        drawLamp (g, r.removeFromLeft (w), texts[i], lit[i], colours[i], i < icons.size() ? icons[i] : Icon::none);
        r.removeFromLeft (gap);
    }
}

static void drawReadout (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, juce::Colour ink)
{
    g.setColour (juce::Colours::black);
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (ink);
    g.setFont (monoFont (juce::jlimit (11.0f, 40.0f, r.getHeight() * 0.55f), true));
    g.drawFittedText (text, r.toNearestInt().reduced (4, 0), juce::Justification::centred, 1, 0.6f);
}

//==============================================================================
/** Meters: just the VUs (with their record lamps) - which tracks, how many per row, VU / PPM, names. */
class MetersBlock : public Panel
{
public:
    explicit MetersBlock (MainComponent& d) : Panel ("meters", "Meters"), deck (d)
    {
        onPaint = [] (juce::Graphics& g) { g.fillAll (MachineSkin::current().bridge); };
        onLayout = [this] { layoutMeters(); };
    }

    void addSettings (juce::PopupMenu& m) override
    {
        juce::PopupMenu rows;
        for (int n : { 4, 6, 8, 12, 16, 24 })
            rows.addItem (10 + n, juce::String (n) + " per row", true, setting ("perRow", 8) == n);
        rows.addItem (10, "Fit (as big as they go)", true, setting ("perRow", 8) == 0);
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
        minW = per > 0 ? juce::jmax (200, per * 50) : 240;
        minH = 120;
        rebuild();
    }

    void reelChanged() override { rebuild(); }

    void refresh() override
    {
        const bool rec = deck.engine.isRecording();
        for (int i = 0; i < meters.size(); ++i)
        {
            const int t = tracks[i];
            const bool armed = deck.engine.isArmed (t);
            meters[i]->setRecLamp (! armed ? VUMeter::RecLamp::off : rec ? VUMeter::RecLamp::recording : VUMeter::RecLamp::armed, deck.frameFlash);
            if (juce::isPositiveAndBelow (t, 96))
                meters[i]->update (deck.frameRms[(size_t) t], deck.framePeak[(size_t) t], deck.frameDt);
        }
    }

private:
    int numTracks() const { return deck.reel != nullptr ? deck.reel->numTracks : 0; }
    int first() const     { return juce::jlimit (0, juce::jmax (0, numTracks() - 1), setting ("from", 1) - 1); }
    int last() const      { const int to = setting ("to", 0); return to <= 0 ? numTracks() - 1 : juce::jlimit (first(), juce::jmax (0, numTracks() - 1), to - 1); }

    void rebuild()
    {
        meters.clear();
        tracks.clear();
        if (deck.reel != nullptr)
            for (int t = first(); t <= last(); ++t)
            {
                auto* m = meters.add (new VUMeter (deck.reel->tracks[(size_t) t].name));
                m->setStyle (setting ("style", -1));
                m->showLabel (setting ("names", 1) != 0);
                m->setInterceptsMouseClicks (false, false);
                addAndMakeVisible (m);
                tracks.add (t);
            }
        layoutMeters();
    }

    void layoutMeters()
    {
        const int n = meters.size();
        if (n == 0 || getWidth() < 20) return;
        const float gap = 8.0f, pad = 10.0f, W = (float) getWidth() - 2 * pad, H = (float) getHeight() - 2 * pad;
        auto sizeFor = [&] (int per, float& mw, float& mh)
        {
            mw = (W - gap * (float) (per - 1)) / (float) per;
            mh = mw / ChannelModule::meterAspect;
            const int rows = (n + per - 1) / per;
            const float need = (float) rows * mh + gap * (float) (rows - 1);
            if (need > H)              // too tall: shrink to fit the height
            {
                mh = (H - gap * (float) (rows - 1)) / (float) rows;
                mw = mh * ChannelModule::meterAspect;
            }
        };
        int per = setting ("perRow", 8);
        float mw = 0, mh = 0;
        if (per <= 0)
        {
            // fit: the arrangement that gives the biggest meters
            float best = 0;
            for (int p = 1; p <= n; ++p)
            {
                float w, h;
                sizeFor (p, w, h);
                if (w > best) { best = w; per = p; }
            }
        }
        per = juce::jlimit (1, juce::jmax (1, n), per);
        sizeFor (per, mw, mh);
        const int rows = (n + per - 1) / per;
        const float totalH = (float) rows * mh + gap * (float) (rows - 1);
        const float y0 = pad + (H - totalH) * 0.5f;
        for (int i = 0; i < n; ++i)
        {
            const int row = i / per, col = i % per;
            const int inRow = juce::jmin (per, n - row * per);
            const float rowW = (float) inRow * mw + gap * (float) (inRow - 1);
            const float x0 = pad + (W - rowW) * 0.5f;
            meters[i]->setBounds (juce::Rectangle<float> (x0 + (float) col * (mw + gap), y0 + (float) row * (mh + gap), mw, mh).toNearestInt());
        }
    }

    MainComponent& deck;
    juce::OwnedArray<VUMeter> meters;
    juce::Array<int> tracks;
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
    }
    void addSettings (juce::PopupMenu& m) override
    {
        m.addItem (1, "Minutes : seconds", true, setting ("format", 0) == 0);
        m.addItem (2, "Bars . beats", true, setting ("format", 0) == 1);
        m.addItem (3, "Timecode", true, setting ("format", 0) == 2);
    }
    void settingChosen (int r) override { if (r >= 1 && r <= 3) set ("format", r - 1); }
    void refresh() override { repaint(); }

private:
    MainComponent& deck;
};

//==============================================================================
/** Transport: lamps for what the tape is doing (the ones chosen for this block). */
class TransportBlock : public Panel
{
public:
    explicit TransportBlock (MainComponent& d) : Panel ("transport", "Transport"), deck (d)
    {
        onPaint = [this] (juce::Graphics& g)
        {
            paintDeckFace (g, getLocalBounds());
            static const char* names[] = { "REW", "F FWD", "STOP", "PLAY", "REC", "CUE", "CLICK" };
            const auto& e = deck.engine;
            const int wd = e.getWindDirection();
            const bool lit[] = { wd < 0, wd > 0, ! e.isPlaying() && wd == 0, e.isPlaying(), e.isRecording(), e.getLifterDefeat(),
                                 deck.reel != nullptr && deck.reel->clickOn && deck.reel->tempoBpm > 0.0 };
            static const Icon icons[] = { Icon::rew, Icon::ffw, Icon::stop, Icon::play, Icon::rec, Icon::cue, Icon::click };
            juce::StringArray t;
            juce::Array<bool> l;
            juce::Array<juce::Colour> c;
            juce::Array<Icon> ic;
            for (int i = 0; i < numLamps; ++i)
                if (shown (i))
                {
                    t.add (names[i]);
                    ic.add (icons[i]);
                    l.add (lit[i]);
                    c.add (i == 4 ? Colours::red : i == 3 ? juce::Colour (0xff5fcf6a) : Colours::mustard);
                }
            drawLampRow (g, getLocalBounds().toFloat().reduced (8.0f), t, l, c, 110.0f, ic);
        };
    }
    void addSettings (juce::PopupMenu& m) override
    {
        static const char* names[] = { "REW", "F FWD", "STOP", "PLAY", "REC", "CUE (lifter defeat)", "CLICK" };
        juce::PopupMenu k;
        for (int i = 0; i < numLamps; ++i) k.addItem (10 + i, names[i], true, shown (i));
        k.addSeparator();
        k.addItem (30, "All of them");
        k.addItem (31, "Just STOP / PLAY / REC");
        m.addSubMenu ("Lamps", k);
    }
    void settingChosen (int r) override
    {
        int mask = setting ("lamps", allLamps);
        if (r >= 10 && r < 10 + numLamps) { mask ^= 1 << (r - 10); if (mask == 0) mask = 1 << (r - 10); }
        else if (r == 30) mask = allLamps;
        else if (r == 31) mask = (1 << 2) | (1 << 3) | (1 << 4);
        set ("lamps", mask);
    }
    void settingsChanged() override
    {
        int n = 0;
        for (int i = 0; i < numLamps; ++i) n += shown (i) ? 1 : 0;
        minW = juce::jmax (100, n * 48);
        minH = 40;
        repaint();
    }
    void refresh() override { if (++frame % 2 == 0) repaint(); }

private:
    static constexpr int numLamps = 7, allLamps = (1 << 7) - 1;
    bool shown (int i) const { return (setting ("lamps", allLamps) & (1 << i)) != 0; }
    MainComponent& deck;
    int frame = 0;
};

//==============================================================================
/** Speed: which IPS, VARI, and the varispeed in % and semitones. */
class VarispeedBlock : public Panel
{
public:
    explicit VarispeedBlock (MainComponent& d) : Panel ("varispeed", "Speed / varispeed"), deck (d)
    {
        minW = 260; minH = 64;
        onPaint = [this] (juce::Graphics& g)
        {
            paintDeckFace (g, getLocalBounds());
            auto r = paintLabel (g, getLocalBounds(), "SPEED").toFloat().reduced (8.0f, 4.0f);
            r = r.withSizeKeepingCentre (r.getWidth(), juce::jmin (r.getHeight(), 120.0f));
            const auto* reel = deck.reel.get();
            const double ips = reel != nullptr ? reel->deckIps : 15.0;
            const bool vari = reel != nullptr && reel->variOn;
            auto row1 = r.removeFromTop (r.getHeight() * 0.5f - 3.0f);
            r.removeFromTop (6.0f);
            drawLampRow (g, row1, { juce::String (juce::CharPointer_UTF8 ("7\xc2\xbd")), "15", "30", "VARI" },
                         { reel != nullptr && ips == 7.5, reel != nullptr && ips == 15.0, reel != nullptr && ips == 30.0, vari },
                         { Colours::mustard, Colours::mustard, Colours::mustard, Colours::mustard }, 60.0f,
                         { Icon::none, Icon::none, Icon::none, Icon::vari });
            const auto ink = vari ? Colours::mustard : Colours::textDim;
            auto row2 = r.withSizeKeepingCentre (r.getWidth(), juce::jmin (r.getHeight(), 60.0f));
            drawReadout (g, row2.removeFromLeft (row2.getWidth() * 0.5f - 3.0f), deck.variPctLabel.getText(), ink);
            row2.removeFromLeft (6.0f);
            drawReadout (g, row2, deck.variStLabel.getText(), ink);
        };
    }
    void refresh() override { if (++frame % 6 == 0) repaint(); }

private:
    MainComponent& deck;
    int frame = 0;
};

//==============================================================================
/** Tempo (BPM, time signature) and where the time comes from (sync). */
class TempoBlock : public Panel
{
public:
    explicit TempoBlock (MainComponent& d) : Panel ("tempo", "Tempo and sync"), deck (d)
    {
        minW = 170; minH = 64;
        onPaint = [this] (juce::Graphics& g)
        {
            paintDeckFace (g, getLocalBounds());
            auto r = getLocalBounds().toFloat().reduced (8.0f, 6.0f);
            r = r.withSizeKeepingCentre (r.getWidth(), juce::jmin (r.getHeight(), 120.0f));
            const auto* reel = deck.reel.get();
            const auto& sk = MachineSkin::current();
            auto top = r.removeFromTop (r.getHeight() * 0.5f - 3.0f);
            r.removeFromTop (6.0f);
            const bool has = reel != nullptr && reel->tempoBpm > 0.0;
            auto sig = top.removeFromRight (top.getHeight() * 1.4f);
            top.removeFromRight (6.0f);
            drawReadout (g, top, has ? juce::String (reel->tempoBpm, 2) + " BPM" : juce::String ("NO TEMPO"), sk.counterInk);
            drawReadout (g, sig, reel != nullptr ? juce::String (reel->beatsPerBar) + "/4" : juce::String ("-"), sk.counterInk);
            const auto& b = deck.syncSourceButton;
            const auto col = b.findColour (juce::TextButton::buttonColourId);
            drawLamp (g, r, b.getButtonText(), col == Colours::mustard, Colours::mustard);
        };
    }
    void refresh() override { if (++frame % 6 == 0) repaint(); }

private:
    MainComponent& deck;
    int frame = 0;
};

//==============================================================================
/** The master: its meters, which master, and lamps for ARMED and TAPE. */
class MasterBlock : public Panel
{
public:
    explicit MasterBlock (MainComponent& d) : Panel ("master", "Master"), deck (d)
    {
        minW = 150; minH = 70;
        for (auto* m : { &l, &r }) { m->setMaster (true); m->setInterceptsMouseClicks (false, false); addAndMakeVisible (m); }
        onPaint = [this] (juce::Graphics& g)
        {
            g.fillAll (Colours::panel);
            if (! compact())
            {
                paintLabel (g, getLocalBounds(), "MASTER");
                const auto& ms = MachineSkin::currentMaster();
                g.setFont (juce::Font (juce::FontOptions ("Arial", 11.0f, juce::Font::bold)));
                g.setColour (Colours::textDim);
                g.drawText (juce::String (ms.brand) + "  " + ms.model, getLocalBounds().reduced (10, 0).withHeight (18), juce::Justification::centredRight);
            }
            g.setColour (Colours::text);
            g.setFont (monoFont (compact() ? 11.0f : 13.0f, true));
            g.drawText (deck.masterButton.getButtonText().upToFirstOccurrenceOf (juce::String (juce::CharPointer_UTF8 ("  \xe2\x96\xbe")), false, false),
                        nameArea, juce::Justification::centred, true);
            // TAPE: a small reel by the name, lit while you're hearing the master tape
            const bool tape = deck.engine.isMonitoringMaster();
            const auto tr = nameArea.toFloat().removeFromRight (22.0f).withSizeKeepingCentre (20.0f, 20.0f);
            drawIcon (g, Icon::tape, tr, tape ? Colours::mustard : Colours::edge.brighter (0.15f));
            if (tape) { g.setColour (Colours::mustard.withAlpha (0.18f)); g.fillEllipse (tr.expanded (3.0f)); }
        };
        onLayout = [this]
        {
            // (small: no title, just the meters and the master's name)
            auto a = getLocalBounds().reduced (compact() ? 6 : 10, compact() ? 3 : 6);
            if (! compact()) a.removeFromTop (16);
            nameArea = a.removeFromBottom (compact() ? 16 : 22);
            a.removeFromBottom (compact() ? 2 : 4);
            const int vuW = (a.getWidth() - 8) / 2;
            const int vuH = juce::jmin (a.getHeight(), (int) ((float) vuW / ChannelModule::meterAspect));
            const int w = (int) ((float) vuH * ChannelModule::meterAspect);
            auto vus = a.withSizeKeepingCentre (w * 2 + 8, vuH);
            l.setBounds (vus.removeFromLeft (w));
            r.setBounds (vus.removeFromRight (w));
        };
    }
    void refresh() override
    {
        l.update (deck.frameMasterRms[0], deck.frameMasterPeak[0], deck.frameDt);
        r.update (deck.frameMasterRms[1], deck.frameMasterPeak[1], deck.frameDt);
        // armed / recording: the meters' own lamps
        const auto lamp = ! deck.engine.isMasterArmed() ? VUMeter::RecLamp::off
                        : deck.engine.isRecording() ? VUMeter::RecLamp::recording : VUMeter::RecLamp::armed;
        l.setRecLamp (lamp, deck.frameFlash);
        r.setRecLamp (lamp, deck.frameFlash);
        if (++frame % 6 == 0) repaint (nameArea);
    }
    void reelChanged() override { l.skinChanged(); r.skinChanged(); repaint(); }

private:
    bool compact() const { return getHeight() < 140; }
    MainComponent& deck;
    VUMeter l { "L" }, r { "R" };
    juce::Rectangle<int> nameArea;
    int frame = 0;
};

//==============================================================================
/** The autolocator: IN / OUT times, and lamps for AUTO, REH, LOOP. */
class LocatorBlock : public Panel
{
public:
    explicit LocatorBlock (MainComponent& d) : Panel ("locator", "Autolocator"), deck (d)
    {
        minW = 220; minH = 80;
        onPaint = [this] (juce::Graphics& g)
        {
            g.fillAll (Colours::panel);
            auto r = paintLabel (g, getLocalBounds(), "AUTOLOCATOR").toFloat().reduced (10.0f, 4.0f);
            r = r.withSizeKeepingCentre (r.getWidth(), juce::jmin (r.getHeight(), 130.0f));
            const auto& e = deck.engine;
            const double sr = deck.sessionRate();
            auto t = [&] (juce::int64 v) { return v >= 0 ? formatShortTime (v, sr) : juce::String ("--:--"); };
            auto row1 = r.removeFromTop (r.getHeight() * 0.5f - 3.0f);
            r.removeFromTop (6.0f);
            const auto ink = MachineSkin::current().counterInk;
            drawReadout (g, row1.removeFromLeft (row1.getWidth() * 0.5f - 3.0f), "IN  " + t (e.getPunchIn()), ink);
            row1.removeFromLeft (6.0f);
            drawReadout (g, row1, "OUT  " + t (e.getPunchOut()), ink);
            drawLampRow (g, r, { "AUTO", "REH", "LOOP" },
                         { e.getAutoPunch() && (! e.isAutoPunching() || deck.frameFlash), e.getRehearse(), e.getCycle() },
                         { Colours::red, Colours::mustard, Colours::mustard }, 60.0f,
                         { Icon::autoPunch, Icon::rehearse, Icon::loop });
        };
    }
    void refresh() override { if (++frame % 6 == 0) repaint(); }

private:
    MainComponent& deck;
    int frame = 0;
};

//==============================================================================
/** The markers list (the one at the playhead highlighted). */
class MarkersBlock : public Panel
{
public:
    explicit MarkersBlock (MainComponent& d) : Panel ("markers", "Markers"), deck (d)
    {
        minW = 200; minH = 120;
        markers.getPlayhead = [this] { return deck.engine.getPosition(); };
        markers.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (markers);
        onLayout = [this] { markers.setBounds (getLocalBounds()); };
        reelChanged();
    }
    void reelChanged() override { markers.setReel (deck.reel.get()); lastKey = {}; }
    void refresh() override
    {
        markers.setPlayhead (deck.engine.getPosition());
        if (++frame % 15 != 0 || deck.reel == nullptr) return;
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
