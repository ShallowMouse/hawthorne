#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "MachineSkin.h"
#include "MachinePicker.h"
#include "TapeChoice.h"
#include "../Engine/TapeStock.h"
#include "../Engine/NoiseReduction.h"
#include "../Melville.h"

namespace hawthorne
{

//==============================================================================
/** What a new session's (or reel's) wizard settles. */
struct NewReelChoice
{
    juce::String name, artist;     // (a new session only)
    int tracks = 24;
    double ips = 15.0;
    int stock = 1;
    int machine = 0;
    int nr = 0;
    double bpm = 120.0;            // 0: no tempo (Hawthorne on its own)
    int beatsPerBar = 4;
    int desk = -1;                 // inside MELVILLE: WHITMAN's desk (-1: as it is)
    bool usualCables = true;       // inside MELVILLE: the patchbay starts with the usual cables, or empty
};

/** Inside MELVILLE the wizard also asks for the desk and the patchbay. */
inline bool wizardAsksStudio()
{
   #if defined (MELVILLE_HOSTED) && MELVILLE_HOSTED
    return melville::isHosted();
   #else
    return false;
   #endif
}

//==============================================================================
/** A tape box, drawn: the stock's colours, its name on the band, and the reel through the window. */
inline void drawTapeBox (juce::Graphics& g, juce::Rectangle<float> r, int stock, const juce::Font& font)
{
    struct Look { juce::uint32 body, band; };
    static const Look looks[] = {
        { 0xffeceae4, 0xffc9c6be },   // clean
        { 0xffe9e4d6, 0xffc8322a },   // Ampex 456
        { 0xff1d1d1f, 0xffc9a648 },   // Quantegy GP9
        { 0xffc9b48a, 0xff6b3f22 },   // Scotch 250
        { 0xff2f4f7a, 0xffe8c547 },   // BASF SM911
        { 0xffe8e3d3, 0xff1f3e8c },   // Emitape
        { 0xffc19a45, 0xff1d1d1f },   // Ampex 499 (Grand Master Gold)
        { 0xff222428, 0xffb9bec4 },   // Emtec SM900
        { 0xff2f5a3a, 0xffe8e3d3 },   // EMI 888
        { 0xff7a2430, 0xffe8e3d3 },   // EMI 811
        { 0xff263f7a, 0xffe3b341 },   // EMI 815
        { 0xffd8c9a3, 0xffb3261e },   // Scotch 111
        { 0xff9aa3a8, 0xff1f3e8c },   // BASF LGS
    };
    const auto& lk = looks[(size_t) juce::jlimit (0, (int) std::size (looks) - 1, stock)];
    const juce::Colour body (lk.body), band (lk.band);
    auto ink = [] (juce::Colour c) { return c.getPerceivedBrightness() > 0.55f ? juce::Colour (0xff1b1916) : juce::Colour (0xfff2efe6); };

    // the box (square, like a 10.5" reel box), a shadow, and a little perspective edge
    const float side = juce::jmin (r.getWidth(), r.getHeight());
    auto box = juce::Rectangle<float> (side, side).withCentre (r.getCentre());
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (box.translated (3.0f, 4.0f), 3.0f);
    g.setColour (body.darker (0.35f));
    g.fillRoundedRectangle (box.withTrimmedLeft (side - 6.0f).translated (0.0f, 0.0f), 2.0f);
    auto face = box.withTrimmedRight (5.0f);
    g.setColour (body);
    g.fillRoundedRectangle (face, 3.0f);

    // the band, with the stock's name
    auto bandR = face.withTrimmedTop (face.getHeight() * 0.14f).withHeight (face.getHeight() * 0.22f);
    g.setColour (band);
    g.fillRect (bandR);
    const auto& info = tape::info ((tape::Stock) stock);
    g.setColour (ink (band));
    g.setFont (font.withHeight (juce::jmax (9.0f, bandR.getHeight() * 0.42f)));
    g.drawFittedText (stock == 0 ? juce::String ("NO TAPE") : juce::String (info.label), bandR.reduced (4.0f, 0.0f).toNearestInt(),
                      juce::Justification::centred, 1, 0.6f);

    // the window, with the tape pack and the hub
    const float wr = face.getWidth() * 0.27f;
    const juce::Point<float> c (face.getCentreX(), face.getBottom() - face.getHeight() * 0.33f);
    g.setColour (body.darker (0.5f));
    g.fillEllipse (juce::Rectangle<float> (wr * 2.0f + 6.0f, wr * 2.0f + 6.0f).withCentre (c));
    if (stock != 0)
    {
        g.setColour (juce::Colour (0xff3a2a20));
        g.fillEllipse (juce::Rectangle<float> (wr * 2.0f, wr * 2.0f).withCentre (c));
        g.setColour (juce::Colour (0xff4a382b));
        g.drawEllipse (juce::Rectangle<float> (wr * 1.5f, wr * 1.5f).withCentre (c), 1.0f);
    }
    g.setColour (juce::Colour (0xffb9bcc0));
    g.fillEllipse (juce::Rectangle<float> (wr * 0.8f, wr * 0.8f).withCentre (c));
    g.setColour (juce::Colour (0xff5c6064));
    g.fillEllipse (juce::Rectangle<float> (wr * 0.22f, wr * 0.22f).withCentre (c));

}

/** A noise-reduction unit, drawn: a 1U rack panel with its name, a switch and lamps (or a blank plate). */
inline void drawNrUnit (juce::Graphics& g, juce::Rectangle<float> r, int system, const juce::Font& font)
{
    auto unit = r.withSizeKeepingCentre (r.getWidth(), juce::jmin (r.getHeight(), r.getWidth() * 0.28f));
    const juce::Colour face = system == 0 ? juce::Colour (0xff3a3b3e)
                            : system == 3 ? juce::Colour (0xff2a2c30) : juce::Colour (0xff4b4d52);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (unit.translated (2.0f, 3.0f), 2.0f);
    g.setColour (face);
    g.fillRoundedRectangle (unit, 2.0f);
    // rack ears with their screws
    for (auto ear : { unit.withWidth (unit.getWidth() * 0.06f), unit.withTrimmedLeft (unit.getWidth() * 0.94f) })
    {
        g.setColour (face.darker (0.25f));
        g.fillRect (ear);
        for (float y : { 0.25f, 0.75f })
        {
            g.setColour (juce::Colour (0xff1a1b1d));
            g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre ({ ear.getCentreX(), ear.getY() + ear.getHeight() * y }));
        }
    }
    auto p = unit.reduced (unit.getWidth() * 0.08f, unit.getHeight() * 0.18f);
    if (system == 0)
    {
        g.setColour (juce::Colour (0xff8f8b82));
        g.setFont (font.withHeight (p.getHeight() * 0.42f));
        g.drawText ("BLANK PANEL", p, juce::Justification::centred);
        return;
    }
    const auto& info = nr::info (system);
    g.setColour (juce::Colour (0xffece6d8));
    g.setFont (font.withHeight (p.getHeight() * 0.5f).boldened());
    g.drawText (info.label, p.removeFromLeft (p.getWidth() * 0.55f), juce::Justification::centredLeft);
    // lamps and a toggle
    const char* lamps[] = { "NR", "ENC", "DEC" };
    for (int i = 0; i < 3; ++i)
    {
        auto lr = p.removeFromLeft (p.getWidth() / (float) (3 - i + 1));
        const juce::Colour on = i == 0 ? juce::Colour (0xff7ee08a) : juce::Colour (0xffffb347);
        g.setColour (i == 0 ? on : juce::Colour (0xff2b2420));
        g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ lr.getCentreX(), lr.getY() + lr.getHeight() * 0.32f }));
        g.setColour (juce::Colour (0xffb9b2a3));
        g.setFont (font.withHeight (juce::jlimit (6.0f, 11.0f, juce::jmin (lr.getHeight() * 0.3f, lr.getWidth() * 0.3f))));
        g.drawFittedText (lamps[i], lr.withTrimmedTop (lr.getHeight() * 0.55f).toNearestInt(), juce::Justification::centred, 1, 0.6f);
    }
    g.setColour (juce::Colour (0xffc9cbcf));
    g.fillRoundedRectangle (juce::Rectangle<float> (8.0f, p.getHeight() * 0.8f).withCentre (p.getCentre()), 3.0f);
}

//==============================================================================
/**
    A new session (or a new reel on a session), step by step and in pictures, laid out like
    the machine room: a card per choice with its picture, where and when it came from, its
    story and its sound, oldest first. Steps: the session, the machine (and the tracks), the
    tape (and the speed), the noise reduction, the tempo, and inside MELVILLE the desk and the
    patchbay, then a last look at the tape box.
*/
class NewReelWizard : public juce::Component
{
public:
    struct Look
    {
        juce::Colour accent = Colours::mustard, onAccent = Colours::black, bg = Colours::bg, panel = Colours::panel,
                     text = Colours::text, dim = Colours::textDim;
        std::function<juce::Font (float, bool)> font;
    };

    NewReelWizard (bool withSession, const NewReelChoice& start, const Look& lk, const juce::String& sessionName,
                   std::function<void (const NewReelChoice&)> create)
        : choice (start), look (lk), forSession (sessionName), onCreate (std::move (create)), list (*this)
    {
        if (withSession) steps.push_back (Step::session);
        for (auto s : { Step::machine, Step::tape, Step::nr, Step::tempo }) steps.push_back (s);
       #if defined (MELVILLE_HOSTED) && MELVILLE_HOSTED
        if (wizardAsksStudio())
        {
            if (! melville::deskCards().empty())
            {
                steps.push_back (Step::desk);
                if (choice.desk < 0 && melville::currentDesk) choice.desk = melville::currentDesk();
            }
            steps.push_back (Step::wiring);
        }
       #endif
        steps.push_back (Step::ready);

        for (auto* e : { &nameEd, &artistEd, &bpmEd })
        {
            e->setFont (font (20.0f, false));
            e->setColour (juce::TextEditor::backgroundColourId, look.panel);
            e->setColour (juce::TextEditor::textColourId, look.text);
            e->setColour (juce::TextEditor::outlineColourId, look.dim.withAlpha (0.6f));
            e->setColour (juce::TextEditor::focusedOutlineColourId, look.accent);
            e->setIndents (10, 8);
            e->onReturnKey = [this] { next(); };
            addChildComponent (e);
        }
        nameEd.setTextToShowWhenEmpty ("The album, the single, the client", look.dim);
        artistEd.setTextToShowWhenEmpty ("Who it's for (optional)", look.dim);
        nameEd.setText (choice.name, false);
        artistEd.setText (choice.artist, false);
        bpmEd.setInputRestrictions (6, "0123456789.");
        bpmEd.setJustification (juce::Justification::centred);
        bpmEd.setFont (font (34.0f, true));
        bpmEd.setText (choice.bpm > 0.0 ? juce::String (choice.bpm, choice.bpm == std::floor (choice.bpm) ? 0 : 1) : juce::String ("120"), false);
        bpmEd.onTextChange = [this] { readBpm(); repaint(); };

        view.setViewedComponent (&list, false);
        view.setScrollBarsShown (true, false);
        view.setScrollBarThickness (10);
        addChildComponent (view);

        for (auto* b : { &backButton, &nextButton, &cancelButton })
        {
            b->setColour (juce::TextButton::buttonColourId, look.panel);
            b->setColour (juce::TextButton::textColourOffId, look.text);
            addAndMakeVisible (b);
        }
        nextButton.setColour (juce::TextButton::buttonColourId, look.accent);
        nextButton.setColour (juce::TextButton::textColourOffId, look.onAccent);
        backButton.onClick = [this] { go (index - 1); };
        nextButton.onClick = [this] { next(); };
        cancelButton.onClick = [this] { close(); };

        setWantsKeyboardFocus (true);
        setSize (1000, 760);
        go (0);
    }

    /** (Development: every step as a picture, into a folder.) */
    static void gallery (const juce::File& dir, const Look& lk)
    {
        NewReelChoice c;
        c.name = "Sea Shanties"; c.artist = "The Pequod Crew"; c.stock = 8; c.machine = 4; c.nr = 1; c.ips = 15.0; c.tracks = 24;
        NewReelWizard w (true, c, lk, {}, nullptr);
        for (int i = 0; i < (int) w.steps.size(); ++i)
        {
            w.go (i);
            juce::PNGImageFormat png;
            juce::FileOutputStream out (dir.getChildFile ("wizard-" + juce::String (i + 1) + ".png"));
            if (out.openedOk()) { out.setPosition (0); out.truncate(); png.writeImageToStream (w.createComponentSnapshot (w.getLocalBounds()), out); }
        }
    }

    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress::returnKey) { next(); return true; }
        if (k == juce::KeyPress::escapeKey) { close(); return true; }
        if (k == juce::KeyPress::leftKey && index > 0) { go (index - 1); return true; }
        return false;
    }

    void resized() override
    {
        auto foot = getLocalBounds().removeFromBottom (70).reduced (30, 16);
        cancelButton.setBounds (foot.removeFromLeft (110));
        nextButton.setBounds (foot.removeFromRight (150));
        foot.removeFromRight (10);
        backButton.setBounds (foot.removeFromRight (110));
        nameEd.setBounds (60, 210, getWidth() - 120, 44);
        artistEd.setBounds (60, 320, getWidth() - 120, 44);
        bpmEd.setBounds (60, 210, 200, 64);
        layoutStep();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (look.bg);

        // the steps along the top
        auto top = getLocalBounds().removeFromTop (44).reduced (30, 0);
        const float sw = (float) top.getWidth() / (float) steps.size();
        for (size_t i = 0; i < steps.size(); ++i)
        {
            auto r = juce::Rectangle<float> ((float) top.getX() + sw * (float) i, 14.0f, sw - 8.0f, 22.0f);
            const bool here = (int) i == index, done = (int) i < index;
            g.setColour (here ? look.accent : done ? look.accent.withAlpha (0.35f) : look.panel);
            g.fillRoundedRectangle (r.withHeight (4.0f).withY (r.getBottom() + 2.0f), 2.0f);
            g.setColour (here ? look.text : look.dim);
            g.setFont (font (13.0f, true));
            g.drawText (juce::String ((int) i + 1) + "  " + stepName (steps[i]), r, juce::Justification::centredLeft);
        }

        // the step's question
        g.setColour (look.text);
        g.setFont (font (26.0f, true));
        g.drawText (question(), 30, 58, getWidth() - 60, 34, juce::Justification::centredLeft);
        g.setColour (look.dim);
        g.setFont (font (15.0f, false));
        g.drawText (hint(), 30, 92, getWidth() - 60, 20, juce::Justification::centredLeft);

        switch (steps[(size_t) index])
        {
            case Step::session: paintSession (g); break;
            case Step::machine: paintChips (g, "TRACKS"); break;
            case Step::tape:    paintChips (g, "SPEED"); break;
            case Step::nr:      break;
            case Step::tempo:   paintTempo (g); paintChips (g, "BAR"); break;
            case Step::desk:    break;
            case Step::wiring:  break;
            case Step::ready:   paintReady (g); break;
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const int h = chipAt (e.getPosition());
        if (h != hoverChip) { hoverChip = h; repaint(); }
        setMouseCursor (h >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    }
    void mouseExit (const juce::MouseEvent&) override { hoverChip = -1; repaint(); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (const int h = chipAt (e.getPosition()); h >= 0) { chips[(size_t) h].pick(); layoutChips(); repaint(); }
    }

private:
    enum class Step { session, machine, tape, nr, tempo, desk, wiring, ready };

    //--------------------------------------------------------------------------
    /** The cards of a step, in a scrolling list (like the machine room). */
    struct CardList : juce::Component
    {
        explicit CardList (NewReelWizard& o) : owner (o) {}
        struct Card { int value; juce::Rectangle<int> r; };
        std::vector<Card> cards;
        int hover = -1;

        void set (const std::vector<int>& values, int cols, int cardH, int width)
        {
            cards.clear();
            const int gap = 14, cw = (width - (cols - 1) * gap) / cols;
            for (size_t i = 0; i < values.size(); ++i)
                cards.push_back ({ values[i], { ((int) i % cols) * (cw + gap), ((int) i / cols) * (cardH + gap), cw, cardH } });
            const int rows = ((int) values.size() + cols - 1) / cols;
            setSize (width, rows * (cardH + gap));
            hover = -1;
            repaint();
        }
        int at (juce::Point<int> p) const
        {
            for (size_t i = 0; i < cards.size(); ++i) if (cards[i].r.contains (p)) return (int) i;
            return -1;
        }
        void paint (juce::Graphics& g) override
        {
            for (size_t i = 0; i < cards.size(); ++i) owner.paintCard (g, cards[i].r, cards[i].value, (int) i == hover);
        }
        void mouseMove (const juce::MouseEvent& e) override
        {
            const int h = at (e.getPosition());
            if (h != hover) { hover = h; repaint(); }
            setMouseCursor (h >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        }
        void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
        void mouseDown (const juce::MouseEvent& e) override
        {
            if (const int h = at (e.getPosition()); h >= 0) { owner.pickCard (cards[(size_t) h].value); repaint(); owner.repaint(); }
        }
        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            if (at (e.getPosition()) >= 0) owner.next();     // (a double-click picks it and moves on)
        }
        NewReelWizard& owner;
    };

    struct Chip
    {
        juce::Rectangle<int> r;
        juce::String label;
        bool chosen = false;
        std::function<void()> pick;
    };

    juce::Font font (float size, bool bold) const
    {
        return look.font ? look.font (size, bold) : uiFont (size, bold);
    }

    static juce::String stepName (Step s)
    {
        switch (s)
        {
            case Step::session: return "SESSION";
            case Step::machine: return "MACHINE";
            case Step::tape:    return "TAPE";
            case Step::nr:      return "NOISE REDUCTION";
            case Step::tempo:   return "TEMPO";
            case Step::desk:    return "DESK";
            case Step::wiring:  return "PATCHBAY";
            case Step::ready:   return "READY";
        }
        return {};
    }

    juce::String question() const
    {
        switch (steps[(size_t) index])
        {
            case Step::session: return "A new session";
            case Step::machine: return "Which machine, and how many tracks?";
            case Step::tape:    return "Which tape, at what speed?";
            case Step::nr:      return "Noise reduction?";
            case Step::tempo:   return "What tempo?";
            case Step::desk:    return "Which desk will it be mixed on?";
            case Step::wiring:  return "How should the patchbay start?";
            case Step::ready:   return forSession.isNotEmpty() ? "A new reel for \"" + forSession + "\"" : "Ready to thread up";
        }
        return {};
    }

    juce::String hint() const
    {
        switch (steps[(size_t) index])
        {
            case Step::session: return "A session holds the reels for one job: an album, a single, a client.";
            case Step::machine: return "Oldest first. The machine can be changed later; the track count can't.";
            case Step::tape:    return "Oldest first. The tape stays with the reel for life. Slower tape: bigger low bump, darker top, more hiss.";
            case Step::nr:      return "None first, then oldest first. Hiss reduction across the whole reel: can be changed later, and switched per track.";
            case Step::tempo:   return wizardAsksStudio() ? "The song's tempo and bar: the click, THOREAU and the bar counter follow it. Can be changed later."
                                                         : "For the click and the bar counter. Can be changed later, or none at all.";
            case Step::desk:    return "Oldest first. A new reel starts with a clean desk: every fader, EQ and send back to flat.";
            case Step::wiring:  return "A new reel starts with a clean studio. Every connection is a cable you can see and pull.";
            case Step::ready:   return "Check the box. CREATE threads it up on the deck.";
        }
        return {};
    }

    //--------------------------------------------------------------------------
    void go (int i)
    {
        index = juce::jlimit (0, (int) steps.size() - 1, i);
        const bool session = steps[(size_t) index] == Step::session;
        const bool tempo = steps[(size_t) index] == Step::tempo;
        nameEd.setVisible (session);
        artistEd.setVisible (session);
        bpmEd.setVisible (tempo && choice.bpm > 0.0);
        if (session) nameEd.grabKeyboardFocus(); else if (bpmEd.isVisible()) bpmEd.grabKeyboardFocus(); else grabKeyboardFocus();
        backButton.setEnabled (index > 0);
        nextButton.setButtonText (index == (int) steps.size() - 1 ? "CREATE" : "NEXT");
        hoverChip = -1;
        layoutStep();
        view.setViewPosition (0, 0);
        // (the chosen card in view)
        for (auto& c : list.cards)
            if (isChosen (c.value)) { view.setViewPosition (0, juce::jmax (0, c.r.getY() - 10)); break; }
        repaint();
    }

    void next()
    {
        if (steps[(size_t) index] == Step::session)
        {
            choice.name = nameEd.getText().trim();
            choice.artist = artistEd.getText().trim();
            if (choice.name.isEmpty())
            {
                nameEd.setColour (juce::TextEditor::outlineColourId, Colours::red);
                nameEd.setTextToShowWhenEmpty ("Please give the session a name", Colours::red);
                nameEd.grabKeyboardFocus();
                nameEd.repaint();
                return;
            }
        }
        if (steps[(size_t) index] == Step::tempo && choice.bpm > 0.0)
        {
            readBpm();
            if (choice.bpm < 20.0 || choice.bpm > 400.0)
            {
                bpmEd.setColour (juce::TextEditor::outlineColourId, Colours::red);
                bpmEd.grabKeyboardFocus();
                bpmEd.repaint();
                return;
            }
        }
        if (index < (int) steps.size() - 1) { go (index + 1); return; }

        const auto made = choice;
        auto create = onCreate;
        close();
        if (create) create (made);
    }

    void close()
    {
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>()) { dw->exitModalState (0); return; }
        if (auto* tw = findParentComponentOfClass<juce::DocumentWindow>()) tw->setVisible (false);     // (MELVILLE's own frame)
    }

    //--------------------------------------------------------------------------
    std::vector<int> valuesFor (Step s) const
    {
        std::vector<int> v;
        if (s == Step::machine) return tape::multitrackOrder();
        if (s == Step::tape) return stocksFor (tape::forMultitrack);             // (oldest first, clean last)
        if (s == Step::nr) for (int k = 0; k < nr::numSystems; ++k) v.push_back (nr::order()[k]);
       #if defined (MELVILLE_HOSTED) && MELVILLE_HOSTED
        if (s == Step::desk) for (auto& d : melville::deskCards()) v.push_back (d.id);
       #endif
        if (s == Step::wiring) v = { 1, 0 };      // the usual cables, an empty bay
        return v;
    }

    bool isChosen (int value) const
    {
        switch (steps[(size_t) index])
        {
            case Step::machine: return choice.machine == value;
            case Step::tape:    return choice.stock == value;
            case Step::nr:      return choice.nr == value;
            case Step::desk:    return choice.desk == value;
            case Step::wiring:  return (choice.usualCables ? 1 : 0) == value;
            default:            return false;
        }
    }

    void pickCard (int value)
    {
        switch (steps[(size_t) index])
        {
            case Step::machine: choice.machine = value; break;
            case Step::tape:    choice.stock = value; break;
            case Step::nr:      choice.nr = value; break;
            case Step::desk:    choice.desk = value; break;
            case Step::wiring:  choice.usualCables = value != 0; break;
            default: break;
        }
    }

    void layoutStep()
    {
        const auto s = steps[(size_t) index];
        const bool cardsHere = s == Step::machine || s == Step::tape || s == Step::nr || s == Step::desk || s == Step::wiring;
        view.setVisible (cardsHere);
        if (cardsHere)
        {
            const int bottom = (s == Step::machine || s == Step::tape) ? getHeight() - 136 : getHeight() - 84;
            view.setBounds (30, 124, getWidth() - 60, bottom - 124);
            const int w = view.getWidth() - view.getScrollBarThickness() - 6;
            list.set (valuesFor (s), 2, s == Step::machine ? 290 : s == Step::tape ? 210 : s == Step::desk ? 300 : s == Step::wiring ? 330 : 190, w);
        }
        layoutChips();
    }

    void layoutChips()
    {
        chips.clear();
        const int y = getHeight() - 124;
        auto add = [&] (const juce::StringArray& labels, const std::vector<int>& values, int current, int cw, std::function<void (int)> set)
        {
            for (int i = 0; i < labels.size(); ++i)
            {
                Chip c;
                c.r = { 30 + 130 + i * (cw + 8), y, cw, 34 };
                c.label = labels[i];
                c.chosen = values[(size_t) i] == current;
                const int v = values[(size_t) i];
                c.pick = [set, v] { set (v); };
                chips.push_back (c);
            }
        };
        const auto s = steps[(size_t) index];
        if (s == Step::machine)
            add ({ "8", "16", "24", "32", "48", "64", "72", "96" }, { 8, 16, 24, 32, 48, 64, 72, 96 }, choice.tracks, 74,
                 [this] (int v) { choice.tracks = v; });
        else if (s == Step::tape)
            add ({ juce::String (juce::CharPointer_UTF8 ("7\xc2\xbd ips")), "15 ips", "30 ips" }, { 75, 150, 300 },
                 (int) std::round (choice.ips * 10.0), 96, [this] (int v) { choice.ips = v / 10.0; });
        else if (s == Step::tempo)
        {
            // the bar; on its own Hawthorne can also have no tempo at all
            juce::StringArray labels { "2/4", "3/4", "4/4", "5/4", "6/4", "7/4" };
            std::vector<int> values { 2, 3, 4, 5, 6, 7 };
            if (! wizardAsksStudio()) { labels.add ("NO TEMPO"); values.push_back (0); }
            add (labels, values, choice.bpm > 0.0 ? choice.beatsPerBar : 0, 74, [this] (int v)
            {
                if (v == 0) { choice.bpm = 0.0; bpmEd.setVisible (false); }
                else
                {
                    choice.beatsPerBar = v;
                    if (choice.bpm <= 0.0) { choice.bpm = 120.0; bpmEd.setVisible (true); readBpm(); }
                }
            });
            if (! wizardAsksStudio() && ! chips.empty()) chips.back().r.setWidth (110);
        }
    }

    int chipAt (juce::Point<int> p) const
    {
        for (size_t i = 0; i < chips.size(); ++i) if (chips[i].r.contains (p)) return (int) i;
        return -1;
    }

    //--------------------------------------------------------------------------
    // the cards
    void cardFrame (juce::Graphics& g, juce::Rectangle<int> r, bool chosen, bool hot) const
    {
        g.setColour (look.panel);
        g.fillRoundedRectangle (r.toFloat(), 6.0f);
        g.setColour (chosen ? look.accent : hot ? look.dim : look.panel.brighter (0.15f));
        g.drawRoundedRectangle (r.toFloat().reduced (1.0f), 6.0f, chosen ? 2.5f : 1.0f);
    }

    /** Name (with IN USE / CHOSEN), where and when, the story, and the sound: as in the machine room. */
    void cardText (juce::Graphics& g, juce::Rectangle<int> a, bool chosen, const juce::String& name, const juce::String& maker,
                   const juce::String& story, const juce::String& sound, int storyLines) const
    {
        auto title = a.removeFromTop (22);
        if (chosen)
        {
            g.setColour (look.accent);
            g.setFont (font (12.0f, true));
            g.drawText ("CHOSEN", title.removeFromRight (70), juce::Justification::centredRight);
        }
        g.setColour (look.text);
        g.setFont (font (16.0f, true));
        g.drawFittedText (name, title, juce::Justification::centredLeft, 1, 0.8f);
        g.setColour (look.dim);
        g.setFont (font (12.0f, true));
        g.drawFittedText (maker, a.removeFromTop (17), juce::Justification::centredLeft, 1, 0.8f);
        a.removeFromTop (4);
        if (story.isNotEmpty())
        {
            g.setColour (look.text.withAlpha (0.9f));
            g.setFont (font (13.0f, false));
            g.drawFittedText (story, a.removeFromTop (storyLines * 17), juce::Justification::topLeft, storyLines, 1.0f);
        }
        if (sound.isNotEmpty())
        {
            g.setColour (look.accent.brighter (0.2f));
            g.setFont (font (13.0f, true));
            g.drawFittedText ("Sound: " + sound, a, juce::Justification::topLeft, 3, 1.0f);
        }
    }

    void paintCard (juce::Graphics& g, juce::Rectangle<int> r, int value, bool hot) const
    {
        const bool chosen = isChosen (value);
        cardFrame (g, r, chosen, hot);
        auto a = r.reduced (12);
        switch (steps[(size_t) index])
        {
            case Step::machine:
            {
                drawMachinePreview (g, a.removeFromTop (106).toFloat(), MachineSkin::multitrack (value));
                a.removeFromTop (8);
                const auto& st = multitrackStory (value);
                cardText (g, a, chosen, tape::multitrackMachine (value).name, st.maker, st.history, st.sound, 5);
                break;
            }
            case Step::tape:
            {
                auto pic = a.removeFromLeft (150);
                drawTapeBox (g, pic.toFloat().withSizeKeepingCentre (150.0f, 150.0f), value, font (13.0f, true));
                a.removeFromLeft (14);
                const auto& info = tape::info ((tape::Stock) value);
                cardText (g, a, chosen, info.name, tape::era ((tape::Stock) value), info.description, {}, 9);
                break;
            }
            case Step::nr:
            {
                drawNrUnit (g, a.removeFromTop (74).toFloat(), value, font (12.0f, true));
                a.removeFromTop (10);
                cardText (g, a, chosen, nr::info (value).name, nr::era (value), nr::info (value).description, {}, 5);
                break;
            }
           #if defined (MELVILLE_HOSTED) && MELVILLE_HOSTED
            case Step::desk:
            {
                for (auto& d : melville::deskCards())
                    if (d.id == value)
                    {
                        if (d.draw) d.draw (g, a.removeFromTop (110).toFloat().withSizeKeepingCentre ((float) juce::jmin (a.getWidth(), 260), 110.0f));
                        a.removeFromTop (8);
                        cardText (g, a, chosen, d.name, d.maker, d.history, d.sound, 5);
                    }
                break;
            }
           #endif
            case Step::wiring:
            {
                drawBay (g, a.removeFromTop (130).toFloat(), value != 0);
                a.removeFromTop (10);
                if (value != 0)
                    cardText (g, a, chosen, "The usual cables", "Your usual wiring, or MELVILLE's",
                              "Interface into the tracks, the tracks through EMERSON to WHITMAN and back for the 2-track, "
                              "the mix and the click to TWAIN, TWAIN to the speakers and headphones: all as cables on the bay. "
                              "Pull or move any of them.", {}, 6);
                else
                    cardText (g, a, chosen, "An empty patchbay", "Nothing connected",
                              "Every cable out: nothing reaches the tape, the desk or the speakers until you patch it. "
                              "For building the studio up from scratch.", {}, 6);
                break;
            }
            default: break;
        }
    }

    void paintChips (juce::Graphics& g, const juce::String& title) const
    {
        if (chips.empty()) return;
        g.setColour (look.dim);
        g.setFont (font (14.0f, true));
        g.drawText (title, 30, chips.front().r.getY(), 120, 34, juce::Justification::centredLeft);
        for (size_t i = 0; i < chips.size(); ++i)
        {
            const auto& c = chips[i];
            auto r = c.r.toFloat();
            g.setColour (c.chosen ? look.accent : look.panel);
            g.fillRoundedRectangle (r, 4.0f);
            if (! c.chosen && (int) i == hoverChip) { g.setColour (look.dim); g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f); }
            g.setColour (c.chosen ? look.onAccent : look.text);
            g.setFont (font (15.0f, true));
            g.drawText (c.label, c.r, juce::Justification::centred);
        }
    }

    void paintSession (juce::Graphics& g) const
    {
        g.setColour (look.dim);
        g.setFont (font (14.0f, true));
        g.drawText ("SESSION NAME", 60, 184, 300, 20, juce::Justification::centredLeft);
        g.drawText ("ARTIST", 60, 294, 300, 20, juce::Justification::centredLeft);
        // a stack of tape boxes, waiting
        auto shelf = juce::Rectangle<float> (60.0f, 420.0f, (float) getWidth() - 120.0f, 150.0f);
        const int show[] = { 8, 5, 3, 9, 10, 1 };
        const float bw = 120.0f, gap = (shelf.getWidth() - 6 * bw) / 5.0f;
        for (int i = 0; i < 6; ++i)
            drawTapeBox (g, { shelf.getX() + (float) i * (bw + gap), shelf.getY(), bw, bw }, show[i], font (12.0f, true));
    }

    /** A strip of patchbay: two rows of jacks, with cables hanging between them (or none). */
    void drawBay (juce::Graphics& g, juce::Rectangle<float> r, bool cables) const
    {
        auto bay = r.withSizeKeepingCentre (juce::jmin (r.getWidth(), 360.0f), 110.0f);
        g.setColour (juce::Colour (0xff1c1d1f));
        g.fillRoundedRectangle (bay, 4.0f);
        const int n = 10;
        const float pitch = bay.getWidth() / (float) n;
        std::vector<juce::Point<float>> top, bottom;
        for (int i = 0; i < n; ++i)
        {
            const float x = bay.getX() + pitch * ((float) i + 0.5f);
            top.push_back ({ x, bay.getY() + 30.0f });
            bottom.push_back ({ x, bay.getBottom() - 30.0f });
        }
        for (auto& p : top)    { g.setColour (juce::Colour (0xff3a3c40)); g.fillEllipse (juce::Rectangle<float> (13.0f, 13.0f).withCentre (p)); g.setColour (juce::Colours::black); g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (p)); }
        for (auto& p : bottom) { g.setColour (juce::Colour (0xff3a3c40)); g.fillEllipse (juce::Rectangle<float> (13.0f, 13.0f).withCentre (p)); g.setColour (juce::Colours::black); g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (p)); }
        if (! cables) return;
        const juce::Colour cols[] = { juce::Colour (0xffc8322a), juce::Colour (0xffe0b23a), juce::Colour (0xff2f6fae), juce::Colour (0xff3f8f4f) };
        const int from[] = { 0, 1, 2, 3, 5, 7, 8 }, to[] = { 2, 3, 6, 4, 9, 0, 5 };
        for (int k = 0; k < 7; ++k)
        {
            const auto a = top[(size_t) from[k]], b = bottom[(size_t) to[k]];
            juce::Path p;
            p.startNewSubPath (a);
            p.cubicTo (a.translated (0.0f, 40.0f), b.translated (0.0f, -40.0f), b);
            g.setColour (cols[k % 4]);
            g.strokePath (p, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

    /** A metronome, swinging at the tempo chosen (still with no tempo). */
    void paintTempo (juce::Graphics& g) const
    {
        g.setColour (look.dim);
        g.setFont (font (14.0f, true));
        if (choice.bpm > 0.0) g.drawText ("BEATS PER MINUTE", 60, 184, 300, 20, juce::Justification::centredLeft);
        else                  g.drawText ("NO TEMPO: the bar counter and click are off for this reel", 60, 184, 600, 20, juce::Justification::centredLeft);
        const juce::Point<float> base ((float) getWidth() - 260.0f, 520.0f);
        juce::Path body;
        body.startNewSubPath (base.x - 90.0f, base.y);
        body.lineTo (base.x - 40.0f, base.y - 300.0f);
        body.lineTo (base.x + 40.0f, base.y - 300.0f);
        body.lineTo (base.x + 90.0f, base.y);
        body.closeSubPath();
        g.setColour (juce::Colour (0xff5a3b26));
        g.fillPath (body);
        g.setColour (juce::Colour (0xfff1ead6));
        g.fillRect (base.x - 14.0f, base.y - 270.0f, 28.0f, 200.0f);
        const float lean = choice.bpm > 0.0 ? juce::jlimit (-0.45f, 0.45f, 0.45f - (float) (choice.bpm - 40.0) / 300.0f) : 0.0f;
        const juce::Point<float> pivot (base.x, base.y - 50.0f);
        const auto tip = pivot.translated (std::sin (lean) * 250.0f, -std::cos (lean) * 250.0f);
        g.setColour (juce::Colour (0xff1b1916));
        g.drawLine ({ pivot, tip }, 4.0f);
        g.setColour (look.accent);
        const auto weight = pivot.translated (std::sin (lean) * 160.0f, -std::cos (lean) * 160.0f);
        g.fillRect (juce::Rectangle<float> (26.0f, 18.0f).withCentre (weight));
        if (choice.bpm > 0.0)
        {
            g.setColour (look.text);
            g.setFont (font (16.0f, true));
            g.drawText (juce::String (choice.beatsPerBar) + "/4", 280, 230, 120, 30, juce::Justification::centredLeft);
        }
    }

    void readBpm()
    {
        const double v = bpmEd.getText().getDoubleValue();
        if (v > 0.0) choice.bpm = v;
        bpmEd.setColour (juce::TextEditor::outlineColourId, look.dim.withAlpha (0.6f));
    }

    void paintReady (juce::Graphics& g) const
    {
        // the box, the machine and the rack, and the label on the box
        drawTapeBox (g, { 40.0f, 140.0f, 300.0f, 300.0f }, choice.stock, font (20.0f, true));
        drawMachinePreview (g, { 380.0f, 140.0f, 300.0f, 130.0f }, MachineSkin::multitrack (choice.machine));
        drawNrUnit (g, { 380.0f, 300.0f, 300.0f, 90.0f }, choice.nr, font (12.0f, true));

        auto label = juce::Rectangle<float> (720.0f, 140.0f, (float) getWidth() - 760.0f, 400.0f);
        g.setColour (juce::Colour (0xfff1ead6));
        g.fillRoundedRectangle (label, 3.0f);
        const juce::Colour rule (0xff9db4c9), ink (0xff1b2a6b), key (0xff5d6a7a);
        auto l = label.reduced (14.0f, 12.0f);
        auto ruled = [&] (juce::Rectangle<float> r)      // (the ruled line under a line of writing)
        {
            g.setColour (rule);
            g.drawHorizontalLine ((int) r.getBottom() + 2, label.getX() + 10.0f, label.getRight() - 10.0f);
        };
        auto t1 = l.removeFromTop (26.0f);
        g.setColour (ink);
        g.setFont (font (18.0f, true));
        g.drawFittedText (forSession.isNotEmpty() ? forSession : choice.name, t1.toNearestInt(), juce::Justification::bottomLeft, 1, 0.7f);
        ruled (t1);
        auto t2 = l.removeFromTop (24.0f);
        g.setColour (ink);
        g.setFont (font (13.0f, false));
        g.drawText (forSession.isNotEmpty() ? juce::String ("NEW REEL") : (choice.artist.isNotEmpty() ? choice.artist : juce::String ("-")),
                    t2, juce::Justification::bottomLeft);
        ruled (t2);
        l.removeFromTop (4.0f);
        auto row = [&] (const juce::String& k, const juce::String& v)
        {
            auto r = l.removeFromTop (34.0f);
            ruled (r);
            g.setColour (key);
            g.setFont (font (11.0f, true));
            g.drawText (k, r.removeFromLeft (70.0f), juce::Justification::bottomLeft);
            g.setColour (ink);
            g.setFont (font (15.0f, true));
            g.drawFittedText (v, r.toNearestInt(), juce::Justification::bottomLeft, 1, 0.7f);
        };
        row ("MACHINE", tape::multitrackMachine (choice.machine).name);
        row ("TRACKS", juce::String (choice.tracks));
        row ("TAPE", tape::info ((tape::Stock) choice.stock).name);
        row ("SPEED", choice.ips == 7.5 ? juce::String (juce::CharPointer_UTF8 ("7\xc2\xbd ips")) : juce::String ((int) choice.ips) + " ips");
        row ("NR", nr::info (choice.nr).name);
        row ("TEMPO", choice.bpm > 0.0 ? juce::String (choice.bpm, choice.bpm == std::floor (choice.bpm) ? 0 : 1) + " bpm  " + juce::String (choice.beatsPerBar) + "/4"
                                       : juce::String ("none"));
       #if defined (MELVILLE_HOSTED) && MELVILLE_HOSTED
        if (wizardAsksStudio())
        {
            for (auto& d : melville::deskCards())
                if (d.id == choice.desk) row ("DESK", d.name);
            row ("BAY", choice.usualCables ? "the usual cables" : "empty");
        }
       #endif

        g.setColour (look.dim);
        g.setFont (font (14.0f, false));
        g.drawText ("Fixed for the reel's life: the tracks and the tape. Change the machine, NR and tempo whenever you like.",
                    40, 570, getWidth() - 80, 22, juce::Justification::centredLeft);
    }

    NewReelChoice choice;
    Look look;
    juce::String forSession;
    std::function<void (const NewReelChoice&)> onCreate;
    std::vector<Step> steps;
    std::vector<Chip> chips;
    int index = 0, hoverChip = -1;
    CardList list;
    juce::Viewport view;
    juce::TextEditor nameEd, artistEd, bpmEd;
    juce::TextButton backButton { "BACK" }, nextButton { "NEXT" }, cancelButton { "CANCEL" };

    JUCE_DECLARE_NON_COPYABLE (NewReelWizard)
};

/** Opens the wizard over `over`: in MELVILLE's own window frame inside MELVILLE, else a modal window. */
inline void showNewReelWizard (juce::Component* over, const juce::String& title, bool withSession, const NewReelChoice& start,
                               const NewReelWizard::Look& look, const juce::String& sessionName,
                               std::function<void (const NewReelChoice&)> onCreate)
{
    auto* wizard = new NewReelWizard (withSession, start, look, sessionName, std::move (onCreate));
   #if defined (MELVILLE_HOSTED) && MELVILLE_HOSTED
    if (melville::isHosted())
    {
        melville::showPanel (std::unique_ptr<juce::Component> (wizard), title, "melville");
        return;
    }
   #endif
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (wizard);
    o.dialogTitle = title;
    o.dialogBackgroundColour = look.bg;
    o.componentToCentreAround = over;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.launchAsync();
}

} // namespace hawthorne
