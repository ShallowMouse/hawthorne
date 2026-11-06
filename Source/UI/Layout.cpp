#include "Layout.h"

namespace hawthorne
{

//==============================================================================
Panel::Panel (const juce::String& i, const juce::String& t) : id (i), title (t)
{
    addChildComponent (overlay);
    overlay.setAlwaysOnTop (true);
    overlay.setMouseCursor (juce::MouseCursor::DraggingHandCursor);
}

static constexpr int cornerSize = 22;

bool Panel::Overlay::overCorner (juce::Point<int> p) const
{
    return p.x > getWidth() - cornerSize && p.y > getHeight() - cornerSize;
}

void Panel::Overlay::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    g.setColour ((dragging || resizing ? Colours::mustard : juce::Colour (0xff1c2a3a)).withAlpha (dragging || resizing ? 0.3f : 0.72f));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (Colours::mustard);
    g.drawRoundedRectangle (r, 6.0f, 2.0f);

    // the corner handle
    auto c = r.removeFromBottom ((float) cornerSize - 4.0f).removeFromRight ((float) cornerSize - 4.0f);
    juce::Path tri;
    tri.addTriangle (c.getRight(), c.getY(), c.getRight(), c.getBottom(), c.getX(), c.getBottom());
    g.fillPath (tri);

    auto t = getLocalBounds().toFloat().reduced (8.0f);
    t = t.withSizeKeepingCentre (t.getWidth(), juce::jmin (t.getHeight(), 64.0f));
    g.setFont (monoFont (juce::jlimit (12.0f, 22.0f, t.getHeight() * 0.4f), true));
    g.drawText (panel.title.toUpperCase(), t.removeFromTop (t.getHeight() * 0.55f), juce::Justification::centredBottom, true);
    g.setColour (Colours::text);
    g.setFont (monoFont (11.0f));
    g.drawText ("drag to move  -  corner to resize  -  right-click", t, juce::Justification::centredTop, true);
}

void Panel::Overlay::mouseMove (const juce::MouseEvent& e)
{
    setMouseCursor (overCorner (e.getPosition()) ? juce::MouseCursor::BottomRightCornerResizeCursor : juce::MouseCursor::DraggingHandCursor);
}

void Panel::Overlay::mouseDown (const juce::MouseEvent& e)
{
    dragging = resizing = false;
    if (panel.manager == nullptr) return;
    if (e.mods.isPopupMenu()) { panel.manager->showPanelMenu (panel); return; }
    resizing = overCorner (e.getPosition());
    // which of its cells it's held by (so it moves with the mouse, not jumping to its corner)
    if (auto* host = dynamic_cast<PanelHost*> (panel.getParentComponent()))
    {
        const auto cell = host->cellAt (host->getLocalPoint (nullptr, e.getScreenPosition()));
        const auto first = host->cellAt (host->getLocalPoint (nullptr, panel.localPointToGlobal (juce::Point<int> (1, 1))));
        grabCell = cell - first;
    }
    repaint();
}

void Panel::Overlay::mouseDrag (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() || panel.manager == nullptr) return;
    if (resizing) { panel.manager->resizePanel (panel, e.getScreenPosition(), false); return; }
    if (e.getDistanceFromDragStart() < 6) return;
    if (! dragging) { dragging = true; repaint(); }
    panel.manager->dragPanel (panel, e.getScreenPosition(), grabCell, false);
}

void Panel::Overlay::mouseUp (const juce::MouseEvent& e)
{
    if (panel.manager == nullptr || e.mods.isPopupMenu()) return;
    const bool wasResizing = resizing, wasDragging = dragging;
    dragging = resizing = false;
    repaint();
    if (wasResizing)      panel.manager->resizePanel (panel, e.getScreenPosition(), true);
    else if (wasDragging) panel.manager->dragPanel (panel, e.getScreenPosition(), grabCell, true);
}

//==============================================================================
juce::Rectangle<int> PanelHost::cellArea (juce::Rectangle<int> c) const
{
    const auto x0 = juce::roundToInt ((float) c.getX() * cellW()), x1 = juce::roundToInt ((float) c.getRight() * cellW());
    const auto y0 = juce::roundToInt ((float) c.getY() * cellH()), y1 = juce::roundToInt ((float) c.getBottom() * cellH());
    return { x0, y0, x1 - x0, y1 - y0 };
}

juce::Point<int> PanelHost::cellAt (juce::Point<int> p) const
{
    return { juce::jlimit (0, cols - 1, (int) std::floor ((float) p.x / juce::jmax (1.0f, cellW()))),
             juce::jlimit (0, rows - 1, (int) std::floor ((float) p.y / juce::jmax (1.0f, cellH()))) };
}

void PanelHost::resized()   { manager.layoutHost (*this); }

void PanelHost::paint (juce::Graphics& g)
{
    g.fillAll (Colours::bg);
    if (! manager.isEditing()) return;
    g.setColour (Colours::edge.brighter (0.3f));
    for (int c = 1; c < cols; ++c) g.drawVerticalLine (juce::roundToInt ((float) c * cellW()), 0.0f, (float) getHeight());
    for (int r = 1; r < rows; ++r) g.drawHorizontalLine (juce::roundToInt ((float) r * cellH()), 0.0f, (float) getWidth());
}

void PanelHost::paintOverChildren (juce::Graphics& g)
{
    if (manager.isEditing())
    {
        // the grid, faintly over the panels too
        g.setColour (Colours::mustard.withAlpha (0.18f));
        for (int c = 1; c < cols; ++c) g.drawVerticalLine (juce::roundToInt ((float) c * cellW()), 0.0f, (float) getHeight());
        for (int r = 1; r < rows; ++r) g.drawHorizontalLine (juce::roundToInt ((float) r * cellH()), 0.0f, (float) getWidth());
    }
    if (target.isEmpty()) return;
    const auto a = cellArea (target).reduced (2);
    const auto c = targetOK ? juce::Colour (0xff5fcf6a) : Colours::red;
    g.setColour (c.withAlpha (0.25f));
    g.fillRect (a);
    g.setColour (c);
    g.drawRect (a, 3);
    if (! targetOK)
    {
        g.setFont (monoFont (13.0f, true));
        g.drawText ("DOESN'T FIT HERE", a, juce::Justification::centred, true);
    }
}

void PanelHost::mouseUp (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) manager.showWindowMenu (windowIndex);
}

//==============================================================================
struct LayoutManager::Floating : public juce::DocumentWindow
{
    Floating (LayoutManager& m, int index)
        : juce::DocumentWindow ("Hawthorne", Colours::bg, juce::DocumentWindow::allButtons),
          manager (m), host (m, index)
    {
        setUsingNativeTitleBar (false);
        setTitleBarHeight (30);
        setContentNonOwned (&host, false);
        setResizable (true, false);
        setResizeLimits (300, 200, 20000, 20000);
        tips = std::make_unique<juce::TooltipWindow> (&host, 600);
    }
    ~Floating() override
    {
        tips.reset();
        clearContentComponent();
    }
    void closeButtonPressed() override { manager.windowClosed (host.windowIndex); }
    void maximiseButtonPressed() override { manager.toggleWindowFullScreen (host.windowIndex); }
    void moved() override   { juce::DocumentWindow::moved(); manager.windowMoved(); }
    void resized() override { juce::DocumentWindow::resized(); manager.windowMoved(); }
    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress (juce::KeyPress::F11Key) || (full && k == juce::KeyPress (juce::KeyPress::escapeKey)))
        {
            manager.toggleWindowFullScreen (host.windowIndex);
            return true;
        }
        return manager.onKey && manager.onKey (k);
    }

    /** Full screen: no title bar, the whole of the monitor it's on. */
    void setFull (bool on, juce::Rectangle<int> normal)
    {
        full = on;
        if (on)
        {
            const auto* d = juce::Desktop::getInstance().getDisplays().getDisplayForRect (normal.isEmpty() ? getBounds() : normal);
            setTitleBarHeight (0);
            setResizable (false, false);
            setBounds (d != nullptr ? d->totalArea : getBounds());
            toFront (true);
        }
        else
        {
            setTitleBarHeight (30);
            setResizable (true, false);
            if (! normal.isEmpty()) setBounds (normal);
        }
    }

    LayoutManager& manager;
    PanelHost host;
    bool full = false;
    std::unique_ptr<juce::TooltipWindow> tips;
};

//==============================================================================
LayoutManager::LayoutManager (juce::ApplicationProperties& p) : props (p) {}

LayoutManager::~LayoutManager()
{
    for (auto* p : panels) if (auto* parent = p->getParentComponent()) parent->removeChildComponent (p);
    windows.clear();
}

void LayoutManager::addPanel (Panel& p, juce::Rectangle<int> cells)
{
    p.manager = this;
    panels.add (&p);
    defaultPlaces.push_back ({ p.id, 0, cells });
}

Panel* LayoutManager::find (const juce::String& id) const
{
    for (auto* p : panels) if (p->id == id) return p;
    return nullptr;
}

LayoutManager::Place* LayoutManager::placeOf (const juce::String& id)
{
    for (auto& pl : places) if (pl.panel == id) return &pl;
    return nullptr;
}

PanelHost* LayoutManager::host (int w)
{
    if (w == 0) return &main;
    return juce::isPositiveAndBelow (w - 1, (int) windows.size()) ? &windows[(size_t) w - 1]->host : nullptr;
}

const PanelHost* LayoutManager::host (int w) const
{
    if (w == 0) return &main;
    return juce::isPositiveAndBelow (w - 1, (int) windows.size()) ? &windows[(size_t) w - 1]->host : nullptr;
}

bool LayoutManager::isShown (const Panel& p) const
{
    for (auto& pl : places) if (pl.panel == p.id) return true;
    return false;
}

//==============================================================================
// Where a panel can go: inside the grid, over no other panel, and the cells' size not too
// small for it to work (nor much too big: no more than one cell beyond its maximum).
bool LayoutManager::fits (const Panel& p, int window, juce::Rectangle<int> c, const Place* ignore) const
{
    if (c.getWidth() < 1 || c.getHeight() < 1 || c.getX() < 0 || c.getY() < 0
         || c.getRight() > PanelHost::cols || c.getBottom() > PanelHost::rows) return false;
    for (auto& pl : places)
        if (&pl != ignore && pl.window == window && pl.cells.intersects (c)) return false;
    const auto* h = host (window);
    if (h == nullptr || h->getWidth() < 50 || h->getHeight() < 50) return true;      // (not on screen yet)
    const auto a = h->cellArea (c);
    if (a.getWidth() < p.minW || a.getHeight() < p.minH) return false;
    if (p.maxW > 0 && (float) a.getWidth() > (float) p.maxW + h->cellW()) return false;
    if (p.maxH > 0 && (float) a.getHeight() > (float) p.maxH + h->cellH()) return false;
    return true;
}

bool LayoutManager::findSpot (const Panel& p, int window, juce::Rectangle<int>& cells) const
{
    // the size asked for first, then the others nearest to it
    std::vector<juce::Point<int>> spans;
    spans.push_back ({ juce::jlimit (1, PanelHost::cols, cells.getWidth()), juce::jlimit (1, PanelHost::rows, cells.getHeight()) });
    std::vector<juce::Point<int>> others;
    for (int w = 1; w <= PanelHost::cols; ++w)
        for (int h = 1; h <= PanelHost::rows; ++h)
            if (juce::Point<int> (w, h) != spans[0]) others.push_back ({ w, h });
    const int want = spans[0].x * spans[0].y;
    std::stable_sort (others.begin(), others.end(), [want] (auto a, auto b) { return std::abs (a.x * a.y - want) < std::abs (b.x * b.y - want); });
    spans.insert (spans.end(), others.begin(), others.end());

    for (auto s : spans)
    {
        // where it was asked for, then anywhere (top left first)
        juce::Rectangle<int> asked (cells.getX(), cells.getY(), s.x, s.y);
        if (fits (p, window, asked, nullptr)) { cells = asked; return true; }
        for (int y = 0; y + s.y <= PanelHost::rows; ++y)
            for (int x = 0; x + s.x <= PanelHost::cols; ++x)
                if (fits (p, window, { x, y, s.x, s.y }, nullptr)) { cells = { x, y, s.x, s.y }; return true; }
    }
    return false;
}

juce::Point<int> LayoutManager::minimumHostSize (int window) const
{
    int w = 300, h = 200;
    for (auto& pl : places)
        if (pl.window == window)
            if (auto* p = find (pl.panel))
            {
                w = juce::jmax (w, (int) std::ceil ((double) p->minW * PanelHost::cols / pl.cells.getWidth()));
                h = juce::jmax (h, (int) std::ceil ((double) p->minH * PanelHost::rows / pl.cells.getHeight()));
            }
    return { w, h };
}

//==============================================================================
void LayoutManager::defaults()
{
    places = defaultPlaces;
    windowStates.clear();
}

void LayoutManager::restore()
{
    defaults();
    if (auto xml = props.getUserSettings()->getXmlValue ("layoutGrid"))
        fromXml (*xml);
    apply();
}

void LayoutManager::resetToDefault()
{
    windows.clear();
    defaults();
    apply();
    save();
}

std::unique_ptr<juce::XmlElement> LayoutManager::toXml() const
{
    auto xml = std::make_unique<juce::XmlElement> ("Layout");
    for (size_t i = 0; i < windows.size(); ++i)
    {
        auto* w = xml->createNewChildElement ("Window");
        const bool full = windows[i]->full;
        w->setAttribute ("bounds", (i < windowStates.size() ? windowStates[i].bounds : windows[i]->getBounds()).toString());
        if (full) w->setAttribute ("full", true);
    }
    for (auto& pl : places)
    {
        auto* e = xml->createNewChildElement ("Panel");
        e->setAttribute ("id", pl.panel);
        e->setAttribute ("window", pl.window);
        e->setAttribute ("cells", pl.cells.toString());
    }
    return xml;
}

void LayoutManager::fromXml (const juce::XmlElement& xml)
{
    std::vector<Place> loaded;
    std::vector<WindowState> states;
    for (auto* w : xml.getChildWithTagNameIterator ("Window"))
        states.push_back ({ juce::Rectangle<int>::fromString (w->getStringAttribute ("bounds")), w->getBoolAttribute ("full") });
    for (auto* e : xml.getChildWithTagNameIterator ("Panel"))
    {
        const auto id = e->getStringAttribute ("id");
        const auto c = juce::Rectangle<int>::fromString (e->getStringAttribute ("cells"));
        const int w = juce::jlimit (0, (int) states.size(), e->getIntAttribute ("window", 0));
        if (find (id) == nullptr || c.isEmpty()) continue;
        bool clash = false;
        for (auto& l : loaded) clash = clash || l.panel == id || (l.window == w && l.cells.intersects (c));
        if (clash) continue;
        loaded.push_back ({ id, w, c });
    }
    places = loaded;
    windowStates = states;
}

void LayoutManager::save()
{
    if (auto xml = toXml())
        props.getUserSettings()->setValue ("layoutGrid", xml.get());
    props.saveIfNeeded();
}

void LayoutManager::windowMoved()
{
    if (applying) return;
    for (size_t i = 0; i < windows.size() && i < windowStates.size(); ++i)
        if (! windows[i]->full) windowStates[i].bounds = windows[i]->getBounds();
    save();
}

//==============================================================================
void LayoutManager::apply()
{
    const juce::ScopedValueSetter<bool> busy (applying, true);

    int needed = (int) windowStates.size();
    for (auto& pl : places) needed = juce::jmax (needed, pl.window);
    while ((int) windowStates.size() < needed) windowStates.push_back ({});
    while ((int) windows.size() > needed) windows.pop_back();
    while ((int) windows.size() < needed)
    {
        const int index = (int) windows.size() + 1;
        auto w = std::make_unique<Floating> (*this, index);
        auto b = windowStates[(size_t) index - 1].bounds;
        // keep it on a screen that's there now
        bool onScreen = false;
        for (auto& d : juce::Desktop::getInstance().getDisplays().displays)
            onScreen = onScreen || d.userArea.intersects (b.withHeight (40));
        if (b.getWidth() < 200 || ! onScreen)
            b = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea.withSizeKeepingCentre (1000, 640)
                    .translated (40 * index, 40 * index);
        windowStates[(size_t) index - 1].bounds = b;
        w->setBounds (b);
        if (windowStates[(size_t) index - 1].full) w->setFull (true, b);
        windows.push_back (std::move (w));
    }

    for (auto* p : panels)
    {
        auto* pl = placeOf (p->id);
        auto* h = pl != nullptr ? host (pl->window) : nullptr;
        if (h == nullptr)
        {
            if (auto* parent = p->getParentComponent()) parent->removeChildComponent (p);
            p->setVisible (false);
            continue;
        }
        if (p->getParentComponent() != h) h->addAndMakeVisible (p);
        p->setVisible (true);
        p->setEditing (editing);
    }

    for (auto& w : windows)
    {
        juce::StringArray names;
        for (auto& pl : places) if (pl.window == w->host.windowIndex) if (auto* p = find (pl.panel)) names.add (p->title);
        w->setName ("Hawthorne - " + (names.isEmpty() ? juce::String ("empty window (LAYOUT > Edit layout, drag panels in)") : names.joinIntoString (", ")));
        w->setVisible (floatingVisible);
        layoutHost (w->host);
    }
    layoutHost (main);
    updateLimits();
    if (onChanged) onChanged();
}

void LayoutManager::updateLimits()
{
    // a window can't be made smaller than its panels can work at (up to the size of the screen)
    for (auto& w : windows)
    {
        if (w->full) continue;
        auto m = minimumHostSize (w->host.windowIndex);
        const auto* d = juce::Desktop::getInstance().getDisplays().getDisplayForRect (w->getBounds());
        const auto screen = d != nullptr ? d->userArea : juce::Rectangle<int> (0, 0, 1920, 1080);
        w->setResizeLimits (juce::jmin (m.x + 8, screen.getWidth()), juce::jmin (m.y + 38, screen.getHeight()), 20000, 20000);
    }
}

void LayoutManager::layoutHost (PanelHost& h)
{
    for (auto& pl : places)
    {
        if (pl.window != h.windowIndex) continue;
        auto* p = find (pl.panel);
        if (p == nullptr) continue;
        const auto a = h.cellArea (pl.cells);
        // no bigger than it should be (centred in its cells) ...
        int w = a.getWidth(), ht = a.getHeight();
        if (p->maxW > 0) w = juce::jmin (w, p->maxW);
        if (p->maxH > 0) ht = juce::jmin (ht, p->maxH);
        // ... and never laid out smaller than it works at: scaled down instead
        const int bw = juce::jmax (w, p->minW), bh = juce::jmax (ht, p->minH);
        const float s = juce::jmin (1.0f, (float) a.getWidth() / (float) bw, (float) a.getHeight() / (float) bh);
        if (s < 0.999f)
        {
            p->setBounds (0, 0, bw, bh);
            p->setTransform (juce::AffineTransform::scale (s).translated ((float) a.getX() + ((float) a.getWidth() - (float) bw * s) * 0.5f,
                                                                          (float) a.getY() + ((float) a.getHeight() - (float) bh * s) * 0.5f));
        }
        else
        {
            p->setTransform ({});
            p->setBounds (a.withSizeKeepingCentre (bw, bh));
        }
    }
    h.repaint();
}

//==============================================================================
void LayoutManager::setEditing (bool on)
{
    editing = on;
    for (auto* p : panels) p->setEditing (on && isShown (*p));
    main.repaint();
    for (auto& w : windows) w->host.repaint();
    if (onChanged) onChanged();
}

void LayoutManager::setShown (Panel& p, bool show)
{
    if (show == isShown (p)) return;
    if (show)
    {
        // where it lives by default, else anywhere it fits, else a new window
        juce::Rectangle<int> c;
        for (auto& d : defaultPlaces) if (d.panel == p.id) c = d.cells;
        bool placed = false;
        for (int w = 0; w < numWindows() && ! placed; ++w)
        {
            auto cw = c;
            if (findSpot (p, w, cw)) { places.push_back ({ p.id, w, cw }); placed = true; }
        }
        if (! placed)
        {
            const int w = newWindow();
            juce::Rectangle<int> all (0, 0, PanelHost::cols, PanelHost::rows);
            if (findSpot (p, w, all)) places.push_back ({ p.id, w, all });
        }
    }
    else
        places.erase (std::remove_if (places.begin(), places.end(), [&] (const Place& pl) { return pl.panel == p.id; }), places.end());
    apply();
    save();
}

int LayoutManager::newWindow (juce::Point<int> screenPos)
{
    auto b = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea.withSizeKeepingCentre (1000, 640);
    if (screenPos != juce::Point<int>())
        b = b.withPosition (screenPos - juce::Point<int> (120, 20));
    windowStates.push_back ({ b, false });
    apply();
    save();
    return (int) windows.size();
}

void LayoutManager::windowClosed (int index)
{
    // its panels go back to the main window where they fit (else they're hidden)
    std::vector<juce::String> moving;
    for (auto& pl : places) if (pl.window == index) moving.push_back (pl.panel);
    places.erase (std::remove_if (places.begin(), places.end(), [&] (const Place& pl) { return pl.window == index; }), places.end());
    for (auto& pl : places) if (pl.window > index) --pl.window;
    if (juce::isPositiveAndBelow (index - 1, (int) windowStates.size())) windowStates.erase (windowStates.begin() + (index - 1));
    juce::MessageManager::callAsync ([this, moving]
    {
        windows.clear();
        apply();
        for (auto& id : moving)
            if (auto* p = find (id))
            {
                juce::Rectangle<int> c;
                for (auto& d : defaultPlaces) if (d.panel == id) c = d.cells;
                if (findSpot (*p, 0, c)) places.push_back ({ id, 0, c });
            }
        apply();
        save();
    });
}

void LayoutManager::setFloatingVisible (bool v)
{
    floatingVisible = v;
    for (auto& w : windows) w->setVisible (v);
}

void LayoutManager::toggleWindowFullScreen (int index)
{
    if (! juce::isPositiveAndBelow (index - 1, (int) windows.size())) return;
    auto& w = *windows[(size_t) index - 1];
    auto& st = windowStates[(size_t) index - 1];
    {
        const juce::ScopedValueSetter<bool> busy (applying, true);
        if (! w.full) st.bounds = w.getBounds();
        w.setFull (! w.full, st.bounds);
        st.full = w.full;
    }
    updateLimits();
    save();
}

//==============================================================================
void LayoutManager::clearTargets()
{
    for (int w = 0; w < numWindows(); ++w)
        if (auto* h = host (w))
            if (! h->target.isEmpty()) { h->target = {}; h->repaint(); }
}

void LayoutManager::dragPanel (Panel& p, juce::Point<int> pos, juce::Point<int> grab, bool finished)
{
    auto* pl = placeOf (p.id);
    if (pl == nullptr) return;

    PanelHost* over = nullptr;
    for (int w = numWindows() - 1; w >= 0 && over == nullptr; --w)
        if (auto* h = host (w))
            if (h->isShowing() && h->getScreenBounds().contains (pos)) over = h;

    if (over == nullptr)
    {
        clearTargets();
        if (! finished) return;
        // dropped outside every window: a new window there, the panel filling what it can
        const int w = newWindow (pos);
        juce::Rectangle<int> all (0, 0, PanelHost::cols, PanelHost::rows);
        pl = placeOf (p.id);
        const auto old = *pl;
        places.erase (std::remove_if (places.begin(), places.end(), [&] (const Place& x) { return x.panel == p.id; }), places.end());
        places.push_back (findSpot (p, w, all) ? Place { p.id, w, all } : old);
        apply();
        save();
        return;
    }

    // the same size in cells - or, in another window, the nearest that works there
    juce::Point<int> span (pl->cells.getWidth(), pl->cells.getHeight());
    if (over->windowIndex != pl->window && over->getWidth() > 50)
    {
        const int minC = (int) std::ceil ((float) p.minW / over->cellW()), minR = (int) std::ceil ((float) p.minH / over->cellH());
        span.x = juce::jmax (span.x, minC);
        span.y = juce::jmax (span.y, minR);
        if (p.maxW > 0) span.x = juce::jmin (span.x, juce::jmax (minC, (int) std::ceil ((float) p.maxW / over->cellW())));
        if (p.maxH > 0) span.y = juce::jmin (span.y, juce::jmax (minR, (int) std::ceil ((float) p.maxH / over->cellH())));
        span.x = juce::jlimit (1, PanelHost::cols, span.x);
        span.y = juce::jlimit (1, PanelHost::rows, span.y);
    }
    auto cell = over->cellAt (over->getLocalPoint (nullptr, pos)) - grab;
    cell.x = juce::jlimit (0, PanelHost::cols - span.x, cell.x);
    cell.y = juce::jlimit (0, PanelHost::rows - span.y, cell.y);
    const juce::Rectangle<int> t (cell.x, cell.y, span.x, span.y);
    const bool ok = fits (p, over->windowIndex, t, pl);

    for (int w = 0; w < numWindows(); ++w)
        if (auto* h = host (w))
            if (h != over && ! h->target.isEmpty()) { h->target = {}; h->repaint(); }
    if (! finished)
    {
        if (over->target != t || over->targetOK != ok) { over->target = t; over->targetOK = ok; over->repaint(); }
        return;
    }
    clearTargets();
    if (! ok) return;
    pl->window = over->windowIndex;
    pl->cells = t;
    apply();
    save();
}

void LayoutManager::resizePanel (Panel& p, juce::Point<int> pos, bool finished)
{
    auto* pl = placeOf (p.id);
    auto* h = pl != nullptr ? host (pl->window) : nullptr;
    if (h == nullptr) return;
    const auto cell = h->cellAt (h->getLocalPoint (nullptr, pos));
    const juce::Rectangle<int> t (pl->cells.getX(), pl->cells.getY(),
                                  juce::jmax (1, cell.x - pl->cells.getX() + 1), juce::jmax (1, cell.y - pl->cells.getY() + 1));
    const bool ok = fits (p, pl->window, t, pl);
    if (! finished)
    {
        if (h->target != t || h->targetOK != ok) { h->target = t; h->targetOK = ok; h->repaint(); }
        return;
    }
    clearTargets();
    if (! ok) return;
    pl->cells = t;
    apply();
    save();
}

void LayoutManager::showPanelMenu (Panel& p)
{
    auto* pl = placeOf (p.id);
    if (pl == nullptr) return;
    juce::PopupMenu m;
    m.addSectionHeader (p.title.toUpperCase());
    juce::PopupMenu to;
    for (int w = 0; w < numWindows(); ++w)
        to.addItem (100 + w, w == 0 ? juce::String ("Main window") : "Window " + juce::String (w + 1), w != pl->window);
    to.addItem (99, "A new window");
    m.addSubMenu ("Move to", to);
    m.addItem (1, "Hide");
    m.showMenuAsync (juce::PopupMenu::Options(), [this, &p] (int r)
    {
        auto* place = placeOf (p.id);
        if (r <= 0 || place == nullptr) return;
        if (r == 1) { setShown (p, false); return; }
        const int w = r == 99 ? newWindow() : r - 100;
        place = placeOf (p.id);
        juce::Rectangle<int> c = r == 99 ? juce::Rectangle<int> (0, 0, PanelHost::cols, PanelHost::rows) : place->cells;
        const auto old = *place;
        places.erase (std::remove_if (places.begin(), places.end(), [&] (const Place& x) { return x.panel == p.id; }), places.end());
        places.push_back (findSpot (p, w, c) ? Place { p.id, w, c } : old);   // (stays put if it fits nowhere there)
        apply();
        save();
    });
}

void LayoutManager::showWindowMenu (int window)
{
    juce::PopupMenu m;
    if (window > 0)
    {
        const bool full = juce::isPositiveAndBelow (window - 1, (int) windows.size()) && windows[(size_t) window - 1]->full;
        m.addItem (1, full ? "Exit full screen (F11 / Esc)" : "Full screen (F11)");
        m.addItem (2, "Close this window");
    }
    m.addItem (3, editing ? "Done editing" : "Edit layout...");
    m.showMenuAsync (juce::PopupMenu::Options(), [this, window] (int r)
    {
        if (r == 1) toggleWindowFullScreen (window);
        else if (r == 2) windowClosed (window);
        else if (r == 3) setEditing (! editing);
    });
}

//==============================================================================
juce::StringArray LayoutManager::presetNames() const
{
    juce::StringArray names;
    if (auto xml = props.getUserSettings()->getXmlValue ("layoutGridPresets"))
        for (auto* e : xml->getChildWithTagNameIterator ("Layout"))
            names.add (e->getStringAttribute ("name"));
    return names;
}

void LayoutManager::savePreset (const juce::String& name)
{
    auto all = props.getUserSettings()->getXmlValue ("layoutGridPresets");
    if (all == nullptr) all = std::make_unique<juce::XmlElement> ("Presets");
    for (auto* e : all->getChildWithTagNameIterator ("Layout"))
        if (e->getStringAttribute ("name") == name) { all->removeChildElement (e, true); break; }
    auto xml = toXml();
    xml->setAttribute ("name", name);
    all->addChildElement (xml.release());
    props.getUserSettings()->setValue ("layoutGridPresets", all.get());
    props.saveIfNeeded();
}

void LayoutManager::loadPreset (const juce::String& name)
{
    if (auto all = props.getUserSettings()->getXmlValue ("layoutGridPresets"))
        for (auto* e : all->getChildWithTagNameIterator ("Layout"))
            if (e->getStringAttribute ("name") == name)
            {
                windows.clear();
                fromXml (*e);
                apply();
                save();
                return;
            }
}

void LayoutManager::deletePreset (const juce::String& name)
{
    if (auto all = props.getUserSettings()->getXmlValue ("layoutGridPresets"))
    {
        for (auto* e : all->getChildWithTagNameIterator ("Layout"))
            if (e->getStringAttribute ("name") == name) { all->removeChildElement (e, true); break; }
        props.getUserSettings()->setValue ("layoutGridPresets", all.get());
        props.saveIfNeeded();
    }
}

} // namespace hawthorne
