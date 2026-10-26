#include "LibraryView.h"
#include <juce_gui_extra/juce_gui_extra.h>
#include "SessionSheet.h"   // Paper colours
#include "UIHelpers.h"
#include "TapeChoice.h"

namespace hawthorne
{

namespace
{
    /** Tape box colours: plain stock boxes. */
    const juce::Colour boxPalette[] = {
        juce::Colour (0xffa63a2e), juce::Colour (0xffc8692a), juce::Colour (0xffd9a521), juce::Colour (0xffb38b5d),
        juce::Colour (0xffe1d3b0), juce::Colour (0xffeeeae0), juce::Colour (0xff4d6650), juce::Colour (0xff2f6f6a),
        juce::Colour (0xff3f6d8c), juce::Colour (0xff4e5d6c), juce::Colour (0xff7a5c8a), juce::Colour (0xff2e2d2a) };

    /** A session's colour: the one chosen for it, else picked from its name. */
    juce::Colour boxColour (const SessionSummary& s)
    {
        static const juce::Colour automatic[] = { juce::Colour (0xffb38b5d), juce::Colour (0xffa63a2e), juce::Colour (0xff4e5d6c),
                                                  juce::Colour (0xff4d6650), juce::Colour (0xffe1d3b0), juce::Colour (0xff2e2d2a),
                                                  juce::Colour (0xff7a5c8a), juce::Colour (0xff3f6d8c) };
        if (s.colour != 0) return juce::Colour (s.colour);
        return automatic[(size_t) (s.name.hashCode() & 0x7fffffff) % std::size (automatic)];
    }

    /** A reel's colour: its own, else its session's. */
    juce::Colour boxColour (const SessionSummary& s, const ReelSummary* r)
    {
        return r != nullptr && r->colour != 0 ? juce::Colour (r->colour) : boxColour (s);
    }

    /** Swatches in a call-out: pick a colour, a custom one, or go back to the default. */
    struct SwatchPanel : juce::Component
    {
        SwatchPanel (juce::Colour current, const juce::String& defaultLabel, std::function<void (juce::Colour)> pick)
            : onPick (std::move (pick)), currentColour (current)
        {
            for (auto c : boxPalette)
            {
                auto* b = swatches.add (new juce::TextButton());
                b->setColour (juce::TextButton::buttonColourId, c);
                b->setWantsKeyboardFocus (false);
                b->onClick = [this, c] { choose (c); };
                addAndMakeVisible (b);
            }
            defaultButton.setButtonText (defaultLabel);
            defaultButton.onClick = [this] { choose ({}); };
            customButton.onClick = [this]
            {
                // applied once, when the picker is closed (not on every drag)
                struct Picker : juce::ColourSelector
                {
                    Picker() : juce::ColourSelector (juce::ColourSelector::showColourspace | juce::ColourSelector::showSliders) {}
                    ~Picker() override { if (fn) fn (getCurrentColour().withAlpha (1.0f)); }
                    std::function<void (juce::Colour)> fn;
                };
                auto* sel = new Picker();
                sel->setCurrentColour (currentColour.isTransparent() ? boxPalette[0] : currentColour);
                sel->setSize (300, 300);
                sel->fn = onPick;
                juce::CallOutBox::launchAsynchronously (std::unique_ptr<juce::Component> (sel), getScreenBounds(), nullptr);
                if (auto* box = findParentComponentOfClass<juce::CallOutBox>()) box->dismiss();
            };
            for (auto* b : { &defaultButton, &customButton }) { b->setWantsKeyboardFocus (false); addAndMakeVisible (b); }
            setSize (6 * 40 + 20, 2 * 40 + 20 + 34);
        }

        void choose (juce::Colour c)
        {
            onPick (c);
            if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
                box->dismiss();
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (10);
            for (int i = 0; i < swatches.size(); ++i)
                swatches[i]->setBounds (r.getX() + (i % 6) * 40, r.getY() + (i / 6) * 40, 36, 36);
            auto row = r.removeFromBottom (28);
            defaultButton.setBounds (row.removeFromLeft (row.getWidth() / 2 - 3));
            row.removeFromLeft (6);
            customButton.setBounds (row);
        }

        std::function<void (juce::Colour)> onPick;
        juce::Colour currentColour;
        juce::OwnedArray<juce::TextButton> swatches;
        juce::TextButton defaultButton, customButton { "CUSTOM..." };
    };

    juce::Colour inkOn (juce::Colour box) { return box.getPerceivedBrightness() > 0.55f ? Paper::ink : Paper::paper; }

    juce::String reelLength (const ReelSummary& r)
    {
        return r.sampleRate > 0 ? formatTime (r.lengthSamples, r.sampleRate).dropLastCharacters (4) : juce::String ("--");
    }

    bool matches (const SessionSummary& s, const ReelSummary* r, const juce::String& q)
    {
        if (q.isEmpty()) return true;
        auto has = [&] (const juce::String& t) { return t.containsIgnoreCase (q); };
        if (has (s.name) || has (s.info["Artist"]) || has (s.info["Title"]) || has (s.info["Client / Label"])) return true;
        return r != nullptr && (has (r->name) || r->trackNames.joinIntoString (" ").containsIgnoreCase (q));
    }
}

//==============================================================================
class LibraryView::Shelves : public juce::Component
{
public:
    explicit Shelves (LibraryView& o) : owner (o) {}

    struct Spine { int session; int reel; juce::Rectangle<int> bounds; };
    struct Group { int session; juce::Rectangle<int> tag; juce::Rectangle<int> emptySlot; };

    static constexpr int spineW = 46, spineH = 236, shelfGap = 64, groupGap = 34, pad = 24;

    int minHeight = 0;

    void rebuild (int width)
    {
        spines.clear();
        groups.clear();
        shelfLines.clear();

        int x = pad, y = pad + 18;
        const auto q = owner.search.getText().trim();
        const bool showArchived = owner.showArchived.getToggleState();

        for (int si = 0; si < (int) owner.sessions.size(); ++si)
        {
            const auto& s = owner.sessions[(size_t) si];
            std::vector<int> visible;
            for (int ri = 0; ri < (int) s.reels.size(); ++ri)
                if ((showArchived || ! s.reels[(size_t) ri].archived) && matches (s, &s.reels[(size_t) ri], q))
                    visible.push_back (ri);

            const bool emptySession = s.reels.empty() || (! s.hasVisibleReels() && ! showArchived);
            if (visible.empty() && ! (emptySession && matches (s, nullptr, q) && s.reels.empty()))
                continue;

            const int count = juce::jmax (1, (int) visible.size());
            const int groupW = juce::jmax (count * spineW, 150);

            if (x + groupW > width - pad && x > pad)
            {
                shelfLines.push_back (y + spineH);
                x = pad;
                y += spineH + shelfGap;
            }

            Group g { si, { x, y + spineH + 8, groupW, 36 }, {} };
            if (visible.empty())
                g.emptySlot = { x, y + 20, spineW, spineH - 20 };

            for (int i = 0; i < (int) visible.size(); ++i)
                spines.push_back ({ si, visible[(size_t) i], { x + i * spineW, y, spineW - 3, spineH } });

            groups.push_back (g);
            x += groupW + groupGap;
        }

        shelfLines.push_back (y + spineH);
        setSize (width, juce::jmax (minHeight, y + spineH + shelfGap + pad));
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff171615));

        // shelves
        for (int lineY : shelfLines)
        {
            g.setColour (juce::Colour (0xff3a2d22));
            g.fillRect (pad / 2, lineY, getWidth() - pad, 10);
            g.setColour (juce::Colour (0xff5a4634));
            g.drawHorizontalLine (lineY, (float) pad / 2, (float) (getWidth() - pad / 2));
            g.setColour (juce::Colour (0x40000000));
            g.fillRect (pad / 2, lineY + 10, getWidth() - pad, 6);
        }

        for (auto& gr : groups)
        {
            const auto& s = owner.sessions[(size_t) gr.session];
            const bool sel = s.file == owner.selectedSessionFile;

            // session tag on the shelf lip
            auto tag = gr.tag.toFloat();
            g.setColour (sel ? Paper::paper : Paper::paperDark);
            g.fillRoundedRectangle (tag, 2.0f);
            g.setColour (sel ? Colours::mustard : Paper::faint);
            g.drawRoundedRectangle (tag, 2.0f, sel ? 2.0f : 1.0f);
            g.setColour (Paper::ink);
            g.setFont (Paper::type (13.0f, true));
            g.drawText (s.name.toUpperCase(), gr.tag.reduced (6, 2).removeFromTop (17), juce::Justification::centredLeft, true);
            g.setFont (Paper::type (11.0f));
            g.drawText (s.info["Artist"], gr.tag.reduced (6, 2).removeFromBottom (14), juce::Justification::centredLeft, true);

            if (! gr.emptySlot.isEmpty())
            {
                g.setColour (Colours::textDim.withAlpha (0.5f));
                juce::Path p;
                p.addRoundedRectangle (gr.emptySlot.toFloat(), 3.0f);
                const float dash[] = { 5.0f, 4.0f };
                juce::PathStrokeType (1.0f).createDashedStroke (p, p, dash, 2);
                g.fillPath (p);
                g.setFont (uiFont (11.0f));
                g.drawFittedText ("no reels", gr.emptySlot.reduced (2), juce::Justification::centred, 2);
            }
        }

        for (auto& sp : spines)
            paintSpine (g, sp);

        if (owner.sessions.empty())
        {
            g.setColour (Colours::textDim);
            g.setFont (uiFont (16.0f));
            g.drawFittedText ("The library is empty.\nClick NEW SESSION to put the first tape on the shelf.",
                              getLocalBounds().reduced (40), juce::Justification::centred, 3);
        }
    }

    void paintSpine (juce::Graphics& g, const Spine& sp)
    {
        const auto& s = owner.sessions[(size_t) sp.session];
        const auto& r = s.reels[(size_t) sp.reel];
        const bool sel = r.file == owner.selectedReelFile;
        const bool deck = owner.isOnDeck (r.file);

        auto b = sp.bounds.toFloat().translated (0.0f, sel ? -16.0f : 0.0f);
        const auto colour = boxColour (s, &r);

        // box spine with a little shading
        juce::ColourGradient shade (colour.brighter (0.15f), b.getX(), 0, colour.darker (0.35f), b.getRight(), 0, false);
        g.setGradientFill (shade);
        g.fillRect (b);
        if (r.archived)
        {
            g.setColour (juce::Colour (0x88000000));
            g.fillRect (b);
        }
        g.setColour (sel ? Colours::mustard : juce::Colours::black.withAlpha (0.6f));
        g.drawRect (b, sel ? 2.0f : 1.0f);

        // reel number at the top
        g.setColour (inkOn (colour));
        g.setFont (Paper::type (17.0f, true));
        g.drawText (r.name.retainCharacters ("0123456789").trimCharactersAtStart ("0"), b.withHeight (26.0f),
                    juce::Justification::centred);

        // typed label, reading bottom-to-top
        auto label = b.reduced (7.0f, 0.0f).withTrimmedTop (30.0f).withTrimmedBottom (34.0f);
        g.setColour (Paper::paper);
        g.fillRect (label);

        g.saveState();
        g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, label.getCentreX(), label.getCentreY()));
        auto rotated = juce::Rectangle<float> (label.getHeight(), label.getWidth()).withCentre (label.getCentre()).reduced (6.0f, 1.0f);
        g.setColour (Paper::ink);
        g.setFont (Paper::type (12.0f, true));
        const auto title = s.info["Title"].isNotEmpty() ? s.info["Title"] : s.name;
        g.drawText ((s.info["Artist"].isNotEmpty() ? s.info["Artist"] + " - " : juce::String()) + title,
                    rotated.removeFromTop (rotated.getHeight() * 0.55f), juce::Justification::centredLeft, true);
        g.setFont (Paper::type (10.0f));
        g.drawText (r.name + "  " + juce::String (r.numTracks) + "T  " + reelLength (r), rotated,
                    juce::Justification::centredLeft, true);
        g.restoreState();

        // bottom: track count / on-deck lamp
        auto foot = b.withTrimmedTop (b.getHeight() - 30.0f);
        if (deck)
        {
            g.setColour (Colours::mustard);
            g.fillEllipse (foot.withSizeKeepingCentre (9.0f, 9.0f).translated (0, -5.0f));
            g.setFont (Paper::type (8.0f, true));
            g.drawText ("DECK", foot.withTrimmedTop (16.0f), juce::Justification::centred);
        }
        else
        {
            g.setColour (inkOn (colour).withAlpha (0.8f));
            g.setFont (Paper::type (11.0f, true));
            g.drawText (juce::String (r.numTracks) + "T", foot, juce::Justification::centred);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        for (auto& sp : spines)
            if (sp.bounds.expanded (0, 16).contains (e.getPosition()))
            {
                owner.selectedSessionFile = owner.sessions[(size_t) sp.session].file;
                owner.selectedReelFile = owner.sessions[(size_t) sp.session].reels[(size_t) sp.reel].file;
                owner.updateSelection();
                return;
            }

        for (auto& gr : groups)
            if (gr.tag.contains (e.getPosition()) || gr.emptySlot.contains (e.getPosition()))
            {
                owner.selectedSessionFile = owner.sessions[(size_t) gr.session].file;
                owner.selectedReelFile = juce::File();
                owner.updateSelection();
                return;
            }
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        for (auto& sp : spines)
            if (sp.bounds.contains (e.getPosition()))
            {
                owner.loadSelected();
                return;
            }
    }

    juce::Rectangle<int> boundsOf (const juce::File& reelFile) const
    {
        for (auto& sp : spines)
            if (owner.sessions[(size_t) sp.session].reels[(size_t) sp.reel].file == reelFile)
                return sp.bounds;
        return {};
    }

private:
    LibraryView& owner;
    std::vector<Spine> spines;
    std::vector<Group> groups;
    std::vector<int> shelfLines;
};

//==============================================================================
/** The selected tape box, pulled off the shelf: its label, the tracks and the latest takes. */
class LibraryView::BoxView : public juce::Component
{
public:
    explicit BoxView (LibraryView& o) : owner (o) {}

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Colours::panel);
        auto r = getLocalBounds().reduced (20);

        const auto* s = owner.selectedSession();
        const auto* reel = owner.selectedReel();

        if (s == nullptr)
        {
            g.setColour (Colours::textDim);
            g.setFont (uiFont (15.0f));
            g.drawFittedText ("Pick a tape off the shelf.", r, juce::Justification::centred, 2);
            return;
        }

        // the box front
        const auto colour = boxColour (*s, reel);
        auto boxRect = r.toFloat();
        g.setColour (juce::Colour (0x50000000));
        g.fillRoundedRectangle (boxRect.translated (4.0f, 5.0f), 3.0f);
        g.setColour (colour);
        g.fillRoundedRectangle (boxRect, 3.0f);
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.drawRoundedRectangle (boxRect, 3.0f, 1.0f);

        // typed paper label
        auto label = boxRect.reduced (16.0f);
        g.setColour (Paper::paper);
        g.fillRect (label);
        g.setColour (Paper::margin.withAlpha (0.6f));
        g.drawHorizontalLine ((int) label.getY() + 30, label.getX(), label.getRight());

        auto text = label.reduced (14.0f, 8.0f);
        auto line = [&] (float h) { return text.removeFromTop (h); };

        g.setColour (Paper::ink);
        g.setFont (Paper::type (15.0f, true));
        g.drawText ((s->info["Studio"].isNotEmpty() ? s->info["Studio"] : juce::String ("HAWTHORNE")).toUpperCase()
                        + "  -  MULTITRACK MASTER", line (22.0f), juce::Justification::centredLeft, true);
        text.removeFromTop (12.0f);

        auto field = [&] (const juce::String& name, const juce::String& value, bool big = false)
        {
            auto row = line (big ? 26.0f : 20.0f);
            g.setColour (Paper::faint);
            g.setFont (Paper::type (11.0f, true));
            g.drawText (name, row.removeFromLeft (96.0f), juce::Justification::centredLeft);
            g.setColour (Paper::ink);
            g.setFont (Paper::type (big ? 18.0f : 14.0f, big));
            g.drawText (value.isNotEmpty() ? value : juce::String ("-"), row, juce::Justification::centredLeft, true);
        };

        field ("ARTIST", s->info["Artist"], true);
        field ("TITLE", s->info["Title"].isNotEmpty() ? s->info["Title"] : s->name, true);
        field ("SESSION", s->name);
        if (s->info["Producer"].isNotEmpty() || s->info["Engineer"].isNotEmpty())
            field ("PROD / ENG", s->info["Producer"] + (s->info["Engineer"].isNotEmpty() ? "  /  " + s->info["Engineer"] : juce::String()));

        if (reel == nullptr)
        {
            text.removeFromTop (10.0f);
            g.setColour (Paper::faint);
            g.setFont (Paper::type (13.0f));
            g.drawFittedText (s->reels.empty() ? "No reels in this session yet - click NEW REEL."
                                               : juce::String ((int) s->reels.size()) + " reel(s) - pick one off the shelf.",
                              text.removeFromTop (60.0f).toNearestInt(), juce::Justification::topLeft, 3);
            return;
        }

        const int reelNo = s->reels.empty() ? 0 : (int) (std::find_if (s->reels.begin(), s->reels.end(),
                                [&] (auto& x) { return x.file == reel->file; }) - s->reels.begin()) + 1;
        field ("REEL", juce::String (reelNo) + " of " + juce::String ((int) s->reels.size()) + (reel->archived ? "   (archived)" : ""));
        field ("TRACKS", juce::String (reel->numTracks) + "   " + juce::String (reel->sampleRate / 1000.0, 1) + " kHz / 24-bit");
        field ("LENGTH", reelLength (*reel));
        field ("DATE", reel->info["Date"]);
        if (! reel->masters.isEmpty())
            field ("MASTERS", reel->masters.joinIntoString (",  "));
        field ("SPEED", Reel::formatIps (reel->ips) + " ips");
        field ("MACHINE", tape::multitrackMachine (reel->machine).name);
        field ("TAPE", juce::StringArray { tape::info ((tape::Stock) reel->tapeStock).label, reel->info["EQ"], reel->info["Noise Reduction"] }
                            .joinIntoString ("  ").trim());

        // tracks on the tape
        text.removeFromTop (10.0f);
        g.setColour (Paper::faint);
        g.setFont (Paper::type (11.0f, true));
        g.drawText ("ON THIS TAPE", line (18.0f), juce::Justification::centredLeft);

        const int perCol = 12;
        auto tracksArea = line (perCol * 15.0f);
        const int cols = 3;
        const float colW = tracksArea.getWidth() / (float) cols;
        g.setColour (Paper::ink);
        g.setFont (Paper::type (12.0f));
        const int shown = juce::jmin (reel->trackNames.size(), perCol * cols);
        for (int i = 0; i < shown; ++i)
            g.drawText (reel->trackNames[i], juce::Rectangle<float> (tracksArea.getX() + (float) (i / perCol) * colW,
                                                                     tracksArea.getY() + (float) (i % perCol) * 15.0f, colW - 6.0f, 15.0f),
                        juce::Justification::centredLeft, true);
        if (reel->trackNames.isEmpty())
            g.drawText ("(blank tape)", tracksArea.withHeight (15.0f), juce::Justification::centredLeft);
        else if (reel->trackNames.size() > shown)
            g.drawText ("+ " + juce::String (reel->trackNames.size() - shown) + " more", tracksArea.withTrimmedTop (tracksArea.getHeight() - 15.0f),
                        juce::Justification::centredRight);

        // latest takes
        text.removeFromTop (8.0f);
        g.setColour (Paper::faint);
        g.setFont (Paper::type (11.0f, true));
        g.drawText ("TAKES  (" + juce::String ((int) reel->takes.size()) + ")", line (18.0f), juce::Justification::centredLeft);
        g.setFont (Paper::type (12.0f));
        const int maxTakes = juce::jmax (0, (int) (text.getHeight() / 16.0f));
        const int first = juce::jmax (0, (int) reel->takes.size() - maxTakes);
        for (int i = first; i < (int) reel->takes.size(); ++i)
        {
            const auto& tk = reel->takes[(size_t) i];
            g.setColour (tk.status == "Keeper" ? Paper::margin : Paper::ink);
            g.drawText (juce::String (tk.take).paddedLeft (' ', 3) + "  "
                            + formatTime (tk.start, reel->sampleRate).dropLastCharacters (4) + "  trk " + tk.tracks + "  "
                            + tk.status + (tk.song.isNotEmpty() ? "  " + tk.song : juce::String()),
                        line (16.0f), juce::Justification::centredLeft, true);
        }
    }

private:
    LibraryView& owner;
};

//==============================================================================
LibraryView::LibraryView (juce::ApplicationProperties& p) : props (p)
{
    setWantsKeyboardFocus (true);

    shelves = std::make_unique<Shelves> (*this);
    box = std::make_unique<BoxView> (*this);

    shelfView.setViewedComponent (shelves.get(), false);
    shelfView.setScrollBarsShown (true, false);
    addAndMakeVisible (shelfView);
    addAndMakeVisible (*box);

    search.setTextToShowWhenEmpty ("Search artist, title, session, track...", Colours::textDim);
    search.setFont (uiFont (14.0f));
    search.onTextChange = [this] { shelves->rebuild (shelfView.getMaximumVisibleWidth()); };
    addAndMakeVisible (search);

    showArchived.onClick = [this] { shelves->rebuild (shelfView.getMaximumVisibleWidth()); };
    showArchived.setWantsKeyboardFocus (false);
    addAndMakeVisible (showArchived);

    newSessionButton.setColour (juce::TextButton::buttonColourId, Colours::mustard);
    newSessionButton.setColour (juce::TextButton::textColourOffId, Colours::black);
    loadButton.setColour (juce::TextButton::buttonColourId, Colours::mustard);
    loadButton.setColour (juce::TextButton::textColourOffId, Colours::black);

    newSessionButton.onClick = [this] { newSession(); };
    backButton.onClick       = [this] { if (onBackToDeck) onBackToDeck(); };
    folderButton.onClick     = [this] { changeFolder(); };
    loadButton.onClick       = [this] { loadSelected(); };
    newReelButton.onClick    = [this] { newReel(); };
    revealButton.onClick     = [this] { revealSelected(); };
    archiveButton.onClick    = [this] { archiveSelected(); };
    deleteButton.onClick     = [this] { deleteSelected(); };
    colourButton.onClick     = [this] { chooseColour(); };
    colourButton.setTooltip ("Box colour for this reel, or for the whole session (its reels follow it unless given their own)");

    for (auto* b : { &newSessionButton, &backButton, &folderButton, &loadButton, &newReelButton, &revealButton, &archiveButton, &deleteButton, &colourButton })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }

    updateSelection();
}

LibraryView::~LibraryView() = default;

juce::File LibraryView::getLibraryFolder() const
{
    const auto saved = props.getUserSettings()->getValue ("libraryFolder");
    return saved.isNotEmpty() ? juce::File (saved) : Library::defaultFolder();
}

void LibraryView::chooseColour()
{
    const auto* s = selectedSession();
    if (s == nullptr) return;
    const auto* r = selectedReel();

    auto openSwatches = [this] (juce::File file, juce::Colour current, juce::String defaultLabel)
    {
        auto panel = std::make_unique<SwatchPanel> (current, defaultLabel, [this, file] (juce::Colour c) { applyColour (file, c); });
        juce::CallOutBox::launchAsynchronously (std::move (panel), colourButton.getScreenBounds(), nullptr);
    };

    if (r == nullptr)
    {
        openSwatches (s->file, juce::Colour (s->colour), "AUTOMATIC");
        return;
    }

    juce::PopupMenu m;
    m.addItem (1, "This reel (" + r->name + ")");
    m.addItem (2, "Whole session - its reels follow unless they have their own");
    const auto reelFile = r->file, sessionFile = s->file;
    const auto reelColour = juce::Colour (r->colour), sessionColour = juce::Colour (s->colour);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (colourButton),
                     [openSwatches, reelFile, sessionFile, reelColour, sessionColour] (int result)
    {
        if (result == 1) openSwatches (reelFile, reelColour, "SESSION'S");
        if (result == 2) openSwatches (sessionFile, sessionColour, "AUTOMATIC");
    });
}

void LibraryView::applyColour (const juce::File& file, juce::Colour c)
{
    juce::String err;
    if (! Library::setColour (file, c.isTransparent() ? 0u : c.getARGB(), err))
    {
        showError ("Colour", err);
        return;
    }
    if (onColourChanged) onColourChanged (file, c);
    refresh();
}

void LibraryView::refresh()
{
    juce::StringArray log;
    sessions = Library::scan (getLibraryFolder(), log);

    // keep the selection valid
    if (selectedSession() == nullptr)
    {
        selectedSessionFile = juce::File();
        selectedReelFile = juce::File();
    }
    if (selectedReel() == nullptr)
        selectedReelFile = juce::File();

    shelves->rebuild (juce::jmax (300, shelfView.getMaximumVisibleWidth()));
    updateSelection();

    if (! log.isEmpty())
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, "Library", log.joinIntoString ("\n"), "OK", this);
}

void LibraryView::select (const juce::File& reelFile)
{
    for (auto& s : sessions)
        for (auto& r : s.reels)
            if (r.file == reelFile)
            {
                selectedSessionFile = s.file;
                selectedReelFile = r.file;
            }
    updateSelection();

    const auto b = shelves->boundsOf (reelFile);
    if (! b.isEmpty())
        shelfView.setViewPosition (0, juce::jmax (0, b.getY() - 40));
}

const SessionSummary* LibraryView::selectedSession() const
{
    for (auto& s : sessions)
        if (s.file == selectedSessionFile)
            return &s;
    return nullptr;
}

const ReelSummary* LibraryView::selectedReel() const
{
    if (auto* s = selectedSession())
        for (auto& r : s->reels)
            if (r.file == selectedReelFile)
                return &r;
    return nullptr;
}

bool LibraryView::isOnDeck (const juce::File& f) const
{
    return getLoadedReel != nullptr && getLoadedReel() == f;
}

void LibraryView::updateSelection()
{
    const auto* s = selectedSession();
    const auto* r = selectedReel();

    loadButton.setEnabled (r != nullptr);
    loadButton.setButtonText (r != nullptr && isOnDeck (r->file) ? "BACK TO DECK" : "LOAD REEL");
    newReelButton.setEnabled (s != nullptr);
    revealButton.setEnabled (s != nullptr);
    colourButton.setEnabled (s != nullptr);
    archiveButton.setEnabled (r != nullptr && ! isOnDeck (r->file));
    archiveButton.setButtonText (r != nullptr && r->archived ? "UNARCHIVE" : "ARCHIVE");
    deleteButton.setEnabled (s != nullptr && (r != nullptr ? ! isOnDeck (r->file) : s->reels.empty()));
    deleteButton.setButtonText (r == nullptr && s != nullptr ? "DELETE SESSION" : "DELETE");
    backButton.setVisible (getLoadedReel != nullptr && getLoadedReel() != juce::File());

    shelves->repaint();
    box->repaint();
    resized();
}

//==============================================================================
static double ipsFromIndex (int i) { return i == 0 ? 7.5 : i == 2 ? 30.0 : 15.0; }

void LibraryView::newSession()
{
    auto* w = new ReelDialog ("New Session", "A session holds the reels for one job - an album, a single, a client.",
                                     juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", "", "Session name:");
    w->addTextEditor ("artist", "", "Artist:");
    w->addComboBox ("tracks", { "8", "16", "24", "32", "48", "64", "72", "96" }, "Tracks on the first reel:");
    w->getComboBoxComponent ("tracks")->setSelectedItemIndex (2);
    w->addComboBox ("ips", { juce::String (juce::CharPointer_UTF8 ("7\xc2\xbd ips")), "15 ips", "30 ips" }, "Tape speed:");
    w->getComboBoxComponent ("ips")->setSelectedItemIndex (1);
    w->addStockPicker ("stock", "Tape stock (fixed for the life of the reel):", tape::forMultitrack, 1);
    w->addMachinePicker ("machine", "Machine (can be changed later):", false, 0);
    w->addButton ("Create", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    // Don't close without a name - remind instead.
    if (auto* create = w->getButton ("Create"))
        create->onClick = [w]
        {
            if (w->getTextEditorContents ("name").trim().isEmpty())
            {
                w->setMessage ("Please give the session a name.");
                if (auto* ed = w->getTextEditor ("name"))
                {
                    ed->setColour (juce::TextEditor::outlineColourId, Colours::red);
                    ed->grabKeyboardFocus();
                    ed->repaint();
                }
                return;
            }
            w->exitModalState (1);
        };

    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w] (int result)
    {
        if (result != 1) return;

        const auto name = w->getTextEditorContents ("name").trim();
        const auto artist = w->getTextEditorContents ("artist").trim();
        const int tracks = w->getComboBoxComponent ("tracks")->getText().getIntValue();

        juce::String err;
        auto s = Session::create (getLibraryFolder(), name, err);
        if (s == nullptr) { showError ("New Session", err); return; }

        if (artist.isNotEmpty())
        {
            s->info.set ("Artist", artist);
            s->save (err);
        }

        const double ips = ipsFromIndex (w->getComboBoxComponent ("ips")->getSelectedItemIndex());
        const int stock = w->stockAt ("stock");
        auto reel = s->createReel (tracks, getSampleRate ? getSampleRate() : 48000.0, err, ips, stock);
        if (reel == nullptr) { showError ("New Session", err); return; }
        reel->machine = w->getComboBoxComponent ("machine")->getSelectedItemIndex();
        reel->save (err);

        const auto file = reel->getFile();
        refresh();
        select (file);
        if (onLoadReel) onLoadReel (file);
    }), true);
}

void LibraryView::newReel()
{
    const auto* s = selectedSession();
    if (s == nullptr) return;

    const int lastTracks = s->reels.empty() ? kDefaultTracks : s->reels.back().numTracks;
    const double lastIps = s->reels.empty() ? 15.0 : s->reels.back().ips;
    const int lastStock = s->reels.empty() ? 1 : s->reels.back().tapeStock;
    const int lastMachine = s->reels.empty() ? 0 : s->reels.back().machine;
    const auto sessionFile = s->file;

    auto* w = new ReelDialog ("New Reel", "Add a reel to \"" + s->name + "\".\nThe track count can't be changed later.",
                                     juce::MessageBoxIconType::NoIcon, this);
    juce::StringArray counts { "8", "16", "24", "32", "48", "64", "72", "96" };
    w->addComboBox ("tracks", counts, "Tracks:");
    w->getComboBoxComponent ("tracks")->setSelectedItemIndex (juce::jmax (0, counts.indexOf (juce::String (lastTracks))));
    w->addComboBox ("ips", { juce::String (juce::CharPointer_UTF8 ("7\xc2\xbd ips")), "15 ips", "30 ips" }, "Tape speed:");
    w->getComboBoxComponent ("ips")->setSelectedItemIndex (lastIps == 7.5 ? 0 : lastIps == 30.0 ? 2 : 1);
    w->addStockPicker ("stock", "Tape stock (fixed for the life of the reel):", tape::forMultitrack, lastStock);
    w->addMachinePicker ("machine", "Machine (can be changed later):", false, lastMachine);
    w->addButton ("Create", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w, sessionFile] (int result)
    {
        if (result != 1) return;
        const int tracks = w->getComboBoxComponent ("tracks")->getText().getIntValue();

        juce::String err;
        auto session = Session::load (sessionFile, err);
        const double ips = ipsFromIndex (w->getComboBoxComponent ("ips")->getSelectedItemIndex());
        const int stock = w->stockAt ("stock");
        auto reel = session != nullptr ? session->createReel (tracks, getSampleRate ? getSampleRate() : 48000.0, err, ips, stock) : nullptr;
        if (reel == nullptr) { showError ("New Reel", err); return; }
        reel->machine = w->getComboBoxComponent ("machine")->getSelectedItemIndex();
        reel->save (err);

        const auto file = reel->getFile();
        refresh();
        select (file);
        if (onLoadReel) onLoadReel (file);
    }), true);
}

void LibraryView::loadSelected()
{
    if (auto* r = selectedReel())
    {
        if (isOnDeck (r->file)) { if (onBackToDeck) onBackToDeck(); }
        else if (onLoadReel)    onLoadReel (r->file);
    }
}

void LibraryView::archiveSelected()
{
    auto* r = selectedReel();
    if (r == nullptr || isOnDeck (r->file)) return;

    juce::String err;
    if (! Library::setArchived (r->file, ! r->archived, err))
        showError ("Archive", err);
    refresh();
}

void LibraryView::deleteSelected()
{
    const auto* s = selectedSession();
    const auto* r = selectedReel();
    if (s == nullptr) return;

    if (r != nullptr && isOnDeck (r->file))
    {
        showError ("Delete", "That reel is on the deck. Load a different reel first.");
        return;
    }

    const auto what = r != nullptr ? r->name + " of \"" + s->name + "\"" : "the session \"" + s->name + "\"";
    const auto reelFile = r != nullptr ? r->file : juce::File();
    const auto sessionFile = s->file;

    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::WarningIcon, "Delete",
        "Move " + what + " to the Recycle Bin?\nIts audio goes with it.", "Delete", "Cancel", this,
        juce::ModalCallbackFunction::create ([this, reelFile, sessionFile] (int result)
        {
            if (result != 1) return;
            juce::String err;
            const bool ok = reelFile != juce::File() ? Library::deleteReel (reelFile, err)
                                                     : Library::deleteSession (sessionFile, err);
            if (! ok) showError ("Delete", err);
            selectedReelFile = juce::File();
            refresh();
        }));
}

void LibraryView::revealSelected()
{
    if (auto* r = selectedReel())          r->file.revealToUser();
    else if (auto* s = selectedSession())  s->file.revealToUser();
}

void LibraryView::changeFolder()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose the library folder", getLibraryFolder());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this] (const juce::FileChooser& fc)
    {
        const auto dir = fc.getResult();
        if (! dir.isDirectory()) return;
        props.getUserSettings()->setValue ("libraryFolder", dir.getFullPathName());
        props.saveIfNeeded();
        refresh();
    });
}

void LibraryView::showError (const juce::String& title, const juce::String& message)
{
    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, title, message, "OK", this);
}

bool LibraryView::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::returnKey && ! search.hasKeyboardFocus (false)) { loadSelected(); return true; }
    if (k == juce::KeyPress::escapeKey && backButton.isVisible())             { if (onBackToDeck) onBackToDeck(); return true; }
    return false;
}

//==============================================================================
void LibraryView::paint (juce::Graphics& g)
{
    g.fillAll (Colours::bg);
    g.setColour (Colours::panel);
    g.fillRect (topBar);

    g.setColour (Colours::mustard);
    g.setFont (monoFont (22.0f, true));
    drawLogoMark (g, topBar.toFloat().withTrimmedLeft (12).withWidth (52).reduced (0, 6));
    g.setColour (Colours::mustard);
    g.drawText ("H A W T H O R N E", topBar.withTrimmedLeft (70).withWidth (280), juce::Justification::centredLeft);
    g.setColour (Colours::text);
    g.setFont (monoFont (15.0f, true));
    g.drawText ("TAPE LIBRARY", topBar.withTrimmedLeft (360).withWidth (200), juce::Justification::centredLeft);

    g.setColour (Colours::textDim);
    g.setFont (monoFont (11.0f));
    g.drawText ("LIBRARY  " + getLibraryFolder().getFullPathName(), folderArea, juce::Justification::centredLeft, true);
}

void LibraryView::resized()
{
    auto r = getLocalBounds();
    topBar = r.removeFromTop (56);
    {
        auto t = topBar.reduced (14, 11);
        t.removeFromLeft (470);
        if (backButton.isVisible())
        {
            backButton.setBounds (t.removeFromRight (140));
            t.removeFromRight (10);
        }
        newSessionButton.setBounds (t.removeFromRight (140));
        t.removeFromRight (16);
        showArchived.setBounds (t.removeFromRight (140));
        t.removeFromRight (10);
        search.setBounds (t.removeFromRight (juce::jmin (380, t.getWidth())));
    }

    auto bottom = r.removeFromBottom (34).reduced (14, 6);
    folderButton.setBounds (bottom.removeFromRight (90));
    folderArea = bottom;

    auto side = r.removeFromRight (520);
    auto buttons = side.removeFromBottom (110).reduced (20, 10);
    {
        auto row1 = buttons.removeFromTop (44);
        loadButton.setBounds (row1.removeFromLeft (row1.getWidth() / 2 - 5));
        row1.removeFromLeft (10);
        newReelButton.setBounds (row1);
        buttons.removeFromTop (8);
        const int w = (buttons.getWidth() - 30) / 4;
        revealButton.setBounds (buttons.removeFromLeft (w));
        buttons.removeFromLeft (10);
        colourButton.setBounds (buttons.removeFromLeft (w));
        buttons.removeFromLeft (10);
        archiveButton.setBounds (buttons.removeFromLeft (w));
        buttons.removeFromLeft (10);
        deleteButton.setBounds (buttons);
    }
    box->setBounds (side);

    shelfView.setBounds (r);
    shelves->minHeight = r.getHeight();
    shelves->rebuild (juce::jmax (300, shelfView.getMaximumVisibleWidth()));
}

} // namespace hawthorne
