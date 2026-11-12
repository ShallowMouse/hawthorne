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
        { "MCI  -  Florida, USA  -  1980",
          "MCI, founded in Fort Lauderdale in the 50s and bought by Sony in 1982, built consoles and machines for working "
          "studios. The JH-24 (1979-80), successor to the JH-16, was its 24-track workhorse: reliable, affordable and "
          "everywhere in the 80s, with its Autolocator III.",
          "Bright and aggressive, with forward upper mids and a slightly lighter low end. Great on rock drums and guitars "
          "that need to jump out." },
        { "Ampex  -  California, USA  -  1976",
          "Ampex built America's first professional tape recorders in the late 40s. The MM1200 (1976), successor to the "
          "MM1000 and MM1100, was its big multitrack of the late 70s and 80s - 8, 16 or 24 tracks on 2-inch tape - and a "
          "fixture of rock and pop studios.",
          "Big and warm: the fattest low end of the lot and a soft, rounded top. Thick and forgiving." },
        { "Otari  -  Japan  -  1979",
          "Otari's MTR-90 (launched in 1979 as a 24-track 'for the 80s') had a pinch-rollerless, servo-driven transport. "
          "Solid, fast and quiet, it became one of the best-selling 2-inch 24-tracks ever, in commercial and project studios alike.",
          "Neutral and honest with low noise. It colours the least of the multitracks - tape glue without a strong character." },
        { "Studer  -  Switzerland  -  1964 (Abbey Road from 1965)",
          "Studer's first multitrack: a valve 4-track on 1-inch tape. Abbey Road bought four in 1965 to replace its Telefunken "
          "4-tracks, and Revolver and Sgt. Pepper's Lonely Hearts Club Band were built up on them, with reduction mixes "
          "bounced between machines, until 8-track arrived in 1968. Levels were read on the EMI desk's PPMs.",
          "Valve electronics: thick, warm and slightly compressed, with soft valve grit, a gentle top and more hiss and "
          "wobble. The sound of 60s Abbey Road." },
        { "3M  -  USA  -  1966",
          "3M's Isoloop machine, first shipped in mid-1966 - Wally Heider's 1-inch 8-track was among the first. Abbey Road "
          "chose the M23 8-track to succeed its J37s in 1968, and the Beatles used it for much of the White Album and Abbey "
          "Road. Each track has its own electronics drawer with a VU.",
          "Early solid-state: punchy and solid, a little darker and grainier than later machines." },
        { "Hawthorne  -  digital",
          "No machine at all: the reel plays and records straight from its files, whatever tape it's on.",
          "Bit-perfect: exactly what went in. No saturation, EQ, hiss, wow or flutter." },
        { "Scully  -  Connecticut, USA  -  1965",
          "Scully, the Bridgeport record-lathe maker, launched the solid-state 280 in 1965 (designed by Bell Sound's Dan "
          "Cronin) and the 1-inch 8-track in 1966, when studios everywhere were moving to 8 tracks. From 1967 to 1969 its "
          "8-track was a best seller - Decca in London used Scullys too.",
          "Punchy and forward in the mids with a solid low end and a slightly soft top." },
        { "3M  -  Minnesota, USA  -  1972",
          "3M's 2-inch multitrack of the 70s, successor to the M56, in 16- and 24-track versions. Its Isoloop closed-loop "
          "drive held the tape against the heads with very low flutter.",
          "Round and warm: smooth low mids, a gentle top and a very steady transport." },
        { "Stephens  -  Burbank, USA  -  1974",
          "John Stephens started on 3M M23 transports, then built his own with no capstan or pinch roller - two reel motors "
          "in a crystal-locked servo loop drove the tape - and transformerless electronics. The 821B was the 24-track; "
          "a few were built with 40 tracks on 2-inch tape. Rare and prized in Los Angeles studios into the early 80s.",
          "Open and detailed: a deep, clean low end and an extended top. The least 'tapey' of the vintage machines." },
        { "TEAC / Tascam  -  Japan  -  1976",
          "The machine that put 8-track recording into home and project studios for under $3,000: eight narrow tracks on "
          "half-inch tape, often paired with dbx noise reduction. Sold as a TEAC, later with the Tascam badge, until the mid-80s.",
          "Narrow tracks: less headroom, a lighter low end, a softer top and noticeably more hiss. Lo-fi charm." },
        { "Ampex  -  California, USA  -  mid 1950s",
          "In 1953 Ampex announced 2- and 3-track machines on quarter-inch tape, and soon 3- and 4-track ones on half-inch. "
          "RCA Victor recorded its great orchestral sessions on valve 300-3s at 15 ips, mixing down to 2-track masters while "
          "the world waited for stereo at home.",
          "Valve electronics on wide tracks: thick, warm and compressed, a dull top, a big low end, lots of hiss and some wobble." },
        { "Ampex  -  California, USA  -  1957",
          "Ross Snyder at Ampex invented Sel-Sync in 1955 - playing back from the record head, so new parts could be laid "
          "in time with old ones. The first Sel-Sync machine, a 1-inch 8-track, went to Les Paul early in 1957 for $10,000. "
          "He called it the Octopus. Ampex built only a handful in the late 50s.",
          "Valve electronics: warm and dense, soft on top, with plenty of hiss and wobble. Overdubs without bouncing." },
    };
    return s[juce::jlimit (0, tape::numMultitrackMachines - 1, i)];
}

inline const MachineStory& masterStory (int i)
{
    static const MachineStory s[] = {
        { "Ampex  -  California, USA  -  1976",
          "Unveiled at the 1976 AES as the 2-track of the ATR-100 series, the ATR-102 is the classic mastering deck - many "
          "of the great records of the 70s and 80s were mixed down to one, often on half-inch tape. Its pinch-rollerless, "
          "servo transport handles tape gently, with almost no flutter.",
          "A huge, deep low end, an open top and very low noise. The 'finished record' sound." },
        { "Studer  -  Switzerland  -  1970",
          "The A80 was Studer's workhorse for two decades, in mastering rooms and broadcasters everywhere. Modular "
          "electronics, each with its own VU.",
          "Smooth and detailed with a slightly silky top. Polished and refined." },
        { "Otari  -  Japan  -  1984",
          "Otari's compact professional 2-track, on quarter- and half-inch tape - common in mastering, broadcast and "
          "post-production.",
          "Clean and neutral: an honest print of the mix." },
        { "Ampex  -  California, USA  -  1958",
          "The valve-era Ampex: the 350 transport (1953) with the 351 electronics of 1958. More than 10,000 were in use by "
          "1961, and they recorded much of the music of the late 50s and early 60s.",
          "Vintage valve electronics: thick even-harmonic warmth, a softer top and more noise." },
        { "EMI  -  England  -  late 1940s",
          "EMI's own valve tape recorder, the BTR2, built from the late 40s - the 2-track at Abbey Road for years. The Beatles' early mono "
          "and stereo masters were mixed to BTR2s. Housed in EMI green, read on a PPM.",
          "Warm and round with gentle valve saturation and a soft top." },
        { "Hawthorne  -  digital",
          "No 2-track machine: the master is recorded and played straight, whatever its tape.",
          "Bit-perfect: the mix exactly as it left the bus." },
        { "Ampex  -  California, USA  -  1967",
          "The AG-440 replaced Ampex's valve machines with solid-state electronics, and through the late 60s and 70s it was "
          "in studios and broadcasters everywhere - the everyday 2-track of its time.",
          "Warm and solid with a full low end: the classic American 2-track sound." },
        { "Scully  -  Connecticut, USA  -  1960s",
          "The 2-track version of Scully's 280, at home in mastering rooms and radio stations - often beside a Scully lathe.",
          "Punchy and direct, with a firm middle." },
        { "Studer  -  Switzerland  -  1982",
          "Studer's microprocessor-controlled 2-track of the 80s, built for broadcast and mastering, with a precise transport "
          "and a fluorescent tape counter.",
          "Clean, precise and quiet: the tidiest print of the mix." },
        { "Revox  -  Switzerland  -  1967",
          "Studer's semi-professional sister brand. The A77 was the reel-to-reel of home studios, radio and musicians for a "
          "decade - many a demo and bounce went through one.",
          "A little less headroom and more hiss, with a friendly, slightly soft sound." },
        { "Ampex  -  California, USA  -  1949",
          "After the 200A of 1948 - America's first professional tape recorder - Ampex brought out the Model 300 in 1949, "
          "and it became the studio standard of the early 50s. Les Paul's early sound-on-sound records were made on a 300 "
          "that Bing Crosby gave him.",
          "Valve warmth and grit, a dull top, a fat low end, plenty of hiss and some wobble." },
        { "Telefunken  -  Germany  -  1954",
          "The Magnetophon was AEG's - tape recording began with it in Berlin in 1935. The M5 of 1954 was the valve studio "
          "and broadcast deck of post-war Germany, built in later versions for decades. Read on a peak meter, German style.",
          "Smoother and quieter than the American valve machines: a gentle, silky saturation and a soft top." },
    };
    return s[juce::jlimit (0, tape::numMasterMachines - 1, i)];
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
    // the badge on the right (its room kept, so the keys never run under it)
    const juce::String t = juce::String (sk.brand) + " " + sk.model;
    const juce::Font badgeFont (juce::FontOptions ("Arial", 11.0f, juce::Font::bold));
    const float bw = juce::GlyphArrangement::getStringWidth (badgeFont, t) + 12.0f;
    auto badgeRoom = d.removeFromRight (bw);
    d.removeFromRight (8.0f);
    auto counter = d.removeFromLeft (d.getWidth() * 0.34f);
    g.setColour (sk.counterBg);
    g.fillRoundedRectangle (counter, 3.0f);
    g.setColour (sk.counterInk);
    g.setFont (monoFont (juce::jmin (counter.getHeight() * 0.55f, counter.getWidth() * 0.16f), true));
    g.drawFittedText (sk.counter == MachineSkin::Counter::drum ? "0 0 1 2" : "00:01:23", counter.toNearestInt().reduced (2, 0),
                      juce::Justification::centred, 1, 0.5f);
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
        g.setFont (uiFont (juce::jmin (10.0f, k.getWidth() * 0.24f), true));
        g.drawText (keys[i], k, juce::Justification::centred);
    }
    // badge
    g.setFont (badgeFont);
    auto badge = juce::Rectangle<float> (bw, 16.0f).withCentre (badgeRoom.getCentre());
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
        for (int i : tape::multitrackOrder())
            place (false, i);
        for (int i : tape::masterOrder())
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
