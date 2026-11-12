#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "MachineSkin.h"
#include "MachinePicker.h"
#include "TapeChoice.h"
#include "../Engine/TapeStock.h"
#include "../Engine/NoiseReduction.h"

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
};

//==============================================================================
/** A tape box, drawn: the stock's colours, its name on the band, and the reel through the window. */
inline void drawTapeBox (juce::Graphics& g, juce::Rectangle<float> r, int stock, const juce::Font& font)
{
    struct Look { juce::uint32 body, band; };
    static const Look looks[] = {
        { 0xffeceae4, 0xffc9c6be },   // clean
        { 0xffe9e4d6, 0xffc8322a },   // 456-type
        { 0xff1d1d1f, 0xffc9a648 },   // GP9-type
        { 0xffc9b48a, 0xff6b3f22 },   // 250-type
        { 0xff2f4f7a, 0xffe8c547 },   // 911-type
        { 0xffe8e3d3, 0xff1f3e8c },   // Emitape
        { 0xff8d9196, 0xffe07a2c },   // 499-type
        { 0xff222428, 0xffb9bec4 },   // SM900-type
        { 0xff2f5a3a, 0xffe8e3d3 },   // EMI 888
        { 0xff7a2430, 0xffe8e3d3 },   // EMI 811
        { 0xff263f7a, 0xffe3b341 },   // EMI 815
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
    A new session (or a new reel on a session), step by step and in pictures: the session,
    then the machine (and the tracks), the tape (and the speed), the noise reduction, and
    a last look at the tape box before it's made.
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
        : choice (start), look (lk), forSession (sessionName), onCreate (std::move (create))
    {
        if (withSession) steps.push_back (Step::session);
        for (auto s : { Step::machine, Step::tape, Step::nr, Step::ready }) steps.push_back (s);
        stocks = stocksFor (tape::forMultitrack);

        for (auto* e : { &nameEd, &artistEd })
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
        setSize (1000, 740);
        go (0);
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
        layout();
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
        g.drawText (question(), 30, 60, getWidth() - 60, 34, juce::Justification::centredLeft);
        g.setColour (look.dim);
        g.setFont (font (15.0f, false));
        g.drawText (hint(), 30, 94, getWidth() - 60, 20, juce::Justification::centredLeft);

        switch (steps[(size_t) index])
        {
            case Step::session: paintSession (g); break;
            case Step::machine: paintMachines (g); break;
            case Step::tape:    paintTapes (g); break;
            case Step::nr:      paintNr (g); break;
            case Step::ready:   paintReady (g); break;
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const int h = hitAt (e.getPosition());
        if (h != hover) { hover = h; repaint(); }
        setMouseCursor (h >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    }
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (const int h = hitAt (e.getPosition()); h >= 0) { items[(size_t) h].pick(); layout(); repaint(); }
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        // a double-click on a card picks it and moves on
        if (const int h = hitAt (e.getPosition()); h >= 0 && items[(size_t) h].card) next();
    }

    /** (Development: every step as a picture, into a folder.) */
    static void gallery (const juce::File& dir, const Look& lk)
    {
        NewReelChoice c;
        c.name = "Sea Shanties"; c.artist = "The Pequod Crew"; c.stock = 8; c.machine = 4; c.nr = 1; c.ips = 15.0; c.tracks = 4 * 6;
        NewReelWizard w (true, c, lk, {}, nullptr);
        for (int i = 0; i < (int) w.steps.size(); ++i)
        {
            w.go (i);
            juce::PNGImageFormat png;
            juce::FileOutputStream out (dir.getChildFile ("wizard-" + juce::String (i + 1) + ".png"));
            if (out.openedOk()) { out.setPosition (0); out.truncate(); png.writeImageToStream (w.createComponentSnapshot (w.getLocalBounds()), out); }
        }
    }

private:
    enum class Step { session, machine, tape, nr, ready };

    struct Item
    {
        juce::Rectangle<int> r;
        bool card = false, chosen = false;
        int value = 0;
        juce::String label;
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
            case Step::ready:   return forSession.isNotEmpty() ? "A new reel for \"" + forSession + "\"" : "Ready to thread up";
        }
        return {};
    }

    juce::String hint() const
    {
        switch (steps[(size_t) index])
        {
            case Step::session: return "A session holds the reels for one job: an album, a single, a client.";
            case Step::machine: return "The machine can be changed later. The track count can't.";
            case Step::tape:    return "The tape stays with the reel for life. Slower tape: bigger low bump, darker top, more hiss.";
            case Step::nr:      return "Hiss reduction across the whole reel. Can be changed later, and switched per track.";
            case Step::ready:   return "Check the box. CREATE threads it up on the deck.";
        }
        return {};
    }

    //--------------------------------------------------------------------------
    void go (int i)
    {
        index = juce::jlimit (0, (int) steps.size() - 1, i);
        const bool session = steps[(size_t) index] == Step::session;
        nameEd.setVisible (session);
        artistEd.setVisible (session);
        if (session) nameEd.grabKeyboardFocus(); else grabKeyboardFocus();
        backButton.setEnabled (index > 0);
        nextButton.setButtonText (index == (int) steps.size() - 1 ? "CREATE" : "NEXT");
        hover = -1;
        layout();
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
        if (index < (int) steps.size() - 1) { go (index + 1); return; }

        const auto made = choice;
        auto create = onCreate;
        close();
        if (create) create (made);
    }

    void close()
    {
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>()) dw->exitModalState (0);
    }

    int hitAt (juce::Point<int> p) const
    {
        for (size_t i = 0; i < items.size(); ++i) if (items[i].r.contains (p)) return (int) i;
        return -1;
    }

    /** The cards and chips on this step, where they go and what a click does. */
    void layout()
    {
        items.clear();
        const int x0 = 30, w = getWidth() - 60;
        auto chips = [&] (int y, const juce::String& title, const juce::StringArray& labels, const std::vector<int>& values,
                          int current, std::function<void (int)> set, int cw = 74)
        {
            juce::ignoreUnused (title);
            const int gap = 8;
            for (int i = 0; i < labels.size(); ++i)
            {
                Item it;
                it.r = { x0 + 130 + i * (cw + gap), y, cw, 34 };
                it.label = labels[i];
                it.value = values[(size_t) i];
                it.chosen = values[(size_t) i] == current;
                const int v = values[(size_t) i];
                it.pick = [set, v] { set (v); };
                items.push_back (it);
            }
        };

        switch (steps[(size_t) index])
        {
            case Step::session: break;
            case Step::machine:
            {
                const int cols = 4, gap = 10, cw = (w - (cols - 1) * gap) / cols, ch = 150;
                int n = 0;
                for (int m : multitrackOrder())      // oldest first, digital last
                {
                    Item it;
                    it.card = true;
                    it.r = { x0 + (n % cols) * (cw + gap), 130 + (n / cols) * (ch + gap), cw, ch };
                    it.value = m;
                    it.chosen = choice.machine == m;
                    it.pick = [this, m] { choice.machine = m; };
                    items.push_back (it);
                    ++n;
                }
                chips (636, "TRACKS", { "8", "16", "24", "32", "48", "64", "72", "96" }, { 8, 16, 24, 32, 48, 64, 72, 96 },
                       choice.tracks, [this] (int v) { choice.tracks = v; });
                break;
            }
            case Step::tape:
            {
                const int cols = 6, gap = 12, cw = (w - (cols - 1) * gap) / cols, ch = 180;
                for (size_t n = 0; n < stocks.size(); ++n)
                {
                    Item it;
                    it.card = true;
                    it.r = { x0 + ((int) n % cols) * (cw + gap), 128 + ((int) n / cols) * (ch + gap), cw, ch };
                    it.value = stocks[n];
                    it.chosen = choice.stock == stocks[n];
                    const int s = stocks[n];
                    it.pick = [this, s] { choice.stock = s; };
                    items.push_back (it);
                }
                chips (556, "SPEED", { juce::String (juce::CharPointer_UTF8 ("7\xc2\xbd ips")), "15 ips", "30 ips" }, { 75, 150, 300 },
                       (int) std::round (choice.ips * 10.0), [this] (int v) { choice.ips = v / 10.0; }, 96);
                break;
            }
            case Step::nr:
            {
                const int cols = nr::numSystems, gap = 14, cw = (w - (cols - 1) * gap) / cols, ch = 290;
                for (int n = 0; n < nr::numSystems; ++n)
                {
                    Item it;
                    it.card = true;
                    it.r = { x0 + n * (cw + gap), 136, cw, ch };
                    it.value = n;
                    it.chosen = choice.nr == n;
                    it.pick = [this, n] { choice.nr = n; };
                    items.push_back (it);
                }
                break;
            }
            case Step::ready: break;
        }
    }

    void cardFrame (juce::Graphics& g, const Item& it, bool hot) const
    {
        auto r = it.r.toFloat();
        g.setColour (look.panel);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (it.chosen ? look.accent : hot ? look.dim : look.panel.brighter (0.15f));
        g.drawRoundedRectangle (r.reduced (1.0f), 6.0f, it.chosen ? 2.5f : 1.0f);
    }

    void paintChips (juce::Graphics& g, const juce::String& title, int y) const
    {
        g.setColour (look.dim);
        g.setFont (font (14.0f, true));
        g.drawText (title, 30, y, 120, 34, juce::Justification::centredLeft);
        for (size_t i = 0; i < items.size(); ++i)
        {
            const auto& it = items[i];
            if (it.card) continue;
            auto r = it.r.toFloat();
            g.setColour (it.chosen ? look.accent : look.panel);
            g.fillRoundedRectangle (r, 4.0f);
            if (! it.chosen && (int) i == hover) { g.setColour (look.dim); g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f); }
            g.setColour (it.chosen ? look.onAccent : look.text);
            g.setFont (font (15.0f, true));
            g.drawText (it.label, it.r, juce::Justification::centred);
        }
    }

    void paintSession (juce::Graphics& g) const
    {
        g.setColour (look.dim);
        g.setFont (font (14.0f, true));
        g.drawText ("SESSION NAME", 60, 184, 300, 20, juce::Justification::centredLeft);
        g.drawText ("ARTIST", 60, 294, 300, 20, juce::Justification::centredLeft);
        // a stack of tape boxes, waiting
        auto shelf = juce::Rectangle<float> (60.0f, 410.0f, (float) getWidth() - 120.0f, 150.0f);
        const int show[] = { 5, 8, 9, 10, 1, 4 };
        const float bw = 120.0f, gap = (shelf.getWidth() - 6 * bw) / 5.0f;
        for (int i = 0; i < 6; ++i)
            drawTapeBox (g, { shelf.getX() + (float) i * (bw + gap), shelf.getY(), bw, bw }, show[i], font (12.0f, true));
    }

    void paintMachines (juce::Graphics& g) const
    {
        for (size_t i = 0; i < items.size(); ++i)
        {
            const auto& it = items[i];
            if (! it.card) continue;
            cardFrame (g, it, (int) i == hover);
            auto a = it.r.reduced (8);
            drawMachinePreview (g, a.removeFromTop (84).toFloat(), MachineSkin::multitrack (it.value));
            a.removeFromTop (5);
            g.setColour (look.text);
            g.setFont (font (15.0f, true));
            g.drawFittedText (tape::multitrackMachine (it.value).name, a.removeFromTop (20), juce::Justification::centredLeft, 1, 0.8f);
            g.setColour (look.dim);
            g.setFont (font (12.0f, false));
            g.drawFittedText (multitrackStory (it.value).maker, a.removeFromTop (18), juce::Justification::centredLeft, 1, 0.8f);
        }
        // what the chosen machine sounds like
        g.setColour (look.text.withAlpha (0.9f));
        g.setFont (font (14.0f, false));
        g.drawFittedText (multitrackStory (choice.machine).sound, 30, 606, getWidth() - 60, 22, juce::Justification::centredLeft, 1, 0.8f);
        paintChips (g, "TRACKS", 636);
    }

    void paintTapes (juce::Graphics& g) const
    {
        for (size_t i = 0; i < items.size(); ++i)
        {
            const auto& it = items[i];
            if (! it.card) continue;
            cardFrame (g, it, (int) i == hover);
            auto a = it.r.reduced (10);
            drawTapeBox (g, a.removeFromTop (118).toFloat(), it.value, font (12.0f, true));
            a.removeFromTop (6);
            g.setColour (look.text);
            g.setFont (font (13.5f, true));
            g.drawFittedText (tape::info ((tape::Stock) it.value).name, a, juce::Justification::centredTop, 2, 0.8f);
        }
        // what the chosen tape sounds like
        g.setColour (look.text.withAlpha (0.9f));
        g.setFont (font (14.0f, false));
        g.drawFittedText (tape::info ((tape::Stock) choice.stock).description, 30, 506, getWidth() - 60, 42,
                          juce::Justification::topLeft, 2, 0.9f);
        paintChips (g, "SPEED", 556);
    }

    void paintNr (juce::Graphics& g) const
    {
        for (size_t i = 0; i < items.size(); ++i)
        {
            const auto& it = items[i];
            cardFrame (g, it, (int) i == hover);
            auto a = it.r.reduced (14);
            drawNrUnit (g, a.removeFromTop (110).toFloat(), it.value, font (12.0f, true));
            a.removeFromTop (12);
            g.setColour (look.text);
            g.setFont (font (17.0f, true));
            g.drawText (nr::info (it.value).name, a.removeFromTop (24), juce::Justification::centredLeft);
            g.setColour (look.text.withAlpha (0.85f));
            g.setFont (font (13.0f, false));
            g.drawFittedText (nr::info (it.value).description, a, juce::Justification::topLeft, 8, 0.9f);
        }
    }

    void paintReady (juce::Graphics& g) const
    {
        // the box, the machine, and the label on the box
        drawTapeBox (g, { 40.0f, 140.0f, 300.0f, 300.0f }, choice.stock, font (20.0f, true));
        drawMachinePreview (g, { 380.0f, 140.0f, 300.0f, 130.0f }, MachineSkin::multitrack (choice.machine));
        drawNrUnit (g, { 380.0f, 300.0f, 300.0f, 90.0f }, choice.nr, font (12.0f, true));

        auto label = juce::Rectangle<float> (720.0f, 140.0f, (float) getWidth() - 760.0f, 300.0f);
        g.setColour (juce::Colour (0xfff1ead6));
        g.fillRoundedRectangle (label, 3.0f);
        g.setColour (juce::Colour (0xff9db4c9));
        for (float y = label.getY() + 52.0f; y < label.getBottom() - 10.0f; y += 34.0f)
            g.drawHorizontalLine ((int) y, label.getX() + 10.0f, label.getRight() - 10.0f);
        auto l = label.reduced (14.0f, 12.0f);
        g.setColour (juce::Colour (0xff1b2a6b));
        g.setFont (font (18.0f, true));
        g.drawFittedText (forSession.isNotEmpty() ? forSession : choice.name, l.removeFromTop (24).toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);
        g.setFont (font (13.0f, false));
        g.drawText (forSession.isNotEmpty() ? juce::String ("NEW REEL") : (choice.artist.isNotEmpty() ? choice.artist : juce::String ("-")),
                    l.removeFromTop (18), juce::Justification::centredLeft);
        l.removeFromTop (8);
        auto row = [&] (const juce::String& k, const juce::String& v)
        {
            auto r = l.removeFromTop (34.0f);
            g.setColour (juce::Colour (0xff5d6a7a));
            g.setFont (font (11.0f, true));
            g.drawText (k, r.removeFromLeft (70.0f), juce::Justification::bottomLeft);
            g.setColour (juce::Colour (0xff1b2a6b));
            g.setFont (font (15.0f, true));
            g.drawFittedText (v, r.toNearestInt(), juce::Justification::bottomLeft, 1, 0.7f);
        };
        row ("MACHINE", tape::multitrackMachine (choice.machine).name);
        row ("TRACKS", juce::String (choice.tracks));
        row ("TAPE", tape::info ((tape::Stock) choice.stock).name);
        row ("SPEED", choice.ips == 7.5 ? juce::String (juce::CharPointer_UTF8 ("7\xc2\xbd ips")) : juce::String ((int) choice.ips) + " ips");
        row ("NR", nr::info (choice.nr).name);

        g.setColour (look.dim);
        g.setFont (font (14.0f, false));
        g.drawText ("Fixed for the reel's life: the tracks and the tape. Change the machine and NR whenever you like.",
                    40, 470, getWidth() - 80, 22, juce::Justification::centredLeft);
    }

    NewReelChoice choice;
    Look look;
    juce::String forSession;
    std::function<void (const NewReelChoice&)> onCreate;
    std::vector<Step> steps;
    std::vector<int> stocks;
    std::vector<Item> items;
    int index = 0, hover = -1;
    juce::TextEditor nameEd, artistEd;
    juce::TextButton backButton { "BACK" }, nextButton { "NEXT" }, cancelButton { "CANCEL" };

    JUCE_DECLARE_NON_COPYABLE (NewReelWizard)
};

/** Opens the wizard in a modal window over `over`. */
inline void showNewReelWizard (juce::Component* over, const juce::String& title, bool withSession, const NewReelChoice& start,
                               const NewReelWizard::Look& look, const juce::String& sessionName,
                               std::function<void (const NewReelChoice&)> onCreate)
{
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (new NewReelWizard (withSession, start, look, sessionName, std::move (onCreate)));
    o.dialogTitle = title;
    o.dialogBackgroundColour = look.bg;
    o.componentToCentreAround = over;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.launchAsync();
}

} // namespace hawthorne
