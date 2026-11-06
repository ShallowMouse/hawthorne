#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "HawthorneLookAndFeel.h"

namespace hawthorne
{

//==============================================================================
/**
    Hawthorne's layout: a grid in every window. The UI is split into panels (meters,
    transport, master, autolocator, markers); each one covers a block of grid cells in the
    main window or in any number of extra windows, or is hidden. Layouts are global, with
    named presets.

    LAYOUT -> Edit layout: the grid shows; drag a panel to move it (into any window, or out
    of every window for a new one), drag its corner to change how many cells it covers. A
    panel only goes where it fits: never over another, never smaller than it can work at or
    bigger than makes sense. Windows can't be made too small for what's in them, and if a
    screen is still too small, a panel scales down rather than breaking.
*/
class LayoutManager;

//==============================================================================
/** One panel. Its owner fills in how it paints and lays out (in its own coordinates). */
class Panel : public juce::Component
{
public:
    Panel (const juce::String& id, const juce::String& title);

    const juce::String id, title;

    // Sizes it works at, in pixels (0 = no maximum)
    int minW = 200, minH = 100, maxW = 0, maxH = 0;

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
        void mouseMove (const juce::MouseEvent&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        bool overCorner (juce::Point<int>) const;
        bool dragging = false, resizing = false;
        juce::Point<int> grabCell;           // which of the panel's cells it was picked up by
    };
    Overlay overlay { *this };
    LayoutManager* manager = nullptr;
};

//==============================================================================
/** One window's grid. */
class PanelHost : public juce::Component
{
public:
    static constexpr int cols = 12, rows = 8;

    PanelHost (LayoutManager& m, int index) : manager (m), windowIndex (index) {}
    void resized() override;
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;

    juce::Rectangle<int> cellArea (juce::Rectangle<int> cells) const;    // cells -> pixels
    juce::Point<int> cellAt (juce::Point<int> local) const;              // pixels -> cell (clamped)
    float cellW() const { return (float) getWidth() / (float) cols; }
    float cellH() const { return (float) getHeight() / (float) rows; }

    LayoutManager& manager;
    int windowIndex;                      // 0 = the main window
    juce::Rectangle<int> target;          // while dragging: the cells it would go to
    bool targetOK = false;
};

//==============================================================================
class LayoutManager
{
public:
    explicit LayoutManager (juce::ApplicationProperties&);
    ~LayoutManager();

    /** cells: where it goes in the main window by default. */
    void addPanel (Panel&, juce::Rectangle<int> defaultCells);
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
    int  newWindow (juce::Point<int> screenPos = {});
    int  numWindows() const { return (int) windows.size() + 1; }
    void showPanelMenu (Panel&);
    void showWindowMenu (int window);
    void setFloatingVisible (bool);       // (hidden while the library is up)
    void toggleWindowFullScreen (int window);

    /** The smallest the main window's grid can be for what's in it (so nothing gets cramped). */
    juce::Point<int> minimumHostSize (int window) const;

    std::function<bool (const juce::KeyPress&)> onKey;     // keys pressed in other windows
    std::function<void()> onChanged;

    //--------------------------------------------------------------------------
    // (used by PanelHost / Panel)
    void layoutHost (PanelHost&);
    void dragPanel (Panel&, juce::Point<int> screenPos, juce::Point<int> grabCell, bool finished);
    void resizePanel (Panel&, juce::Point<int> screenPos, bool finished);
    void windowClosed (int index);
    void windowMoved();

private:
    struct Place { juce::String panel; int window = 0; juce::Rectangle<int> cells; };
    struct Floating;

    Panel* find (const juce::String& id) const;
    Place* placeOf (const juce::String& id);
    PanelHost* host (int window);
    const PanelHost* host (int window) const;
    bool fits (const Panel&, int window, juce::Rectangle<int> cells, const Place* ignore) const;
    bool findSpot (const Panel&, int window, juce::Rectangle<int>& cells) const;
    void clearTargets();
    void apply();
    void save();
    std::unique_ptr<juce::XmlElement> toXml() const;
    void fromXml (const juce::XmlElement&);
    void defaults();
    void updateLimits();

    juce::ApplicationProperties& props;
    PanelHost main { *this, 0 };
    juce::Array<Panel*> panels;
    std::vector<Place> defaultPlaces;
    std::vector<Place> places;                   // only shown panels
    std::vector<std::unique_ptr<Floating>> windows;
    struct WindowState { juce::Rectangle<int> bounds; bool full = false; };
    std::vector<WindowState> windowStates;
    bool editing = false, floatingVisible = true, applying = false;
};

} // namespace hawthorne
