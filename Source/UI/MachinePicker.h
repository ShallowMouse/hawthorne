#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "MachineSkin.h"
#include "HawthorneLookAndFeel.h"
#include "../Engine/TapeStock.h"

namespace hawthorne
{

//==============================================================================
/** The story of each machine, for the picker. */
struct MachineStory
{
    const char* maker;       // maker, country, era
    const char* history;
    const char* sound;
};

inline const MachineStory& multitrackStory (int i)
{
    static const MachineStory s[] = {
        { "Studer  -  Switzerland  -  1978",
          "Studer's first microprocessor-controlled multitrack: a 24-track, 2-inch machine with a famously stable transport "
          "and a meter bridge of backlit VUs. Through the late 70s and the 80s it was the flagship machine in big studios "
          "around the world.",
          "Clean and punchy with a tight, controlled low end and an extended top. It flatters a mix without smearing it - "
          "the 'expensive' tape sound." },
        { "MCI  -  Florida, USA  -  around 1980",
          "MCI (later bought by Sony) built consoles and machines for working studios, and the JH-24 was its 24-track "
          "workhorse: reliable, affordable and everywhere in the 80s, with its Autolocator III and colour-coded keys.",
          "Bright and aggressive, with forward upper mids and a slightly lighter low end. Great on rock drums and guitars "
          "that need to jump out." },
        { "Ampex  -  California, USA  -  1970s",
          "Ampex invented the professional tape recorder in America, and the MM1200 was its big 70s multitrack (8, 16 and "
          "24 tracks), a fixture of rock and pop studios with its row of glowing VUs under the deck.",
          "Big and warm: the fattest low end of the lot and a soft, rounded top. Thick and forgiving." },
        { "Otari  -  Japan  -  1980s",
          "Otari's MTR-90 was the solid, dependable 24-track of the 80s and 90s - fast, quiet and built to run all day, found "
          "in countless commercial and project studios.",
          "Neutral and honest with low noise. It colours the least of the multitracks - tape glue without a strong character." },
        { "Studer  -  Switzerland  -  1964 to 1972 at Abbey Road",
          "The valve 4-track on 1-inch tape that EMI's Abbey Road used through the mid-60s: Revolver and Sgt. Pepper's Lonely "
          "Hearts Club Band were built up on J37s, with reduction mixes bounced between machines. Levels were read on the EMI "
          "desk's PPMs.",
          "Valve electronics: thick, warm and slightly compressed, with soft valve grit, a gentle top and more hiss and "
          "wobble. The sound of 60s Abbey Road." },
        { "3M  -  USA  -  1966",
          "3M's 8-track on 1-inch tape, one of the first 8-tracks. Abbey Road brought one in during 1968, and the Beatles used "
          "it for much of the White Album and Abbey Road. Each track has its own electronics drawer with a backlit VU.",
          "Early solid-state: punchy and solid, a little darker and grainier than later machines." },
        { "Hawthorne  -  digital",
          "No machine at all: the reel plays and records straight from its files, whatever tape it's on.",
          "Bit-perfect: exactly what went in. No saturation, EQ, hiss, wow or flutter." },
    };
    return s[juce::jlimit (0, 6, i)];
}

inline const MachineStory& masterStory (int i)
{
    static const MachineStory s[] = {
        { "Ampex  -  California, USA  -  1976",
          "Ampex's ATR-102 is the classic mastering deck - many of the great records of the 70s and 80s were mixed down to "
          "one, often on half-inch tape. Precise transport, orange-backlit VUs.",
          "A huge, deep low end, an open top and very low noise. The 'finished record' sound." },
        { "Studer  -  Switzerland  -  1970",
          "The A80 was Studer's workhorse for two decades, in mastering rooms and broadcasters everywhere. Modular "
          "electronics, each with its own VU.",
          "Smooth and detailed with a slightly silky top. Polished and refined." },
        { "Otari  -  Japan  -  1980s",
          "Otari's compact professional 2-track, on quarter- and half-inch tape - common in mastering, broadcast and "
          "post-production.",
          "Clean and neutral: an honest print of the mix." },
        { "Ampex  -  California, USA  -  1950s",
          "The valve-era Ampex: a 350-series transport with 351 electronics in brushed aluminium. It recorded much of the "
          "music of the 50s and early 60s.",
          "Vintage valve electronics: thick even-harmonic warmth, a softer top and more noise." },
        { "EMI  -  England  -  1950s",
          "EMI's own valve tape recorder, the BTR2 - the mastering machine at Abbey Road for years. The Beatles' early mono "
          "and stereo masters were mixed to BTR2s. Housed in EMI green, read on a PPM.",
          "Warm and round with gentle valve saturation and a soft top." },
        { "Hawthorne  -  digital",
          "No 2-track machine: the master is recorded and played straight, whatever its tape.",
          "Bit-perfect: the mix exactly as it left the bus." },
    };
    return s[juce::jlimit (0, 5, i)];
}

//==============================================================================
/** A small drawing of the machine's deck and meter, in its own colours. */
inline void drawMachinePreview (juce::Graphics& g, juce::Rectangle<float> r, const MachineSkin& sk)
{
    // meter bridge strip behind, deck in front
    g.setColour (sk.bridge);
    g.fillRoundedRectangle (r, 4.0f);
    auto meterRow = r.removeFromTop (r.getHeight() * 0.55f).reduced (6.0f, 6.0f);
    auto deck = r;

    // meters
    const int nMeters = 4;
    const float mw = (meterRow.getWidth() - (nMeters - 1) * 6.0f) / nMeters;
    for (int i = 0; i < nMeters; ++i)
    {
        auto m = juce::Rectangle<float> (meterRow.getX() + i * (mw + 6.0f), meterRow.getY(), mw, meterRow.getHeight());
        g.setColour (sk.bezel);
        g.fillRoundedRectangle (m, 3.0f);
        auto face = m.reduced (3.0f).withTrimmedBottom (m.getHeight() * 0.18f);
        juce::ColourGradient fg (sk.faceTop, face.getCentreX(), face.getY(), sk.faceBottom, face.getCentreX(), face.getBottom(), false);
        g.setGradientFill (fg);
        g.fillRoundedRectangle (face, 2.0f);
        const juce::Point<float> P (face.getCentreX(), face.getBottom() + face.getHeight() * 0.25f);
        const float R = face.getHeight() * 0.95f;
        juce::Path arc;
        arc.addCentredArc (P.x, P.y, R, R, 0.0f, -0.8f, 0.45f, true);
        g.setColour (sk.meterInk);
        g.strokePath (arc, juce::PathStrokeType (1.0f));
        if (sk.meter != MachineSkin::Meter::ppm)
        {
            juce::Path red;
            red.addCentredArc (P.x, P.y, R, R, 0.0f, 0.45f, 0.8f, true);
            g.setColour (sk.meterRed);
            g.strokePath (red, juce::PathStrokeType (2.0f));
        }
        const float a = -0.5f + 0.25f * (float) i;
        g.setColour (sk.needle);
        g.drawLine (P.x, P.y, P.x + R * 1.05f * std::sin (a), P.y - R * 1.05f * std::cos (a), 1.2f);
        g.setColour (sk.meterInk);
        g.setFont (uiFont (face.getHeight() * 0.28f, true));
        g.drawText (sk.meter == MachineSkin::Meter::ppm ? "PPM" : "VU", face.withTrimmedTop (face.getHeight() * 0.45f), juce::Justification::centred);
    }

    // deck
    juce::ColourGradient dg (sk.deck, deck.getX(), deck.getY(), sk.deckLow, deck.getX(), deck.getBottom(), false);
    g.setGradientFill (dg);
    g.fillRoundedRectangle (deck, 4.0f);
    auto d = deck.reduced (8.0f, 8.0f);
    auto counter = d.removeFromLeft (d.getWidth() * 0.3f);
    g.setColour (sk.counterBg);
    g.fillRoundedRectangle (counter, 3.0f);
    g.setColour (sk.counterInk);
    g.setFont (monoFont (counter.getHeight() * 0.55f, true));
    g.drawText (sk.counter == MachineSkin::Counter::drum ? "0 0 1 2" : "00:01:23", counter, juce::Justification::centred);
    d.removeFromLeft (8.0f);
    const char* keys[] = { "STOP", "PLAY", "REC" };
    const float kw = juce::jmin (52.0f, (d.getWidth() - 12.0f) / 3.0f);
    for (int i = 0; i < 3; ++i)
    {
        auto k = d.removeFromLeft (kw).reduced (0.0f, 2.0f);
        d.removeFromLeft (6.0f);
        juce::Colour cap = sk.key;
        if (sk.keys == MachineSkin::Keys::coloured) cap = i == 0 ? juce::Colour (0xffe9e6dc) : i == 1 ? juce::Colour (0xff2f8f4e) : juce::Colour (0xffc8322a);
        if (sk.keys == MachineSkin::Keys::ivory && i == 2) cap = juce::Colour (0xffd9682c);
        g.setColour (cap);
        g.fillRoundedRectangle (k, sk.keys == MachineSkin::Keys::bakelite ? 6.0f : 2.0f);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawRoundedRectangle (k, sk.keys == MachineSkin::Keys::bakelite ? 6.0f : 2.0f, 1.0f);
        g.setColour (cap.getPerceivedBrightness() > 0.55f ? juce::Colour (0xff1b1916) : (i == 2 ? juce::Colour (0xffe0503c) : juce::Colours::white));
        g.setFont (uiFont (10.0f, true));
        g.drawText (keys[i], k, juce::Justification::centred);
    }
    // badge
    const juce::String t = juce::String (sk.brand) + " " + sk.model;
    g.setFont (juce::Font (juce::FontOptions ("Arial", 11.0f, juce::Font::bold)));
    const float bw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), t) + 12.0f;
    auto badge = juce::Rectangle<float> (bw, 16.0f).withPosition (deck.getRight() - bw - 8.0f, deck.getY() + 6.0f);
    g.setColour (sk.badge);
    g.fillRoundedRectangle (badge, 2.0f);
    g.setColour (sk.badgeText);
    g.drawText (t, badge, juce::Justification::centred);
}

//==============================================================================
/** Pick the multitrack and the 2-track: a card per machine with its look, story and sound. */
class MachinePicker : public juce::Component
{
public:
    MachinePicker (int multitrack, int master, std::function<void (int, int)> onPick)
        : mt (multitrack), ms (master), pick (std::move (onPick))
    {
        for (int i = 0; i < tape::numMultitrackMachines; ++i) cards.add (new Card (*this, false, i));
        for (int i = 0; i < tape::numMasterMachines; ++i)     cards.add (new Card (*this, true, i));
        for (auto* c : cards) content.addAndMakeVisible (c);
        viewport.setViewedComponent (&content, false);
        viewport.setScrollBarsShown (true, false);
        addAndMakeVisible (viewport);
        setSize (980, 760);
    }

    void paint (juce::Graphics& g) override { g.fillAll (Colours::bg); }

    void resized() override
    {
        viewport.setBounds (getLocalBounds());
        const int w = getWidth() - viewport.getScrollBarThickness() - 16;
        const int colW = (w - 16) / 2, cardH = 300;
        const int top = 42;
        // multitrack on the left, the 2-track master on the right
        int rows[2] = { 0, 0 };
        auto place = [&] (bool master, int index)
        {
            for (auto* c : cards)
                if (c->master == master && c->index == index)
                {
                    const int col = master ? 1 : 0;
                    c->setBounds (8 + col * (colW + 16), top + rows[col] * (cardH + 14), colW, cardH);
                    ++rows[col];
                }
        };
        // oldest at the top, newest at the bottom (no machine - digital - last)
        for (int i : { 4 /*J37 1964*/, 5 /*3M M23 1966*/, 2 /*MM1200 1970s*/, 0 /*A800 1978*/, 1 /*JH-24 ~1980*/, 3 /*MTR-90 1980s*/, tape::noMultitrackMachine })
            place (false, i);
        for (int i : { 4 /*BTR2 1950s*/, 3 /*Ampex 351 1950s*/, 1 /*A80 1970*/, 0 /*ATR-102 1976*/, 2 /*MTR-12 1980s*/, tape::noMasterMachine })
            place (true, i);
        content.setSize (w + 8, top + juce::jmax (rows[0], rows[1]) * (cardH + 14) + 10);
        content.colW = colW;
    }

private:
    struct Content : juce::Component
    {
        int colW = 400;
        void paint (juce::Graphics& g) override
        {
            g.setColour (Colours::mustard);
            g.setFont (monoFont (15.0f, true));
            g.drawText ("MULTITRACK  -  deck, transport, track meters", 10, 6, colW, 30, juce::Justification::centredLeft);
            g.drawText ("2-TRACK  -  the master", 8 + colW + 18, 6, colW, 30, juce::Justification::centredLeft);
        }
    };

    struct Card : juce::Component
    {
        Card (MachinePicker& o, bool m, int i) : owner (o), master (m), index (i)
        {
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
        }
        void paint (juce::Graphics& g) override
        {
            const bool chosen = (master ? owner.ms : owner.mt) == index;
            const auto& sk = master ? MachineSkin::master (index) : MachineSkin::multitrack (index);
            const auto& info = master ? tape::masterMachine (index) : tape::multitrackMachine (index);
            const auto& story = master ? masterStory (index) : multitrackStory (index);
            auto r = getLocalBounds().toFloat();
            g.setColour (Colours::panel);
            g.fillRoundedRectangle (r, 6.0f);
            g.setColour (chosen ? Colours::mustard : (isMouseOver() ? Colours::edge.brighter (0.4f) : Colours::edge));
            g.drawRoundedRectangle (r.reduced (1.0f), 6.0f, chosen ? 2.5f : 1.0f);

            auto a = getLocalBounds().reduced (12);
            drawMachinePreview (g, a.removeFromTop (110).toFloat(), sk);
            a.removeFromTop (8);
            g.setColour (Colours::text);
            g.setFont (uiFont (16.0f, true));
            auto title = a.removeFromTop (22);
            g.drawText (info.name, title, juce::Justification::centredLeft);
            photosArea = title.removeFromRight (110);
            g.setColour (isMouseOver() ? Colours::text : Colours::textDim);
            g.setFont (monoFont (11.0f, true));
            g.drawText ("PHOTOS ->", photosArea, juce::Justification::centredRight);
            if (chosen)
            {
                g.setColour (Colours::mustard);
                g.drawText ("IN USE", title, juce::Justification::centredRight);
            }
            g.setColour (Colours::textDim);
            g.setFont (monoFont (11.0f, true));
            g.drawText (story.maker, a.removeFromTop (16), juce::Justification::centredLeft);
            a.removeFromTop (4);
            g.setColour (Colours::text.withAlpha (0.9f));
            g.setFont (uiFont (13.0f));
            g.drawFittedText (story.history, a.removeFromTop (70), juce::Justification::topLeft, 5, 1.0f);
            g.setColour (Colours::mustard.brighter (0.2f));
            g.setFont (uiFont (13.0f, true));
            g.drawFittedText (juce::String ("Sound: ") + story.sound, a, juce::Justification::topLeft, 3, 1.0f);
        }
        void mouseEnter (const juce::MouseEvent&) override { repaint(); }
        void mouseExit (const juce::MouseEvent&) override  { repaint(); }
        void mouseUp (const juce::MouseEvent& e) override
        {
            if (! e.mouseWasClicked()) return;
            if (photosArea.contains (e.getPosition()))
            {
                // real photos of the machine, in the browser
                const auto& info = master ? tape::masterMachine (index) : tape::multitrackMachine (index);
                juce::URL ("https://www.bing.com/images/search").withParameter ("q", juce::String (info.name) + " tape machine")
                    .launchInDefaultBrowser();
                return;
            }
            if (master) owner.ms = index; else owner.mt = index;
            if (owner.pick) owner.pick (owner.mt, owner.ms);
            for (auto* c : owner.cards) c->repaint();
        }
        juce::Rectangle<int> photosArea;
        MachinePicker& owner;
        const bool master;
        const int index;
    };

    int mt, ms;
    std::function<void (int, int)> pick;
    juce::Viewport viewport;
    Content content;
    juce::OwnedArray<Card> cards;
};

} // namespace hawthorne
