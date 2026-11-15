#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "MachineSkin.h"
#include "HawthorneLookAndFeel.h"
#include "../Engine/CustomMachine.h"

namespace hawthorne
{

inline void drawMachinePreview (juce::Graphics&, juce::Rectangle<float>, const MachineSkin&);

//==============================================================================
/**
    Build a machine: choose its parts as a machine was put together (electronics,
    track width, heads, head contour, transport), then trim it. The reel is on the
    machine while it is being built, so every change is heard as it is made.
*/
class MachineBuilder : public juce::Component
{
public:
    /** Every change, as it is made (the reel goes onto the machine). */
    std::function<void (const tape::MachineParts&, const juce::String&)> onChange;
    /** USE THIS MACHINE (true) or CANCEL (false). */
    std::function<void (bool)> onClose;

    MachineBuilder (bool forTwoTrack, const tape::MachineParts& start, const juce::String& startName)
        : twoTrack (forTwoTrack), parts (tape::tidy (start))
    {
        setOpaque (true);

        nameBox.setText (startName.isNotEmpty() ? startName : juce::String (twoTrack ? "My 2-track" : "My machine"), false);
        nameBox.setFont (uiFont (16.0f, true));
        nameBox.setInputRestrictions (28);
        nameBox.setColour (juce::TextEditor::backgroundColourId, Colours::black);
        nameBox.setColour (juce::TextEditor::textColourId, Colours::text);
        nameBox.setColour (juce::TextEditor::outlineColourId, Colours::edge);
        nameBox.onTextChange = [this] { changed(); };
        addAndMakeVisible (nameBox);

        // the look: any of the real machines' faceplates
        for (int i : looks())
            lookBox.addItem ((twoTrack ? tape::masterMachine (i) : tape::multitrackMachine (i)).name, i + 1);
        lookBox.setSelectedId (parts.look + 1, juce::dontSendNotification);
        if (lookBox.getSelectedId() == 0) { parts.look = looks().front(); lookBox.setSelectedId (parts.look + 1, juce::dontSendNotification); }
        lookBox.onChange = [this] { parts.look = lookBox.getSelectedId() - 1; changed(); };
        addAndMakeVisible (lookBox);

        for (int k = 0; k < tape::numPartKinds; ++k)
            for (int i = 0; i < tape::numPartChoices (k); ++i)
            {
                auto* b = partButtons.add (new juce::TextButton (tape::partChoice (k, i).name));
                b->setClickingTogglesState (true);
                b->setRadioGroupId (100 + k);
                b->setColour (juce::TextButton::buttonColourId, Colours::panelLight);
                b->setColour (juce::TextButton::buttonOnColourId, Colours::mustardDim);
                b->setColour (juce::TextButton::textColourOffId, Colours::textDim.brighter (0.5f));
                b->setColour (juce::TextButton::textColourOnId, juce::Colours::white);
                b->setTooltip (tape::partChoice (k, i).note);
                b->setToggleState (tape::partOf (parts, k) == i, juce::dontSendNotification);
                b->onClick = [this, k, i, b] { if (b->getToggleState()) { tape::partOf (parts, k) = i; changed(); } };
                b->getProperties().set ("kind", k);
                b->getProperties().set ("choice", i);
                addAndMakeVisible (b);
            }

        for (int i = 0; i < tape::numTrims; ++i)
        {
            const auto& t = tape::trimInfo (i);
            auto* s = trims.add (new juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow));
            s->setRange (t.lo, t.hi, 0.01);
            if (std::abs (0.5 * (t.lo + t.hi) - t.rest) > 0.01) s->setSkewFactorFromMidPoint (t.rest);
            s->setDoubleClickReturnValue (true, t.rest);
            s->setValue (tape::trimOf (parts, i), juce::dontSendNotification);
            s->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 74, 18);
            s->setColour (juce::Slider::textBoxTextColourId, Colours::text);
            s->setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            s->setColour (juce::Slider::textBoxBackgroundColourId, Colours::black);
            const juce::String unit (t.unit);
            s->textFromValueFunction = [unit] (double v)
            {
                return unit == "dB" ? (v > 0.004 ? "+" : "") + juce::String (v, 1) + " dB" : "x " + juce::String (v, 2);
            };
            s->valueFromTextFunction = [] (const juce::String& text) { return text.retainCharacters ("0123456789.-").getDoubleValue(); };
            s->updateText();
            s->setTooltip (juce::String (t.tip) + "  Double-click to put it back.");
            s->onValueChange = [this, i, s] { tape::trimOf (parts, i) = s->getValue(); changed(); };
            addAndMakeVisible (s);
        }

        shelfBox.setTextWhenNothingSelected ("Take one from the shelf...");
        shelfBox.setTextWhenNoChoicesAvailable ("The shelf is empty");
        shelfBox.onChange = [this]
        {
            const int k = shelfBox.getSelectedItemIndex();
            if (! juce::isPositiveAndBelow (k, (int) shelf.size())) return;
            parts = shelf[(size_t) k].parts;
            nameBox.setText (shelf[(size_t) k].name, false);
            showParts();
            changed();
        };
        addAndMakeVisible (shelfBox);

        saveButton.onClick = [this]
        {
            tape::NamedMachine m { name(), parts, twoTrack };
            if (tape::MachineShelf::save (m)) { loadShelf(); note = "Saved to the shelf as \"" + name() + "\"."; }
            else note = "Couldn't save it: " + tape::MachineShelf::folder().getFullPathName();
            repaint();
        };
        deleteButton.onClick = [this]
        {
            note = tape::MachineShelf::remove (name(), twoTrack) ? "Taken off the shelf: \"" + name() + "\" (the reel keeps its own copy)."
                                                                 : "There's no machine called \"" + name() + "\" on the shelf.";
            loadShelf();
            repaint();
        };
        cancelButton.onClick = [this] { if (onClose) onClose (false); };
        useButton.onClick = [this] { changed(); if (onClose) onClose (true); };
        useButton.setColour (juce::TextButton::buttonColourId, Colours::mustard);
        useButton.setColour (juce::TextButton::textColourOffId, Colours::black);
        saveButton.setTooltip ("Keeps this machine on the shelf under its name, for any reel to use.");
        deleteButton.setTooltip ("Takes the machine with this name off the shelf. Reels already on it keep their own copy.");
        for (auto* b : { &saveButton, &deleteButton, &cancelButton, &useButton }) addAndMakeVisible (b);

        loadShelf();
    }

    juce::String name() const { const auto n = nameBox.getText().trim(); return n.isNotEmpty() ? n : juce::String (twoTrack ? "My 2-track" : "My machine"); }
    const tape::MachineParts& getParts() const { return parts; }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Colours::bg);
        g.setColour (Colours::mustard);
        g.setFont (monoFont (17.0f, true));
        g.drawText (twoTrack ? "BUILD A 2-TRACK MACHINE" : "BUILD A MULTITRACK MACHINE", titleArea, juce::Justification::centredLeft);

        // the machine as it will look, with its name on the badge
        {
            auto sk = twoTrack ? MachineSkin::master (parts.look) : MachineSkin::multitrack (parts.look);
            const auto label = name().toUpperCase().toStdString();
            sk.brand = "";
            sk.model = label.c_str();
            drawMachinePreview (g, previewArea.toFloat(), sk);
        }

        auto caption = [&g] (const juce::String& t, juce::Rectangle<int> r)
        {
            g.setColour (Colours::textDim);
            g.setFont (monoFont (11.0f, true));
            g.drawText (t, r, juce::Justification::centredLeft);
        };
        caption ("NAME", nameLabel);
        caption ("LOOKS LIKE", lookLabel);
        caption ("SHELF", shelfLabel);

        for (int k = 0; k < tape::numPartKinds; ++k)
        {
            g.setColour (Colours::text);
            g.setFont (monoFont (13.0f, true));
            g.drawText (tape::partKindName (k), partLabel[k], juce::Justification::centredLeft);
            g.setColour (Colours::text.withAlpha (0.85f));
            g.setFont (uiFont (13.0f));
            g.drawFittedText (tape::partChoice (k, tape::partOf (parts, k)).note, partNote[k], juce::Justification::centredLeft, 1, 0.8f);
        }

        g.setColour (Colours::mustard);
        g.setFont (monoFont (13.0f, true));
        g.drawText ("TRIM", trimTitle, juce::Justification::centredLeft);
        for (int i = 0; i < tape::numTrims; ++i)
        {
            g.setColour (Colours::text);
            g.setFont (monoFont (11.0f, true));
            g.drawText (tape::trimInfo (i).name, trimLabel[i], juce::Justification::centred);
        }

        // what it all adds up to
        g.setColour (Colours::panel);
        g.fillRoundedRectangle (sumArea.toFloat(), 4.0f);
        g.setColour (Colours::edge);
        g.drawRoundedRectangle (sumArea.toFloat().reduced (0.5f), 4.0f, 1.0f);
        g.setColour (Colours::mustard.brighter (0.2f));
        g.setFont (uiFont (13.0f, true));
        g.drawFittedText (summary (parts), sumArea.reduced (10, 4), juce::Justification::centredLeft, 2, 0.85f);

        g.setColour (Colours::textDim);
        g.setFont (uiFont (12.5f));
        g.drawFittedText (note.isNotEmpty() ? note
                              : juce::String ("The reel is on this machine while you build it: play the tape and listen. Headroom and colour are printed "
                                              "as you record; everything else is the playback side, so it changes tape already recorded."),
                          noteArea, juce::Justification::centredLeft, 2, 0.9f);
    }

    /** What the parts and trims come to, in figures (against a plain machine). */
    static juce::String summary (const tape::MachineParts& p)
    {
        const auto m = tape::buildMachine (p);
        auto db = [] (double v) { return (v > 0.04 ? "+" : "") + juce::String (std::abs (v) < 0.05 ? 0.0 : v, 1) + " dB"; };
        const double top = juce::jmin (25.0, 22.4 * m.hfScale / (1.0 + 0.06 * m.hfSoftDb));     // (within 2 dB, kHz at 15 ips)
        juce::String s;
        s << "ADDS UP TO:  headroom " << db (m.headroomDb)
          << "   -   second harmonic " << (m.asymmetry < 0.004 ? "none" : m.asymmetry < 0.02 ? "a little" : m.asymmetry < 0.04 ? "some" : "plenty")
          << "   -   head bump x " << juce::String (m.bumpScale, 2) << " at " << juce::roundToInt (60.0 * m.bumpFreq) << " Hz"
          << "   -   top to " << juce::roundToInt (top) << " kHz at 15 ips"
          << "   -   hiss " << db (m.hissDb)
          << "   -   wow & flutter " << juce::String (m.wowFlutter, m.wowFlutter < 0.1 ? 3 : 2) << " %";
        return s;
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (16, 10);
        auto top = r.removeFromTop (34);
        useButton.setBounds (top.removeFromRight (190).reduced (0, 2));
        top.removeFromRight (8);
        cancelButton.setBounds (top.removeFromRight (100).reduced (0, 2));
        titleArea = top;
        r.removeFromTop (8);

        // preview on the left; name, look and shelf on the right
        auto head = r.removeFromTop (116);
        previewArea = head.removeFromLeft (juce::jmin (400, head.getWidth() / 2 - 10));
        head.removeFromLeft (20);
        auto line = [&head] (juce::Rectangle<int>& label) { auto row = head.removeFromTop (32); head.removeFromTop (10); label = row.removeFromLeft (96); return row; };
        nameBox.setBounds (line (nameLabel).reduced (0, 2));
        lookBox.setBounds (line (lookLabel).reduced (0, 2));
        auto sh = line (shelfLabel).reduced (0, 2);
        deleteButton.setBounds (sh.removeFromRight (84));
        sh.removeFromRight (6);
        saveButton.setBounds (sh.removeFromRight (84));
        sh.removeFromRight (8);
        shelfBox.setBounds (sh);
        r.removeFromTop (12);

        // the bottom, upwards: the note, the sum, the trims
        noteArea = r.removeFromBottom (34);
        r.removeFromBottom (6);
        sumArea = r.removeFromBottom (44);
        r.removeFromBottom (10);
        auto trimBand = r.removeFromBottom (juce::jlimit (116, 150, r.getHeight() / 4));
        trimTitle = trimBand.removeFromLeft (70);
        const int tw = trimBand.getWidth() / tape::numTrims;
        for (int i = 0; i < tape::numTrims; ++i)
        {
            auto t = trimBand.removeFromLeft (tw).reduced (4, 0);
            trimLabel[i] = t.removeFromTop (16);
            trims[i]->setBounds (t);
        }
        r.removeFromBottom (10);

        // the parts fill what is left
        const int rowH = juce::jlimit (48, 70, r.getHeight() / tape::numPartKinds);
        int b = 0;
        for (int k = 0; k < tape::numPartKinds; ++k)
        {
            auto row = r.removeFromTop (rowH);
            auto keys = row.removeFromTop (juce::jmin (30, rowH - 20));
            partLabel[k] = keys.removeFromLeft (150);
            const int bw = juce::jmin (190, keys.getWidth() / 4);
            for (int i = 0; i < tape::numPartChoices (k); ++i)
                partButtons[b++]->setBounds (keys.removeFromLeft (bw).reduced (3, 0));
            partNote[k] = row.withTrimmedLeft (153).withTrimmedBottom (4);
        }
    }

private:
    /** The real machines whose look can be worn (oldest first; not "no machine"). */
    std::vector<int> looks() const
    {
        std::vector<int> v;
        for (int i : (twoTrack ? tape::masterOrder() : tape::multitrackOrder()))
            if (i != (twoTrack ? tape::noMasterMachine : tape::noMultitrackMachine)) v.push_back (i);
        return v;
    }

    void showParts()
    {
        for (auto* b : partButtons)
            b->setToggleState (tape::partOf (parts, (int) b->getProperties()["kind"]) == (int) b->getProperties()["choice"], juce::dontSendNotification);
        for (int i = 0; i < tape::numTrims; ++i) trims[i]->setValue (tape::trimOf (parts, i), juce::dontSendNotification);
        lookBox.setSelectedId (parts.look + 1, juce::dontSendNotification);
        if (lookBox.getSelectedId() == 0) { parts.look = looks().front(); lookBox.setSelectedId (parts.look + 1, juce::dontSendNotification); }
    }

    void loadShelf()
    {
        shelf = tape::MachineShelf::list (twoTrack);
        shelfBox.clear (juce::dontSendNotification);
        int id = 1;
        for (auto& m : shelf) shelfBox.addItem (m.name, id++);
    }

    void changed()
    {
        note.clear();
        if (onChange) onChange (parts, name());
        repaint();
    }

    const bool twoTrack;
    tape::MachineParts parts;
    std::vector<tape::NamedMachine> shelf;
    juce::String note;

    juce::TextEditor nameBox;
    juce::ComboBox lookBox, shelfBox;
    juce::TextButton saveButton { "SAVE" }, deleteButton { "DELETE" }, cancelButton { "CANCEL" }, useButton { "USE THIS MACHINE" };
    juce::OwnedArray<juce::TextButton> partButtons;
    juce::OwnedArray<juce::Slider> trims;

    juce::Rectangle<int> titleArea, previewArea, nameLabel, lookLabel, shelfLabel, trimTitle, sumArea, noteArea;
    juce::Rectangle<int> partLabel[tape::numPartKinds], partNote[tape::numPartKinds], trimLabel[tape::numTrims];
};

} // namespace hawthorne
