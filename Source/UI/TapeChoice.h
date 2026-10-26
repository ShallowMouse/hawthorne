#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Engine/TapeStock.h"
#include "HawthorneLookAndFeel.h"

namespace hawthorne
{

/** Tape stocks offered for a use (multitrack reels or the 2-track master), in menu order: clean last. */
inline std::vector<int> stocksFor (int use)
{
    std::vector<int> v;
    for (int s = 1; s < tape::numStocks; ++s)
        if ((tape::info ((tape::Stock) s).use & use) != 0) v.push_back (s);
    v.push_back (0);
    return v;
}

/** An AlertWindow that owns the description labels under its pickers. */
struct ReelDialog : juce::AlertWindow
{
    using juce::AlertWindow::AlertWindow;
    std::vector<std::unique_ptr<juce::Label>> notes;

    /** A combo box with a description underneath that follows the selection. */
    juce::ComboBox* addPicker (const juce::String& id, const juce::String& label, const juce::StringArray& names,
                               int selected, std::function<juce::String (int)> describe, int height = 96)
    {
        addComboBox (id, names, label);
        auto* c = getComboBoxComponent (id);
        c->setSelectedItemIndex (juce::jlimit (0, names.size() - 1, selected), juce::dontSendNotification);

        notes.push_back (std::make_unique<juce::Label>());
        auto* l = notes.back().get();
        l->setJustificationType (juce::Justification::topLeft);
        l->setFont (juce::FontOptions (14.0f));
        l->setColour (juce::Label::textColourId, Colours::text);
        l->setMinimumHorizontalScale (1.0f);
        l->setSize (440, height);
        auto update = [c, l, describe] { l->setText (describe (c->getSelectedItemIndex()), juce::dontSendNotification); };
        update();
        c->onChange = update;
        addCustomComponent (l);
        return c;
    }

    /** Tape stock picker for a use; returns the chosen stock via stockAt(). */
    void addStockPicker (const juce::String& id, const juce::String& label, int use, int current)
    {
        const auto list = stocksFor (use);
        juce::StringArray names;
        int sel = 0;
        for (size_t i = 0; i < list.size(); ++i)
        {
            names.add (tape::info ((tape::Stock) list[i]).name);
            if (list[i] == current) sel = (int) i;
        }
        addPicker (id, label, names, sel, [list] (int i)
        {
            const int s = list[(size_t) juce::jlimit (0, (int) list.size() - 1, i)];
            juce::String t = tape::info ((tape::Stock) s).description;
            if (s != 0) t << "\nSlower tape = bigger low bump, darker top, more hiss.";
            return t;
        });
        stockLists[id] = list;
    }

    int stockAt (const juce::String& id)
    {
        auto it = stockLists.find (id);
        auto* c = getComboBoxComponent (id);
        if (it == stockLists.end() || c == nullptr) return 0;
        return it->second[(size_t) juce::jlimit (0, (int) it->second.size() - 1, c->getSelectedItemIndex())];
    }

    void addMachinePicker (const juce::String& id, const juce::String& label, bool twoTrack, int current)
    {
        juce::StringArray names;
        const int count = twoTrack ? tape::numMasterMachines : tape::numMultitrackMachines;
        for (int i = 0; i < count; ++i)
            names.add ((twoTrack ? tape::masterMachine (i) : tape::multitrackMachine (i)).name);
        addPicker (id, label, names, current, [twoTrack] (int i)
        {
            return juce::String ((twoTrack ? tape::masterMachine (i) : tape::multitrackMachine (i)).description);
        }, 64);
    }

    std::map<juce::String, std::vector<int>> stockLists;
};

} // namespace hawthorne
