#include "Blocks.h"
#include "MainComponent.h"
#include "MachineSkin.h"
#include "UIHelpers.h"
#include "Icons.h"

namespace hawthorne
{

/*  Blocks for the displays: an information board in the style of the machine on the reel -
    its deck colours, its counter's readouts (red LED, green VFD, amber...), its meters. They
    only show the deck: big readouts, plain labels, status lights that light up when something
    is on. (Controlling is done on the main window.) */

//==============================================================================
// The machine's look, for an information board

static const MachineSkin& skin() { return MachineSkin::current(); }

static void paintFace (juce::Graphics& g, juce::Rectangle<int> r)
{
    const auto& sk = skin();
    g.setGradientFill (juce::ColourGradient (sk.deck, 0.0f, (float) r.getY(), sk.deckLow, 0.0f, (float) r.getBottom(), false));
    g.fillRect (r);
    if (sk.brushed)
    {
        // a hint of brushed metal
        juce::Random rnd (7);
        for (int y = r.getY(); y < r.getBottom(); y += 2)
        {
            g.setColour (juce::Colours::white.withAlpha (0.012f + 0.02f * rnd.nextFloat()));
            g.drawHorizontalLine (y, (float) r.getX(), (float) r.getRight());
        }
    }
    // a fine engraved edge between panels
    g.setColour (sk.deckEdge.withAlpha (0.6f));
    g.drawRect (r, 1);
}

/** A small engraved caption (the panel's name, a readout's name). */
static void caption (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text,
                     juce::Justification j = juce::Justification::centredLeft)
{
    g.setColour (skin().deckTextDim);
    g.setFont (monoFont (juce::jlimit (9.0f, 15.0f, r.getHeight() * 0.8f), true).withExtraKerningFactor (0.12f));
    g.drawText (text, r, j, true);
}

static bool isDrum() { return skin().counter == MachineSkin::Counter::drum; }

/** A readout window in the machine's counter style. */
static void readout (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, bool dim = false,
                     juce::Colour inkOverride = {}, juce::Justification j = juce::Justification::centred)
{
    const auto& sk = skin();
    const auto bg = isDrum() ? juce::Colour (0xff141414) : sk.counterBg;
    g.setColour (bg);
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawRoundedRectangle (r, 3.0f, 1.0f);
    auto ink = inkOverride.isTransparent() ? (isDrum() ? juce::Colour (0xfff2efe6) : sk.counterInk) : inkOverride;
    if (dim) ink = ink.withMultipliedAlpha (0.28f);
    // a soft glow on lit displays (not on drums)
    g.setFont (monoFont (juce::jmin (r.getHeight() * 0.62f, 90.0f), true));
    if (! isDrum() && ! dim)
    {
        g.setColour (ink.withAlpha (0.18f));
        g.drawFittedText (text, r.toNearestInt().reduced (6, 0).translated (0, 1), j, 1, 0.5f);
    }
    g.setColour (ink);
    g.drawFittedText (text, r.toNearestInt().reduced (6, 0), j, 1, 0.5f);
}

/** A status light: a word that lights up (in the counter's colour, or its own) when it's on. */
static void statusLight (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, bool on,
                         juce::Colour onColour = {}, Icon icon = Icon::none)
{
    const auto& sk = skin();
    const auto c = onColour.isTransparent() ? (isDrum() ? Colours::mustard : sk.counterInk) : onColour;
    const auto lamp = r.removeFromLeft (juce::jmin (r.getHeight(), 18.0f)).withSizeKeepingCentre (9.0f, 9.0f);
    g.setColour (on ? c : sk.deckTextDim.withAlpha (0.25f));
    g.fillEllipse (lamp);
    if (on) { g.setColour (c.withAlpha (0.3f)); g.fillEllipse (lamp.expanded (3.0f)); }
    r.removeFromLeft (6.0f);
    if (icon != Icon::none)
    {
        const float sz = juce::jmin (r.getHeight(), 18.0f);
        drawIcon (g, icon, r.removeFromLeft (sz).withSizeKeepingCentre (sz, sz), on ? sk.deckText : sk.deckTextDim.withAlpha (0.5f));
        r.removeFromLeft (4.0f);
    }
    g.setColour (on ? sk.deckText : sk.deckTextDim.withAlpha (0.55f));
    g.setFont (monoFont (juce::jlimit (10.0f, 16.0f, r.getHeight() * 0.7f), true));
    g.drawText (text, r, juce::Justification::centredLeft, true);
}

/** The panel's area under its caption. */
static juce::Rectangle<float> body (juce::Graphics& g, juce::Rectangle<int> b, const juce::String& name)
{
    auto r = b.toFloat().reduced (10.0f, 6.0f);
    const float capH = juce::jlimit (12.0f, 18.0f, r.getHeight() * 0.14f);
    caption (g, r.removeFromTop (capH), name);
    r.removeFromTop (4.0f);
    return r;
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
        minW = 140; minH = 40;
        onPaint = [this] (juce::Graphics& g)
        {
            paintFace (g, getLocalBounds());
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
/** Transport: what the tape is doing, big - PLAY, RECORD, STOP, REWIND, FAST FWD - with
    status lights for CUE, CLICK, LOOP and AUTO PUNCH. */
class TransportBlock : public Panel
{
public:
    explicit TransportBlock (MainComponent& d) : Panel ("transport", "Transport"), deck (d)
    {
        minW = 220; minH = 70;
        onPaint = [this] (juce::Graphics& g)
        {
            paintFace (g, getLocalBounds());
            auto r = body (g, getLocalBounds(), "TRANSPORT");
            const auto& e = deck.engine;
            const int wd = e.getWindDirection();
            juce::String word = "STOP";
            Icon icon = Icon::stop;
            juce::Colour ink;
            if (e.isRecording())     { word = "RECORD"; icon = Icon::rec; ink = Colours::red; }
            else if (wd < 0)         { word = "REWIND"; icon = Icon::rew; }
            else if (wd > 0)         { word = "FAST FWD"; icon = Icon::ffw; }
            else if (e.isPlaying())  { word = "PLAY"; icon = Icon::play; }

            const bool lights = r.getHeight() > 70.0f;
            auto main = lights ? r.removeFromTop (r.getHeight() * 0.68f) : r;
            // the big status: an icon and the word, in a readout
            readout (g, main, {}, false, ink);
            const auto& sk = skin();
            const auto col = ink.isTransparent() ? (isDrum() ? juce::Colour (0xfff2efe6) : sk.counterInk) : ink;
            auto inner = main.reduced (10.0f, main.getHeight() * 0.16f);
            const float isz = inner.getHeight() * 0.8f;
            g.setFont (monoFont (juce::jmin (inner.getHeight() * 0.85f, 90.0f), true));
            const float tw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), word);
            const float total = juce::jmin (inner.getWidth(), isz + 14.0f + tw);
            auto line = inner.withSizeKeepingCentre (total, inner.getHeight());
            drawIcon (g, icon, line.removeFromLeft (isz).withSizeKeepingCentre (isz, isz), col);
            line.removeFromLeft (14.0f);
            g.setColour (col);
            g.drawFittedText (word, line.toNearestInt(), juce::Justification::centredLeft, 1, 0.5f);

            if (lights)
            {
                r.removeFromTop (6.0f);
                const auto* reel = deck.reel.get();
                const bool click = reel != nullptr && reel->clickOn && reel->tempoBpm > 0.0;
                const float w = r.getWidth() / 4.0f;
                statusLight (g, r.removeFromLeft (w), "CUE", e.getLifterDefeat(), {}, Icon::cue);
                statusLight (g, r.removeFromLeft (w), "CLICK", click, {}, Icon::click);
                statusLight (g, r.removeFromLeft (w), "LOOP", e.getCycle(), {}, Icon::loop);
                statusLight (g, r, "AUTO", e.getAutoPunch(), Colours::red, Icon::autoPunch);
            }
        };
    }
    void refresh() override { if (++frame % 2 == 0) repaint(); }

private:
    MainComponent& deck;
    int frame = 0;
};

//==============================================================================
/** Speed: the tape speed big, and the varispeed (in % and semitones) when it's on. */
class VarispeedBlock : public Panel
{
public:
    explicit VarispeedBlock (MainComponent& d) : Panel ("varispeed", "Speed / varispeed"), deck (d)
    {
        minW = 200; minH = 64;
        onPaint = [this] (juce::Graphics& g)
        {
            paintFace (g, getLocalBounds());
            auto r = body (g, getLocalBounds(), "TAPE SPEED");
            const auto* reel = deck.reel.get();
            const bool vari = reel != nullptr && reel->variOn;
            const double ips = reel != nullptr ? reel->deckIps : 15.0;
            const auto ipsText = (ips == 7.5 ? juce::String (juce::CharPointer_UTF8 ("7\xc2\xbd")) : juce::String ((int) ips)) + " IPS";
            const bool wide = r.getWidth() > r.getHeight() * 3.2f;
            auto a = wide ? r.removeFromLeft (r.getWidth() * 0.42f) : r.removeFromTop (r.getHeight() * 0.5f - 3.0f);
            if (wide) r.removeFromLeft (8.0f); else r.removeFromTop (6.0f);
            readout (g, a, reel != nullptr ? ipsText : juce::String ("--"));
            // the varispeed: dim when it's off
            auto pct = r.removeFromLeft (r.getWidth() * 0.5f - 3.0f);
            r.removeFromLeft (6.0f);
            readout (g, pct, deck.variPctLabel.getText(), ! vari);
            readout (g, r, deck.variStLabel.getText(), ! vari);
        };
    }
    void refresh() override { if (++frame % 6 == 0) repaint(); }

private:
    MainComponent& deck;
    int frame = 0;
};

//==============================================================================
/** Tempo: the BPM big, the time signature, and where the time comes from. */
class TempoBlock : public Panel
{
public:
    explicit TempoBlock (MainComponent& d) : Panel ("tempo", "Tempo and sync"), deck (d)
    {
        minW = 170; minH = 64;
        onPaint = [this] (juce::Graphics& g)
        {
            paintFace (g, getLocalBounds());
            auto r = body (g, getLocalBounds(), "TEMPO");
            const auto* reel = deck.reel.get();
            const bool has = reel != nullptr && reel->tempoBpm > 0.0;
            const bool sync = r.getHeight() > 60.0f;
            auto top = sync ? r.removeFromTop (r.getHeight() * 0.66f) : r;
            auto sig = top.removeFromRight (juce::jmin (top.getWidth() * 0.3f, top.getHeight() * 1.6f));
            top.removeFromRight (6.0f);
            readout (g, top, has ? juce::String (reel->tempoBpm, 2) : juce::String ("---.--"), ! has);
            readout (g, sig, reel != nullptr ? juce::String (reel->beatsPerBar) + "/4" : juce::String ("-"), ! has);
            if (sync)
            {
                r.removeFromTop (6.0f);
                const auto& b = deck.syncSourceButton;
                const bool external = b.findColour (juce::TextButton::buttonColourId) == Colours::mustard;
                statusLight (g, r, b.getButtonText(), external, {}, Icon::timecode);
            }
        };
    }
    void refresh() override { if (++frame % 6 == 0) repaint(); }

private:
    MainComponent& deck;
    int frame = 0;
};

//==============================================================================
/** The master: its meters (their lamps show armed / recording), which master, and a small
    reel lit while you're hearing the master tape. */
class MasterBlock : public Panel
{
public:
    explicit MasterBlock (MainComponent& d) : Panel ("master", "Master"), deck (d)
    {
        minW = 150; minH = 70;
        for (auto* m : { &l, &r }) { m->setMaster (true); m->setInterceptsMouseClicks (false, false); addAndMakeVisible (m); }
        onPaint = [this] (juce::Graphics& g)
        {
            paintFace (g, getLocalBounds());
            if (! compact())
            {
                const auto& ms = MachineSkin::currentMaster();
                auto top = getLocalBounds().toFloat().reduced (10.0f, 6.0f).removeFromTop (16.0f);
                caption (g, top, "MASTER");
                caption (g, top, juce::String (ms.brand).toUpperCase() + "  " + juce::String (ms.model).toUpperCase(), juce::Justification::centredRight);
            }
            const auto& sk = skin();
            g.setColour (sk.deckText);
            g.setFont (monoFont (compact() ? 11.0f : 13.0f, true));
            g.drawText (deck.masterButton.getButtonText().upToFirstOccurrenceOf (juce::String (juce::CharPointer_UTF8 ("  \xe2\x96\xbe")), false, false),
                        nameArea, juce::Justification::centred, true);
            const bool tape = deck.engine.isMonitoringMaster();
            const auto tr = nameArea.toFloat().removeFromRight (22.0f).withSizeKeepingCentre (20.0f, 20.0f);
            if (tape) { g.setColour (sk.counterInk.withAlpha (0.2f)); g.fillEllipse (tr.expanded (3.0f)); }
            drawIcon (g, Icon::tape, tr, tape ? (isDrum() ? Colours::mustard : sk.counterInk) : sk.deckTextDim.withAlpha (0.35f));
        };
        onLayout = [this]
        {
            auto a = getLocalBounds().reduced (compact() ? 6 : 10, compact() ? 3 : 6);
            if (! compact()) a.removeFromTop (18);
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
/** The autolocator: IN, OUT and the length between them, with lights for AUTO, REHEARSE
    and LOOP. */
class LocatorBlock : public Panel
{
public:
    explicit LocatorBlock (MainComponent& d) : Panel ("locator", "Autolocator"), deck (d)
    {
        minW = 220; minH = 80;
        onPaint = [this] (juce::Graphics& g)
        {
            paintFace (g, getLocalBounds());
            auto r = body (g, getLocalBounds(), "AUTOLOCATOR");
            const auto& e = deck.engine;
            const double sr = deck.sessionRate();
            const auto in = e.getPunchIn(), out = e.getPunchOut();
            auto t = [&] (juce::int64 v) { return v >= 0 ? formatShortTime (v, sr) : juce::String ("--:--"); };
            // the locate memories along the bottom (lit: holds a place; 1-8 goes there, Ctrl stores)
            if (r.getHeight() > 100.0f && deck.reel != nullptr)
            {
                auto strip = r.removeFromBottom (18.0f);
                r.removeFromBottom (6.0f);
                const auto& sk = skin();
                const float cw = strip.getWidth() / (float) Reel::numLocates;
                for (int k = 0; k < Reel::numLocates; ++k)
                {
                    const auto cell = strip.removeFromLeft (cw).reduced (2.0f, 0.0f);
                    const bool held = deck.reel->locates[(size_t) k] >= 0;
                    g.setColour (held ? Colours::mustard.withAlpha (0.85f) : sk.deckEdge.withAlpha (0.8f));
                    if (held) g.fillRoundedRectangle (cell, 3.0f); else g.drawRoundedRectangle (cell, 3.0f, 1.0f);
                    g.setColour (held ? juce::Colours::black : sk.deckTextDim);
                    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
                    g.drawText ("LOC " + juce::String (k + 1), cell, juce::Justification::centred);
                }
            }
            const bool lights = r.getHeight() > 64.0f;
            auto rows = lights ? r.removeFromTop (r.getHeight() * 0.7f) : r;
            const float capH = juce::jlimit (10.0f, 14.0f, rows.getHeight() * 0.22f);
            const float w = (rows.getWidth() - 12.0f) / 3.0f;
            auto cell = [&] (const juce::String& name, const juce::String& value, bool dim)
            {
                auto c = rows.removeFromLeft (w);
                rows.removeFromLeft (6.0f);
                caption (g, c.removeFromTop (capH), name, juce::Justification::centred);
                c.removeFromTop (2.0f);
                readout (g, c, value, dim);
            };
            cell ("IN", t (in), in < 0);
            cell ("OUT", t (out), out < 0);
            cell ("LENGTH", in >= 0 && out > in ? formatShortTime (out - in, sr) : juce::String ("--:--"), ! (in >= 0 && out > in));
            if (lights)
            {
                r.removeFromTop (6.0f);
                const float lw = r.getWidth() / 3.0f;
                statusLight (g, r.removeFromLeft (lw), "AUTO", e.getAutoPunch() && (! e.isAutoPunching() || deck.frameFlash), Colours::red, Icon::autoPunch);
                statusLight (g, r.removeFromLeft (lw), "REHEARSE", e.getRehearse(), {}, Icon::rehearse);
                statusLight (g, r, "LOOP", e.getCycle(), {}, Icon::loop);
            }
        };
    }
    void refresh() override { if (++frame % 6 == 0) repaint(); }

private:
    MainComponent& deck;
    int frame = 0;
};

//==============================================================================
/** Markers: where you are (the last marker passed), what's next and how far, and the list. */
class MarkersBlock : public Panel
{
public:
    explicit MarkersBlock (MainComponent& d) : Panel ("markers", "Markers"), deck (d)
    {
        minW = 200; minH = 80;
        onPaint = [this] (juce::Graphics& g)
        {
            paintFace (g, getLocalBounds());
            auto r = body (g, getLocalBounds(), "MARKERS");
            const auto* reel = deck.reel.get();
            if (reel == nullptr) return;
            const double sr = deck.sessionRate();
            const auto pos = deck.engine.getPosition();
            std::vector<Marker> ms (reel->markers.begin(), reel->markers.end());
            std::sort (ms.begin(), ms.end(), [] (auto& a, auto& b) { return a.position < b.position; });
            int cur = -1;
            for (int i = 0; i < (int) ms.size(); ++i) if (ms[(size_t) i].position <= pos) cur = i;
            const int next = cur + 1 < (int) ms.size() ? cur + 1 : -1;

            // now / next, big
            const float topH = juce::jmin (r.getHeight(), juce::jmax (40.0f, r.getHeight() * 0.42f));
            auto top = r.removeFromTop (topH);
            const float capH = juce::jlimit (10.0f, 14.0f, top.getHeight() * 0.22f);
            auto nowA = top.removeFromLeft (top.getWidth() * 0.55f - 3.0f);
            top.removeFromLeft (6.0f);
            caption (g, nowA.removeFromTop (capH), "NOW");
            readout (g, nowA.withTrimmedTop (2.0f), cur >= 0 ? ms[(size_t) cur].name.toUpperCase() : juce::String ("-"), cur < 0,
                     {}, juce::Justification::centredLeft);
            caption (g, top.removeFromTop (capH), next >= 0 ? "NEXT  IN " + formatShortTime (ms[(size_t) next].position - pos, sr) : juce::String ("NEXT"));
            readout (g, top.withTrimmedTop (2.0f), next >= 0 ? ms[(size_t) next].name.toUpperCase() : juce::String ("-"), next < 0,
                     {}, juce::Justification::centredLeft);

            // the list
            r.removeFromTop (8.0f);
            const float rowH = juce::jlimit (16.0f, 26.0f, r.getHeight() / juce::jmax (1.0f, (float) ms.size()));
            const int rowsFit = (int) (r.getHeight() / rowH);
            const int first = juce::jlimit (0, juce::jmax (0, (int) ms.size() - rowsFit), cur - rowsFit / 2);
            const auto& sk = skin();
            for (int i = first; i < (int) ms.size() && r.getHeight() >= rowH; ++i)
            {
                auto row = r.removeFromTop (rowH);
                if (i == cur)
                {
                    g.setColour (sk.counterInk.withAlpha (0.14f));
                    g.fillRoundedRectangle (row, 3.0f);
                }
                g.setColour (i == cur ? sk.deckText : sk.deckTextDim);
                g.setFont (monoFont (rowH * 0.6f, i == cur));
                g.drawText (formatShortTime (ms[(size_t) i].position, sr), row.removeFromLeft (rowH * 4.0f).reduced (6.0f, 0.0f), juce::Justification::centredLeft);
                g.drawText (ms[(size_t) i].name, row, juce::Justification::centredLeft, true);
            }
        };
    }
    void refresh() override { if (++frame % 6 == 0) repaint(); }

private:
    MainComponent& deck;
    int frame = 0;
};

//==============================================================================
/** The reel: session and reel, machine, tape, speed, noise reduction, tracks - and which
    tracks are armed. */
class ReelBlock : public Panel
{
public:
    explicit ReelBlock (MainComponent& d) : Panel ("reel", "Reel"), deck (d)
    {
        minW = 220; minH = 70;
        onPaint = [this] (juce::Graphics& g)
        {
            paintFace (g, getLocalBounds());
            auto r = body (g, getLocalBounds(), "REEL");
            const auto* reel = deck.reel.get();
            if (reel == nullptr) return;
            const auto& sk = skin();
            const float nameH = juce::jlimit (16.0f, 40.0f, r.getHeight() * 0.32f);
            g.setColour (sk.deckText);
            g.setFont (monoFont (nameH * 0.8f, true));
            g.drawFittedText ((deck.session != nullptr ? deck.session->getName() + "  -  " : juce::String()) + reel->getName(),
                              r.removeFromTop (nameH).toNearestInt(), juce::Justification::centredLeft, 1, 0.6f);
            juce::StringArray lines;
            lines.add (juce::String (tape::multitrackMachine (reel->machine).name) + "   " + tape::info ((tape::Stock) reel->tapeStock).label);
            lines.add (Reel::formatIps (reel->ips) + " IPS   " + juce::String (reel->numTracks) + " TRACKS   "
                       + juce::String (reel->sampleRate / 1000.0, 1) + " kHz"
                       + (reel->noiseReduction != nr::none ? juce::String ("   ") + nr::info (reel->noiseReduction).label : juce::String()));
            juce::StringArray armed;
            for (int t = 0; t < reel->numTracks; ++t) if (deck.engine.isArmed (t)) armed.add (juce::String (t + 1));
            g.setFont (monoFont (juce::jlimit (10.0f, 18.0f, r.getHeight() * 0.18f), false));
            const float lh = juce::jlimit (12.0f, 24.0f, r.getHeight() / 3.2f);
            for (auto& l : lines) { g.setColour (sk.deckTextDim); g.drawText (l, r.removeFromTop (lh), juce::Justification::centredLeft, true); }
            if (r.getHeight() >= 12.0f)
                statusLight (g, r.removeFromTop (lh), armed.isEmpty() ? juce::String ("NO TRACKS ARMED") : "ARMED  " + armed.joinIntoString (" "),
                             ! armed.isEmpty(), Colours::red, Icon::arm);
        };
    }
    void refresh() override { if (++frame % 10 == 0) repaint(); }
    void reelChanged() override { repaint(); }

private:
    MainComponent& deck;
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
    if (type == "reel")      return std::make_unique<ReelBlock> (d);
    return nullptr;
}

} // namespace hawthorne
