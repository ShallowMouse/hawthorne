#include "MarkerPanel.h"
#include "UIHelpers.h"

namespace hawthorne
{

//==============================================================================
class MarkerPanel::Row : public juce::Component
{
public:
    explicit Row (MarkerPanel& p) : owner (p)
    {
        name.setFont (uiFont (14.0f));
        name.setColour (juce::Label::textColourId, Colours::text);
        name.setInterceptsMouseClicks (false, true);
        name.onTextChange = [this] { owner.rename (index, name.getText()); };
        addAndMakeVisible (name);
    }

    void update (int i, const Marker& m, double sr, bool current)
    {
        index = i;
        time = formatTime (m.position, sr);
        isCurrent = current;
        if (! name.isBeingEdited())
            name.setText (m.name, juce::dontSendNotification);
        name.setColour (juce::Label::textColourId, current ? Colours::mustard : Colours::text);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds();
        g.setColour (isCurrent ? Colours::mustardDim.withAlpha (0.35f) : (index % 2 ? Colours::panelLight.withAlpha (0.4f) : Colours::panel));
        g.fillRect (r);
        g.setColour (isCurrent ? Colours::mustard : Colours::textDim);
        g.setFont (monoFont (13.0f, true));
        g.drawText (time, r.removeFromLeft (timeWidth).reduced (8, 0), juce::Justification::centredLeft);
    }

    void resized() override { name.setBounds (getLocalBounds().withTrimmedLeft (timeWidth)); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
            owner.showMenu (index);
        else if (owner.session != nullptr && juce::isPositiveAndBelow (index, (int) owner.session->markers.size()) && owner.onLocate)
            owner.onLocate (owner.session->markers[(size_t) index].position);
    }

    void mouseDoubleClick (const juce::MouseEvent&) override { startRename(); }
    void startRename() { name.showEditor(); }

private:
    MarkerPanel& owner;
    juce::Label name;
    juce::String time;
    int index = -1;
    bool isCurrent = false;
    static constexpr int timeWidth = 118;
};

//==============================================================================
MarkerPanel::MarkerPanel()
{
    list.setRowHeight (28);
    list.setColour (juce::ListBox::backgroundColourId, Colours::panel);
    list.setOutlineThickness (0);
    list.setWantsKeyboardFocus (false);
    addAndMakeVisible (list);

    addButton.setTooltip ("Drop a marker at the current position (M) - works while recording too");
    addButton.onClick = [this] { if (onAddMarker) onAddMarker(); };
    addButton.setWantsKeyboardFocus (false);
    addAndMakeVisible (addButton);
}

void MarkerPanel::setSession (Session* s)
{
    session = s;
    currentIndex = -1;
    refresh();
}

void MarkerPanel::refresh()
{
    list.updateContent();
    list.repaint();
    for (int i = 0; i < getNumRows(); ++i)
        if (auto* row = dynamic_cast<Row*> (list.getComponentForRowNumber (i)))
            row->update (i, session->markers[(size_t) i], session->sampleRate, i == currentIndex);
    repaint();
}

void MarkerPanel::setPlayhead (juce::int64 pos)
{
    int idx = -1;
    if (session != nullptr)
        for (int i = 0; i < (int) session->markers.size(); ++i)
            if (session->markers[(size_t) i].position <= pos)
                idx = i;

    if (idx != currentIndex)
    {
        currentIndex = idx;
        refresh();
    }
}

int MarkerPanel::getNumRows()
{
    return session != nullptr ? (int) session->markers.size() : 0;
}

juce::Component* MarkerPanel::refreshComponentForRow (int row, bool, juce::Component* existing)
{
    if (session == nullptr || ! juce::isPositiveAndBelow (row, (int) session->markers.size()))
    {
        delete existing;
        return nullptr;
    }

    auto* r = dynamic_cast<Row*> (existing);
    if (r == nullptr)
    {
        delete existing;
        r = new Row (*this);
    }

    r->update (row, session->markers[(size_t) row], session->sampleRate, row == currentIndex);
    return r;
}

void MarkerPanel::rename (int index, const juce::String& name)
{
    if (session == nullptr || ! juce::isPositiveAndBelow (index, (int) session->markers.size()))
        return;

    session->markers[(size_t) index].name = name.trim().isEmpty() ? "Marker" : name.trim();
    if (onChanged) onChanged();
    refresh();
}

void MarkerPanel::remove (int index)
{
    if (session == nullptr || ! juce::isPositiveAndBelow (index, (int) session->markers.size()))
        return;

    session->markers.erase (session->markers.begin() + index);
    currentIndex = -1;
    if (onChanged) onChanged();
    refresh();
}

void MarkerPanel::moveToPlayhead (int index)
{
    if (session == nullptr || getPlayhead == nullptr || ! juce::isPositiveAndBelow (index, (int) session->markers.size()))
        return;

    session->markers[(size_t) index].position = getPlayhead();
    session->sortMarkers();   // always in time order
    currentIndex = -1;
    if (onChanged) onChanged();
    refresh();
}

void MarkerPanel::showMenu (int index)
{
    juce::PopupMenu m;
    m.addItem (1, "Rename");
    m.addItem (2, "Move to current position");
    m.addSeparator();
    m.addItem (3, "Delete");

    m.showMenuAsync (juce::PopupMenu::Options(), [this, index] (int r)
    {
        if (r == 1)
            if (auto* row = dynamic_cast<Row*> (list.getComponentForRowNumber (index)))
                row->startRename();
        if (r == 2) moveToPlayhead (index);
        if (r == 3) remove (index);
    });
}

//==============================================================================
void MarkerPanel::paint (juce::Graphics& g)
{
    g.fillAll (Colours::panel);
    g.setColour (Colours::edge);
    g.drawVerticalLine (0, 0.0f, (float) getHeight());

    g.setColour (Colours::mustard);
    g.setFont (monoFont (15.0f, true));
    g.drawText ("MARKERS", 12, 8, 120, 26, juce::Justification::centredLeft);

    if (getNumRows() == 0)
    {
        g.setColour (Colours::textDim);
        g.setFont (uiFont (13.0f));
        g.drawFittedText ("No markers yet.\nPress M or ADD to drop one at the playhead - even while recording.",
                          getLocalBounds().withTrimmedTop (60).reduced (16, 0).removeFromTop (60),
                          juce::Justification::topLeft, 3);
    }
}

void MarkerPanel::resized()
{
    auto r = getLocalBounds().withTrimmedLeft (1);
    auto top = r.removeFromTop (42).reduced (10, 8);
    addButton.setBounds (top.removeFromRight (90));
    list.setBounds (r);
}

} // namespace hawthorne
