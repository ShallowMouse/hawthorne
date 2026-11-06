#include "Layout.h"

namespace hawthorne
{

//==============================================================================
Panel::Panel (const juce::String& t, const juce::String& ti) : type (t), title (ti)
{
    addChildComponent (overlay);
    overlay.setAlwaysOnTop (true);
    overlay.setMouseCursor (juce::MouseCursor::DraggingHandCursor);
}

void Panel::set (const juce::Identifier& n, const juce::var& v)
{
    settings.set (n, v);
    settingsChanged();
    if (manager != nullptr) manager->settingsChanged (*this);
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
    g.drawText ("drag to move  -  corner to resize  -  right-click: settings", t, juce::Justification::centredTop, true);
}

void Panel::Overlay::mouseMove (const juce::MouseEvent& e)
{
    setMouseCursor (overCorner (e.getPosition()) ? juce::MouseCursor::BottomRightCornerResizeCursor : juce::MouseCursor::DraggingHandCursor);
}

void Panel::Overlay::mouseDown (const juce::MouseEvent& e)
{
    dragging = resizing = false;
    if (panel.manager == nullptr) return;
    if (e.mods.isPopupMenu()) { panel.manager->showBlockMenu (panel); return; }
    resizing = overCorner (e.getPosition());
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
    if (! manager.isEditing())
    {
        bool empty = true;
        for (auto* c : getChildren()) empty = empty && ! c->isVisible();
        if (empty)
        {
            g.setColour (Colours::textDim);
            g.setFont (monoFont (14.0f, true));
            g.drawText ("EMPTY DISPLAY  -  LAYOUT > EDIT DISPLAYS, THEN RIGHT-CLICK TO ADD BLOCKS", getLocalBounds(), juce::Justification::centred);
        }
        return;
    }
    g.setColour (Colours::edge.brighter (0.3f));
    for (int c = 1; c < cols; ++c) g.drawVerticalLine (juce::roundToInt ((float) c * cellW()), 0.0f, (float) getHeight());
    for (int r = 1; r < rows; ++r) g.drawHorizontalLine (juce::roundToInt ((float) r * cellH()), 0.0f, (float) getWidth());
}

void PanelHost::paintOverChildren (juce::Graphics& g)
{
    if (manager.isEditing())
    {
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
    if (e.mods.isPopupMenu()) manager.showDisplayMenu (windowIndex, cellAt (e.getPosition()));
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
    void closeButtonPressed() override    { manager.closeDisplay (host.windowIndex); }
    void maximiseButtonPressed() override { manager.toggleFullScreen (host.windowIndex); }
    void moved() override   { juce::DocumentWindow::moved(); manager.windowMoved(); }
    void resized() override { juce::DocumentWindow::resized(); manager.windowMoved(); }
    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress (juce::KeyPress::F11Key) || (full && k == juce::KeyPress (juce::KeyPress::escapeKey)))
        {
            manager.toggleFullScreen (host.windowIndex);
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
    for (auto* b : blocks) if (auto* parent = b->getParentComponent()) parent->removeChildComponent (b);
    windows.clear();
    blocks.clear();
}

Panel* LayoutManager::find (const juce::String& id) const
{
    for (auto* b : blocks) if (b->id == id) return b;
    return nullptr;
}

LayoutManager::Place* LayoutManager::placeOf (const juce::String& id)
{
    for (auto& pl : places) if (pl.block == id) return &pl;
    return nullptr;
}

PanelHost* LayoutManager::host (int w)
{
    return juce::isPositiveAndBelow (w - 1, (int) windows.size()) ? &windows[(size_t) w - 1]->host : nullptr;
}

const PanelHost* LayoutManager::host (int w) const
{
    return juce::isPositiveAndBelow (w - 1, (int) windows.size()) ? &windows[(size_t) w - 1]->host : nullptr;
}

juce::String LayoutManager::newId() const
{
    for (int i = 1;; ++i)
        if (find ("b" + juce::String (i)) == nullptr) return "b" + juce::String (i);
}

//==============================================================================
// Where a block can go: inside the grid, over no other block, its cells not too small for
// it to work (nor more than one cell beyond its maximum).
bool LayoutManager::fits (const Panel& p, int window, juce::Rectangle<int> c, const Place* ignore) const
{
    if (c.getWidth() < 1 || c.getHeight() < 1 || c.getX() < 0 || c.getY() < 0
         || c.getRight() > PanelHost::cols || c.getBottom() > PanelHost::rows) return false;
    for (auto& pl : places)
        if (&pl != ignore && pl.window == window && pl.cells.intersects (c)) return false;
    const auto* h = host (window);
    if (h == nullptr || h->getWidth() < 50 || h->getHeight() < 50) return true;
    const auto a = h->cellArea (c);
    if (a.getWidth() < p.minW || a.getHeight() < p.minH) return false;
    if (p.maxW > 0 && (float) a.getWidth() > (float) p.maxW + h->cellW()) return false;
    if (p.maxH > 0 && (float) a.getHeight() > (float) p.maxH + h->cellH()) return false;
    return true;
}

bool LayoutManager::findSpot (const Panel& p, int window, juce::Rectangle<int>& cells) const
{
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
            if (auto* p = find (pl.block))
            {
                w = juce::jmax (w, (int) std::ceil ((double) p->minW * PanelHost::cols / pl.cells.getWidth()));
                h = juce::jmax (h, (int) std::ceil ((double) p->minH * PanelHost::rows / pl.cells.getHeight()));
            }
    return { w, h };
}

//==============================================================================
void LayoutManager::restore()
{
    if (auto xml = props.getUserSettings()->getXmlValue ("displays"))
        fromXml (*xml);
    apply();
}

void LayoutManager::removeAllDisplays()
{
    windows.clear();
    places.clear();
    windowStates.clear();
    for (auto* b : blocks) if (auto* parent = b->getParentComponent()) parent->removeChildComponent (b);
    blocks.clear();
    apply();
    save();
}

std::unique_ptr<juce::XmlElement> LayoutManager::toXml() const
{
    auto xml = std::make_unique<juce::XmlElement> ("Displays");
    for (size_t i = 0; i < windows.size(); ++i)
    {
        auto* w = xml->createNewChildElement ("Display");
        w->setAttribute ("bounds", (i < windowStates.size() ? windowStates[i].bounds : windows[i]->getBounds()).toString());
        if (windows[i]->full) w->setAttribute ("full", true);
    }
    for (auto& pl : places)
        if (auto* b = find (pl.block))
        {
            auto* e = xml->createNewChildElement ("Block");
            e->setAttribute ("type", b->type);
            e->setAttribute ("display", pl.window);
            e->setAttribute ("cells", pl.cells.toString());
            for (auto& nv : b->settings)
                e->setAttribute ("s_" + nv.name.toString(), nv.value.toString());
        }
    return xml;
}

void LayoutManager::fromXml (const juce::XmlElement& xml)
{
    windows.clear();
    for (auto* b : blocks) if (auto* parent = b->getParentComponent()) parent->removeChildComponent (b);
    blocks.clear();
    places.clear();
    windowStates.clear();

    for (auto* w : xml.getChildWithTagNameIterator ("Display"))
        windowStates.push_back ({ juce::Rectangle<int>::fromString (w->getStringAttribute ("bounds")), w->getBoolAttribute ("full") });
    for (auto* e : xml.getChildWithTagNameIterator ("Block"))
    {
        const auto c = juce::Rectangle<int>::fromString (e->getStringAttribute ("cells"));
        const int w = e->getIntAttribute ("display", 1);
        if (c.isEmpty() || w < 1 || w > (int) windowStates.size() || ! createBlock) continue;
        bool clash = false;
        for (auto& pl : places) clash = clash || (pl.window == w && pl.cells.intersects (c));
        if (clash) continue;
        auto b = createBlock (e->getStringAttribute ("type"));
        if (b == nullptr) continue;
        for (int i = 0; i < e->getNumAttributes(); ++i)
            if (e->getAttributeName (i).startsWith ("s_"))
            {
                const auto v = e->getAttributeValue (i);
                b->settings.set (e->getAttributeName (i).substring (2), v.containsOnly ("-0123456789") && v.isNotEmpty() ? juce::var (v.getIntValue()) : juce::var (v));
            }
        b->id = newId();
        b->manager = this;
        b->settingsChanged();
        places.push_back ({ b->id, w, c });
        blocks.add (b.release());
    }
}

void LayoutManager::save()
{
    if (auto xml = toXml())
        props.getUserSettings()->setValue ("displays", xml.get());
    props.saveIfNeeded();
}

void LayoutManager::windowMoved()
{
    if (applying) return;
    for (size_t i = 0; i < windows.size() && i < windowStates.size(); ++i)
        if (! windows[i]->full) windowStates[i].bounds = windows[i]->getBounds();
    save();
}

void LayoutManager::settingsChanged (Panel& p)
{
    if (auto* pl = placeOf (p.id))
        if (auto* h = host (pl->window)) layoutHost (*h);
    updateLimits();
    save();
}

//==============================================================================
void LayoutManager::apply()
{
    const juce::ScopedValueSetter<bool> busy (applying, true);

    const int needed = (int) windowStates.size();
    while ((int) windows.size() > needed) windows.pop_back();
    while ((int) windows.size() < needed)
    {
        const int index = (int) windows.size() + 1;
        auto w = std::make_unique<Floating> (*this, index);
        auto b = windowStates[(size_t) index - 1].bounds;
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

    for (auto* b : blocks)
    {
        auto* pl = placeOf (b->id);
        auto* h = pl != nullptr ? host (pl->window) : nullptr;
        if (h == nullptr)
        {
            if (auto* parent = b->getParentComponent()) parent->removeChildComponent (b);
            b->setVisible (false);
            continue;
        }
        if (b->getParentComponent() != h) h->addAndMakeVisible (b);
        b->setVisible (true);
        b->setEditing (editing);
    }

    for (auto& w : windows)
    {
        w->setName ("Hawthorne - Display " + juce::String (w->host.windowIndex));
        w->setVisible (visible);
        layoutHost (w->host);
    }
    updateLimits();
    if (onChanged) onChanged();
}

void LayoutManager::updateLimits()
{
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
        auto* p = find (pl.block);
        if (p == nullptr) continue;
        const auto a = h.cellArea (pl.cells);
        int w = a.getWidth(), ht = a.getHeight();
        if (p->maxW > 0) w = juce::jmin (w, p->maxW);
        if (p->maxH > 0) ht = juce::jmin (ht, p->maxH);
        // never laid out smaller than it works at: scaled down instead
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
    if (on && windows.empty()) newDisplay();
    for (auto* b : blocks) b->setEditing (on);
    for (auto& w : windows) w->host.repaint();
    if (onChanged) onChanged();
}

int LayoutManager::newDisplay (juce::Point<int> screenPos)
{
    auto b = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea.withSizeKeepingCentre (1000, 640);
    if (screenPos != juce::Point<int>())
        b = b.withPosition (screenPos - juce::Point<int> (120, 20));
    windowStates.push_back ({ b, false });
    apply();
    save();
    return (int) windows.size();
}

void LayoutManager::closeDisplay (int index)
{
    // its blocks go with it
    std::vector<juce::String> gone;
    for (auto& pl : places) if (pl.window == index) gone.push_back (pl.block);
    places.erase (std::remove_if (places.begin(), places.end(), [&] (const Place& pl) { return pl.window == index; }), places.end());
    for (auto& pl : places) if (pl.window > index) --pl.window;
    if (juce::isPositiveAndBelow (index - 1, (int) windowStates.size())) windowStates.erase (windowStates.begin() + (index - 1));
    juce::MessageManager::callAsync ([this, gone]
    {
        windows.clear();
        for (auto& id : gone)
            if (auto* b = find (id)) { if (auto* parent = b->getParentComponent()) parent->removeChildComponent (b); blocks.removeObject (b); }
        apply();
        save();
    });
}

bool LayoutManager::isFull (int index) const
{
    return juce::isPositiveAndBelow (index - 1, (int) windows.size()) && windows[(size_t) index - 1]->full;
}

void LayoutManager::setVisible (bool v)
{
    visible = v;
    for (auto& w : windows) w->setVisible (v);
}

void LayoutManager::toggleFullScreen (int index)
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

void LayoutManager::addBlock (const juce::String& type, int display, juce::Point<int> cell)
{
    if (! createBlock || host (display) == nullptr) return;
    auto b = createBlock (type);
    if (b == nullptr) return;
    b->id = newId();
    b->manager = this;
    b->settingsChanged();
    juce::Point<int> span (2, 2);
    for (auto& t : blockTypes) if (t.type == type) span = t.defaultCells;
    juce::Rectangle<int> c (cell.x, cell.y, span.x, span.y);
    if (! findSpot (*b, display, c))
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Add " + b->title,
                                                "There's no room for it on this display. Make room, or use a new display.");
        return;
    }
    places.push_back ({ b->id, display, c });
    blocks.add (b.release());
    apply();
    save();
}

void LayoutManager::removeBlock (Panel& p)
{
    const auto id = p.id;
    places.erase (std::remove_if (places.begin(), places.end(), [&] (const Place& pl) { return pl.block == id; }), places.end());
    juce::MessageManager::callAsync ([this, id]
    {
        if (auto* b = find (id)) { if (auto* parent = b->getParentComponent()) parent->removeChildComponent (b); blocks.removeObject (b); }
        apply();
        save();
    });
}

//==============================================================================
void LayoutManager::clearTargets()
{
    for (int w = 1; w <= numDisplays(); ++w)
        if (auto* h = host (w))
            if (! h->target.isEmpty()) { h->target = {}; h->repaint(); }
}

void LayoutManager::dragPanel (Panel& p, juce::Point<int> pos, juce::Point<int> grab, bool finished)
{
    auto* pl = placeOf (p.id);
    if (pl == nullptr) return;

    PanelHost* over = nullptr;
    for (int w = numDisplays(); w >= 1 && over == nullptr; --w)
        if (auto* h = host (w))
            if (h->isShowing() && h->getScreenBounds().contains (pos)) over = h;

    if (over == nullptr)
    {
        clearTargets();
        if (! finished) return;
        // dropped outside every display: a new display there, the block filling what it can
        const int w = newDisplay (pos);
        juce::Rectangle<int> all (0, 0, PanelHost::cols, PanelHost::rows);
        pl = placeOf (p.id);
        if (pl != nullptr && findSpot (p, w, all)) { pl->window = w; pl->cells = all; }
        apply();
        save();
        return;
    }

    // the same size in cells - or, on another display, the nearest that works there
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

    for (int w = 1; w <= numDisplays(); ++w)
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

void LayoutManager::showBlockMenu (Panel& p)
{
    auto* pl = placeOf (p.id);
    if (pl == nullptr) return;
    juce::PopupMenu m;
    m.addSectionHeader (p.title.toUpperCase());
    p.addSettings (m);
    m.addSeparator();
    juce::PopupMenu to;
    for (int w = 1; w <= numDisplays(); ++w)
        to.addItem (2000 + w, "Display " + juce::String (w), w != pl->window);
    to.addItem (1999, "A new display");
    m.addSubMenu ("Move to", to);
    m.addItem (1001, "Remove");
    juce::Component::SafePointer<Panel> safe (&p);
    m.showMenuAsync (juce::PopupMenu::Options(), [this, safe] (int r)
    {
        if (safe == nullptr || r <= 0) return;
        auto& b = *safe;
        auto* place = placeOf (b.id);
        if (place == nullptr) return;
        if (r < 1000) { b.settingChosen (r); return; }
        if (r == 1001) { removeBlock (b); return; }
        const int w = r == 1999 ? newDisplay() : r - 2000;
        place = placeOf (b.id);
        juce::Rectangle<int> c = r == 1999 ? juce::Rectangle<int> (0, 0, PanelHost::cols, PanelHost::rows) : place->cells;
        const auto old = *place;
        places.erase (std::remove_if (places.begin(), places.end(), [&] (const Place& x) { return x.block == b.id; }), places.end());
        places.push_back (findSpot (b, w, c) ? Place { b.id, w, c } : old);
        apply();
        save();
    });
}

void LayoutManager::showDisplayMenu (int display, juce::Point<int> cell)
{
    juce::PopupMenu m;
    m.addSectionHeader ("DISPLAY " + juce::String (display));
    if (editing)
    {
        juce::PopupMenu add;
        for (int i = 0; i < (int) blockTypes.size(); ++i)
            add.addItem (100 + i, blockTypes[(size_t) i].name);
        m.addSubMenu ("Add a block here", add);
        m.addSeparator();
    }
    m.addItem (1, isFull (display) ? "Exit full screen (F11 / Esc)" : "Full screen (F11)");
    m.addItem (3, editing ? "Done editing" : "Edit displays...");
    m.addItem (2, "Close this display");
    m.showMenuAsync (juce::PopupMenu::Options(), [this, display, cell] (int r)
    {
        if (r == 1) toggleFullScreen (display);
        else if (r == 2) closeDisplay (display);
        else if (r == 3) setEditing (! editing);
        else if (r >= 100 && r - 100 < (int) blockTypes.size()) addBlock (blockTypes[(size_t) r - 100].type, display, cell);
    });
}

//==============================================================================
juce::StringArray LayoutManager::presetNames() const
{
    juce::StringArray names;
    if (auto xml = props.getUserSettings()->getXmlValue ("displayPresets"))
        for (auto* e : xml->getChildWithTagNameIterator ("Displays"))
            names.add (e->getStringAttribute ("name"));
    return names;
}

void LayoutManager::savePreset (const juce::String& name)
{
    auto all = props.getUserSettings()->getXmlValue ("displayPresets");
    if (all == nullptr) all = std::make_unique<juce::XmlElement> ("Presets");
    for (auto* e : all->getChildWithTagNameIterator ("Displays"))
        if (e->getStringAttribute ("name") == name) { all->removeChildElement (e, true); break; }
    auto xml = toXml();
    xml->setAttribute ("name", name);
    all->addChildElement (xml.release());
    props.getUserSettings()->setValue ("displayPresets", all.get());
    props.saveIfNeeded();
}

void LayoutManager::loadPreset (const juce::String& name)
{
    if (auto all = props.getUserSettings()->getXmlValue ("displayPresets"))
        for (auto* e : all->getChildWithTagNameIterator ("Displays"))
            if (e->getStringAttribute ("name") == name)
            {
                fromXml (*e);
                apply();
                save();
                return;
            }
}

void LayoutManager::deletePreset (const juce::String& name)
{
    if (auto all = props.getUserSettings()->getXmlValue ("displayPresets"))
    {
        for (auto* e : all->getChildWithTagNameIterator ("Displays"))
            if (e->getStringAttribute ("name") == name) { all->removeChildElement (e, true); break; }
        props.getUserSettings()->setValue ("displayPresets", all.get());
        props.saveIfNeeded();
    }
}

} // namespace hawthorne
