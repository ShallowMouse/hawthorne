#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "HawthorneLookAndFeel.h"

namespace hawthorne
{

//==============================================================================
/**
    Displays: extra windows (second screens...) built from blocks - meters, time,
    transport, varispeed, tempo, master, autolocator, markers. The main window stays the
    classic Hawthorne deck.

    Every display is a grid of 12 x 8 cells. A block covers a block of cells; it has its own
    settings (meters per row, which tracks, VU / PPM, the time's format, which transport
    keys...), and any kind can be used as often as you like. LAYOUT -> Edit displays: right-click
    an empty cell to add a block there, drag a block to move it (to another display too),
    drag its corner to resize, right-click it for its settings. A block only goes where it
    fits: never over another, never smaller than it works at or far bigger than it needs.
    A display can't be made smaller than its blocks work at; on a small screen they scale
    down rather than break. Layouts are global, with named presets.
*/
class LayoutManager;

//==============================================================================
/** A block. Its type fills in how it paints, lays out and what settings it has. */
class Panel : public juce::Component
{
public:
    Panel (const juce::String& type, const juce::String& title);

    juce::String id;                     // unique in the layout
    const juce::String type, title;
    juce::NamedValueSet settings;

    // Sizes it works at, in pixels (0 = no maximum)
    int minW = 200, minH = 100, maxW = 0, maxH = 0;

    /** Settings for the block's menu (ids 1..999), and what to do when one's picked. */
    virtual void addSettings (juce::PopupMenu&) {}
    virtual void settingChosen (int) {}
    /** Settings loaded / changed: rebuild. */
    virtual void settingsChanged() {}
    /** About 30 times a second: follow the deck. */
    virtual void refresh() {}
    /** A different reel (or its tracks / machines changed). */
    virtual void reelChanged() {}

    std::function<void (juce::Graphics&)> onPaint;
    std::function<void()> onLayout;
    std::function<void (const juce::MouseEvent&)> onMouseMove, onMouseUp;

    void mouseMove (const juce::MouseEvent& e) override { if (onMouseMove) onMouseMove (e); }
    void mouseUp (const juce::MouseEvent& e) override   { if (onMouseUp) onMouseUp (e); }
    void paint (juce::Graphics& g) override  { if (onPaint) onPaint (g); }
    void resized() override                  { if (onLayout) onLayout(); overlay.setBounds (getLocalBounds()); }

    void setEditing (bool on) { overlay.setVisible (on); if (on) overlay.toFront (false); }

    int  setting (const juce::Identifier& n, int def) const { return settings.getWithDefault (n, def); }
    void set (const juce::Identifier& n, const juce::var& v);    // and save, re-layout

    LayoutManager* manager = nullptr;

private:
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
        juce::Point<int> grabCell;
    };
    Overlay overlay { *this };
};

//==============================================================================
/** One display's grid. */
class PanelHost : public juce::Component
{
public:
    static constexpr int cols = 12, rows = 8;

    PanelHost (LayoutManager& m, int index) : manager (m), windowIndex (index) {}
    void resized() override;
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;

    juce::Rectangle<int> cellArea (juce::Rectangle<int> cells) const;
    juce::Point<int> cellAt (juce::Point<int> local) const;
    float cellW() const { return (float) getWidth() / (float) cols; }
    float cellH() const { return (float) getHeight() / (float) rows; }

    LayoutManager& manager;
    int windowIndex;                      // 1, 2 ... (display number)
    bool fullScreen = false;
    // full screen: an EXIT FULL SCREEN button shows when the mouse is at the top
    juce::TextButton exitFull { "EXIT FULL SCREEN" };
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    juce::Rectangle<int> target;
    bool targetOK = false;
};

//==============================================================================
class LayoutManager
{
public:
    struct BlockType { juce::String type, name; juce::Point<int> defaultCells; };

    explicit LayoutManager (juce::ApplicationProperties&);
    ~LayoutManager();

    /** The kinds of block, and how to make one. */
    std::vector<BlockType> blockTypes;
    std::function<std::unique_ptr<Panel> (const juce::String& type)> createBlock;

    void restore();
    void removeAllDisplays();

    // Presets
    juce::StringArray presetNames() const;
    void savePreset (const juce::String& name);
    void loadPreset (const juce::String& name);
    void deletePreset (const juce::String& name);

    // Displays
    int  numDisplays() const { return (int) windows.size(); }
    int  newDisplay (juce::Point<int> screenPos = {});
    void closeDisplay (int index);
    bool isFull (int index) const;
    void toggleFullScreen (int index);
    void setVisible (bool);               // (hidden while the library is up)

    // Editing
    bool isEditing() const { return editing; }
    void setEditing (bool);
    void addBlock (const juce::String& type, int display, juce::Point<int> cell);
    void removeBlock (Panel&);
    void showBlockMenu (Panel&);
    void showDisplayMenu (int display, juce::Point<int> cell);

    /** Every block (for the deck to keep them up to date). */
    const juce::OwnedArray<Panel>& getBlocks() const { return blocks; }

    std::function<bool (const juce::KeyPress&)> onKey;     // keys pressed in the displays
    std::function<void()> onChanged;

    //--------------------------------------------------------------------------
    // (used by PanelHost / Panel)
    void layoutHost (PanelHost&);
    void dragPanel (Panel&, juce::Point<int> screenPos, juce::Point<int> grabCell, bool finished);
    void resizePanel (Panel&, juce::Point<int> screenPos, bool finished);
    void windowMoved();
    void settingsChanged (Panel&);

private:
    struct Place { juce::String block; int window = 1; juce::Rectangle<int> cells; };
    struct Floating;

    Panel* find (const juce::String& id) const;
    Place* placeOf (const juce::String& id);
    PanelHost* host (int window);
    const PanelHost* host (int window) const;
    bool fits (const Panel&, int window, juce::Rectangle<int> cells, const Place* ignore) const;
    bool findSpot (const Panel&, int window, juce::Rectangle<int>& cells) const;
    juce::Point<int> minimumHostSize (int window) const;
    void clearTargets();
    void apply();
    void save();
    std::unique_ptr<juce::XmlElement> toXml() const;
    void fromXml (const juce::XmlElement&);
    void updateLimits();
    juce::String newId() const;

    juce::ApplicationProperties& props;
    juce::OwnedArray<Panel> blocks;
    std::vector<Place> places;
    std::vector<std::unique_ptr<Floating>> windows;
    struct WindowState { juce::Rectangle<int> bounds; bool full = false; };
    std::vector<WindowState> windowStates;
    bool editing = false, visible = true, applying = false;
};

} // namespace hawthorne
