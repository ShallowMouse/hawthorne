#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "HawthorneLookAndFeel.h"

namespace hawthorne
{

//==============================================================================
/**
    Hawthorne's flexible layout. The UI is split into panels (meters, transport, master,
    autolocator, markers). Each one is docked in the main window or in any number of
    extra windows - at the top, bottom, left, right or centre - or hidden. Panels on the
    same edge stack (side by side on the top / bottom, one above the other on the left /
    right). Layouts are global, with named presets.

    LAYOUT -> Edit layout: drag a panel by its overlay into any window (near an edge to
    dock it there, the middle for the centre, outside every window for a new one).
*/
enum class Edge { top, bottom, left, right, centre };

class LayoutManager;

//==============================================================================
/** One panel. Its owner fills in how it paints and lays out (in its own coordinates). */
class Panel : public juce::Component
{
public:
    Panel (const juce::String& id, const juce::String& title);

    const juce::String id, title;

    // Natural size: on the top / bottom a panel gets prefH tall (the row is as tall as its
    // tallest), on the left / right prefW wide. fixedW / fixedH: keep that size in a row /
    // column instead of sharing the space.
    int prefW = 300, prefH = 200;
    bool fixedW = false, fixedH = false;
    bool sideOK = true;                  // can it go on the left / right (narrow)?

    std::function<void (juce::Graphics&)> onPaint;
    std::function<void()> onLayout;
    std::function<void (const juce::MouseEvent&)> onMouseMove, onMouseUp;

    void paint (juce::Graphics& g) override  { if (onPaint) onPaint (g); }
    void resized() override                  { if (onLayout) onLayout(); overlay.setBounds (getLocalBounds()); }
    void mouseMove (const juce::MouseEvent& e) override { if (onMouseMove) onMouseMove (e); }
    void mouseUp (const juce::MouseEvent& e) override   { if (onMouseUp) onMouseUp (e); }

    void setEditing (bool on) { overlay.setVisible (on); if (on) overlay.toFront (false); }

private:
    friend class LayoutManager;
    struct Overlay : juce::Component
    {
        Panel& panel;
        explicit Overlay (Panel& p) : panel (p) {}
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        bool dragging = false;
    };
    Overlay overlay { *this };
    LayoutManager* manager = nullptr;
};

//==============================================================================
/** Where the panels go in one window. */
class PanelHost : public juce::Component
{
public:
    PanelHost (LayoutManager& m, int index) : manager (m), windowIndex (index) {}
    void resized() override;
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;

    LayoutManager& manager;
    int windowIndex;                      // 0 = the main window
    juce::Rectangle<int> dropZone;        // while dragging a panel over this window
};

//==============================================================================
class LayoutManager
{
public:
    explicit LayoutManager (juce::ApplicationProperties&);
    ~LayoutManager();

    void addPanel (Panel&, int defaultWindow, Edge defaultEdge);
    PanelHost& mainHost() { return main; }

    /** Load the saved layout (or the default) and put everything in place. */
    void restore();
    void resetToDefault();

    // Presets
    juce::StringArray presetNames() const;
    void savePreset (const juce::String& name);
    void loadPreset (const juce::String& name);
    void deletePreset (const juce::String& name);

    // Editing
    bool isEditing() const { return editing; }
    void setEditing (bool);
    bool isShown (const Panel&) const;
    void setShown (Panel&, bool);
    void move (Panel&, int window, Edge);
    int  newWindow (juce::Point<int> screenPos = {});
    int  numWindows() const { return (int) windows.size() + 1; }
    void showPanelMenu (Panel&);
    void setFloatingVisible (bool);       // (hidden while the library is up)

    std::function<bool (const juce::KeyPress&)> onKey;     // keys pressed in other windows
    std::function<void()> onChanged;

    //--------------------------------------------------------------------------
    // (used by PanelHost / Panel)
    void layoutHost (PanelHost&);
    void dragPanel (Panel&, juce::Point<int> screenPos, bool finished);
    void windowClosed (int index);
    void windowMoved();

private:
    struct Place { juce::String panel; int window = 0; Edge edge = Edge::centre; };
    struct Floating;

    Panel* find (const juce::String& id) const;
    Place* placeOf (const juce::String& id);
    PanelHost* host (int window);
    void apply();
    void save();
    std::unique_ptr<juce::XmlElement> toXml() const;
    void fromXml (const juce::XmlElement&);
    void defaults();
    void dropTarget (juce::Point<int> screenPos, int& window, Edge& edge, juce::Rectangle<int>& zone);

    juce::ApplicationProperties& props;
    PanelHost main { *this, 0 };
    juce::Array<Panel*> panels;
    std::vector<Place> defaultPlaces;
    std::vector<Place> places;                   // only shown panels, in stacking order
    std::vector<std::unique_ptr<Floating>> windows;
    std::vector<juce::Rectangle<int>> windowBounds;
    bool editing = false, floatingVisible = true, applying = false;
};

} // namespace hawthorne
