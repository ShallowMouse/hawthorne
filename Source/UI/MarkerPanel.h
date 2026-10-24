#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Engine/Session.h"
#include "HawthorneLookAndFeel.h"

namespace hawthorne
{

//==============================================================================
/**
    Markers sidebar. Always in time order. Click a marker to go there,
    double-click its name to rename, right-click for more.
*/
class MarkerPanel : public juce::Component,
                    private juce::ListBoxModel
{
public:
    MarkerPanel();

    void setSession (Session*);
    void refresh();                                   // markers changed
    void setPlayhead (juce::int64 samplePos);         // highlight the current marker

    std::function<void (juce::int64)> onLocate;
    std::function<void()> onAddMarker;
    std::function<void()> onChanged;                  // save
    std::function<juce::int64()> getPlayhead;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Row;

    int getNumRows() override;
    void paintListBoxItem (int, juce::Graphics&, int, int, bool) override {}
    juce::Component* refreshComponentForRow (int row, bool selected, juce::Component* existing) override;

    void rename (int index, const juce::String& name);
    void remove (int index);
    void moveToPlayhead (int index);
    void showMenu (int index);

    Session* session = nullptr;
    juce::ListBox list { "Markers", this };
    juce::TextButton addButton { "ADD  (M)" };
    int currentIndex = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MarkerPanel)
};

} // namespace hawthorne
