#include "Layout.h"

namespace hawthorne
{

static const char* edgeName (Edge e)
{
    switch (e)
    {
        case Edge::top:    return "top";
        case Edge::bottom: return "bottom";
        case Edge::left:   return "left";
        case Edge::right:  return "right";
        default:           return "centre";
    }
}
static Edge edgeFrom (const juce::String& s)
{
    if (s == "top") return Edge::top;
    if (s == "bottom") return Edge::bottom;
    if (s == "left") return Edge::left;
    if (s == "right") return Edge::right;
    return Edge::centre;
}

//==============================================================================
Panel::Panel (const juce::String& i, const juce::String& t) : id (i), title (t)
{
    addChildComponent (overlay);
    overlay.setAlwaysOnTop (true);
    overlay.setMouseCursor (juce::MouseCursor::DraggingHandCursor);
}

void Panel::Overlay::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    g.setColour ((dragging ? Colours::mustard : juce::Colour (0xff1c2a3a)).withAlpha (dragging ? 0.35f : 0.72f));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (Colours::mustard);
    g.drawRoundedRectangle (r, 6.0f, 2.0f);
    g.setFont (monoFont (juce::jlimit (12.0f, 22.0f, r.getHeight() * 0.25f), true));
    auto t = r.withSizeKeepingCentre (r.getWidth(), juce::jmin (r.getHeight(), 60.0f));
    g.drawText (panel.title.toUpperCase(), t.removeFromTop (t.getHeight() * 0.55f), juce::Justification::centredBottom, true);
    g.setColour (Colours::text);
    g.setFont (monoFont (11.0f));
    g.drawText ("drag to move  -  right-click for options", t, juce::Justification::centredTop, true);
}

void Panel::Overlay::mouseDown (const juce::MouseEvent& e)
{
    dragging = false;
    if (e.mods.isPopupMenu() && panel.manager != nullptr)
        panel.manager->showPanelMenu (panel);
}

void Panel::Overlay::mouseDrag (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() || panel.manager == nullptr || e.getDistanceFromDragStart() < 6) return;
    if (! dragging) { dragging = true; repaint(); }
    panel.manager->dragPanel (panel, e.getScreenPosition(), false);
}

void Panel::Overlay::mouseUp (const juce::MouseEvent& e)
{
    if (! dragging || panel.manager == nullptr) return;
    dragging = false;
    repaint();
    panel.manager->dragPanel (panel, e.getScreenPosition(), true);
}

//==============================================================================
void PanelHost::resized()   { manager.layoutHost (*this); }

void PanelHost::paint (juce::Graphics& g)
{
    g.fillAll (Colours::bg);
}

void PanelHost::paintOverChildren (juce::Graphics& g)
{
    if (dropZone.isEmpty()) return;
    g.setColour (Colours::mustard.withAlpha (0.28f));
    g.fillRect (dropZone);
    g.setColour (Colours::mustard);
    g.drawRect (dropZone, 3);
}

void PanelHost::Divider::paint (juce::Graphics& g)
{
    if (! isMouseOverOrDragging()) return;
    g.setColour (Colours::mustard.withAlpha (0.8f));
    if (vertical) g.fillRect (getLocalBounds().withSizeKeepingCentre (2, getHeight()));
    else          g.fillRect (getLocalBounds().withSizeKeepingCentre (getWidth(), 2));
}

void PanelHost::Divider::mouseDown (const juce::MouseEvent&)
{
    // where the drag measures from
    if (edge >= 0)
    {
        const auto c = getBounds().getCentre();
        int thick = 0;
        for (auto* p : host.getChildren())
            if (dynamic_cast<Panel*> (p) != nullptr && p->isVisible())
            {
                const auto pb = p->getBounds();
                if (edge == 0 && pb.getBottom() == c.y) thick = juce::jmax (thick, pb.getHeight());
                if (edge == 1 && pb.getY() == c.y)      thick = juce::jmax (thick, pb.getHeight());
                if (edge == 2 && pb.getRight() == c.x)  thick = juce::jmax (thick, pb.getWidth());
                if (edge == 3 && pb.getX() == c.x)      thick = juce::jmax (thick, pb.getWidth());
            }
        start = edge == 0 ? c.y - thick : edge == 1 ? c.y + thick : edge == 2 ? c.x - thick : c.x + thick;
    }
    else
    {
        juce::Component* pa = nullptr;
        juce::Component* pb = nullptr;
        for (auto* p : host.getChildren())
            if (auto* panel = dynamic_cast<Panel*> (p))
            {
                if (panel->id == a) pa = panel;
                if (panel->id == b) pb = panel;
            }
        if (pa == nullptr || pb == nullptr) return;
        start = vertical ? pa->getX() : pa->getY();
        total = vertical ? pa->getWidth() + pb->getWidth() : pa->getHeight() + pb->getHeight();
    }
}

void PanelHost::Divider::mouseDrag (const juce::MouseEvent& e)
{
    host.manager.dividerDragged (*this, e.getEventRelativeTo (&host).getPosition(), false);
}

void PanelHost::Divider::mouseUp (const juce::MouseEvent& e)
{
    if (e.mouseWasDraggedSinceMouseDown())
        host.manager.dividerDragged (*this, e.getEventRelativeTo (&host).getPosition(), true);
}

void PanelHost::Divider::mouseDoubleClick (const juce::MouseEvent&)
{
    host.manager.dividerReset (*this);
}

//==============================================================================
struct LayoutManager::Floating : public juce::DocumentWindow
{
    Floating (LayoutManager& m, int index)
        : juce::DocumentWindow ("Hawthorne", Colours::bg, juce::DocumentWindow::allButtons),
          manager (m), host (m, index)
    {
        setUsingNativeTitleBar (true);
        setContentNonOwned (&host, false);
        setResizable (true, false);
        setResizeLimits (200, 120, 10000, 10000);
        tips = std::make_unique<juce::TooltipWindow> (&host, 600);
    }
    ~Floating() override
    {
        if (juce::Desktop::getInstance().getKioskModeComponent() == this)
            juce::Desktop::getInstance().setKioskModeComponent (nullptr, false);
        tips.reset();
        clearContentComponent();
    }
    void closeButtonPressed() override { manager.windowClosed (host.windowIndex); }
    void moved() override   { juce::DocumentWindow::moved(); manager.windowMoved(); }
    void resized() override { juce::DocumentWindow::resized(); manager.windowMoved(); }
    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress (juce::KeyPress::F11Key))
        {
            auto& d = juce::Desktop::getInstance();
            d.setKioskModeComponent (d.getKioskModeComponent() == this ? nullptr : this, false);
            return true;
        }
        if (k == juce::KeyPress (juce::KeyPress::escapeKey) && juce::Desktop::getInstance().getKioskModeComponent() == this)
        {
            juce::Desktop::getInstance().setKioskModeComponent (nullptr, false);
            return true;
        }
        return manager.onKey && manager.onKey (k);
    }

    LayoutManager& manager;
    PanelHost host;
    std::unique_ptr<juce::TooltipWindow> tips;
};

//==============================================================================
LayoutManager::LayoutManager (juce::ApplicationProperties& p) : props (p) {}

LayoutManager::~LayoutManager()
{
    for (auto* p : panels) if (auto* parent = p->getParentComponent()) parent->removeChildComponent (p);
    windows.clear();
}

void LayoutManager::addPanel (Panel& p, int defaultWindow, Edge defaultEdge)
{
    p.manager = this;
    panels.add (&p);
    defaultPlaces.push_back ({ p.id, defaultWindow, defaultEdge });
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

bool LayoutManager::isShown (const Panel& p) const
{
    for (auto& pl : places) if (pl.panel == p.id) return true;
    return false;
}

//==============================================================================
void LayoutManager::defaults()
{
    places = defaultPlaces;
    edgeSizes.clear();
    windowBounds.clear();
}

void LayoutManager::restore()
{
    defaults();
    if (auto xml = props.getUserSettings()->getXmlValue ("layout"))
        fromXml (*xml);
    apply();
}

void LayoutManager::resetToDefault()
{
    defaults();
    apply();
    save();
}

std::unique_ptr<juce::XmlElement> LayoutManager::toXml() const
{
    auto xml = std::make_unique<juce::XmlElement> ("Layout");
    for (int i = 0; i < (int) windows.size(); ++i)
    {
        auto* w = xml->createNewChildElement ("Window");
        w->setAttribute ("bounds", windows[(size_t) i]->getBounds().toString());
    }
    for (auto& pl : places)
    {
        auto* e = xml->createNewChildElement ("Panel");
        e->setAttribute ("id", pl.panel);
        e->setAttribute ("window", pl.window);
        e->setAttribute ("edge", edgeName (pl.edge));
        if (pl.size > 0) e->setAttribute ("size", pl.size);
    }
    for (auto& [w, es] : edgeSizes)
    {
        if (es[0] == 0 && es[1] == 0 && es[2] == 0 && es[3] == 0) continue;
        auto* e = xml->createNewChildElement ("Edges");
        e->setAttribute ("window", w);
        e->setAttribute ("top", es[0]); e->setAttribute ("bottom", es[1]);
        e->setAttribute ("left", es[2]); e->setAttribute ("right", es[3]);
    }
    return xml;
}

void LayoutManager::fromXml (const juce::XmlElement& xml)
{
    std::vector<Place> loaded;
    std::vector<juce::Rectangle<int>> bounds;
    for (auto* w : xml.getChildWithTagNameIterator ("Window"))
        bounds.push_back (juce::Rectangle<int>::fromString (w->getStringAttribute ("bounds")));
    for (auto* e : xml.getChildWithTagNameIterator ("Panel"))
    {
        const auto id = e->getStringAttribute ("id");
        if (find (id) == nullptr) continue;
        bool dup = false;
        for (auto& l : loaded) dup = dup || l.panel == id;
        if (dup) continue;
        const int w = juce::jlimit (0, (int) bounds.size(), e->getIntAttribute ("window", 0));
        loaded.push_back ({ id, w, edgeFrom (e->getStringAttribute ("edge")), juce::jmax (0, e->getIntAttribute ("size", 0)) });
    }
    places = loaded;
    edgeSizes.clear();
    for (auto* e : xml.getChildWithTagNameIterator ("Edges"))
        edgeSizes[e->getIntAttribute ("window")] = { e->getIntAttribute ("top"), e->getIntAttribute ("bottom"),
                                                     e->getIntAttribute ("left"), e->getIntAttribute ("right") };
    windowBounds = bounds;
}

void LayoutManager::save()
{
    if (auto xml = toXml())
        props.getUserSettings()->setValue ("layout", xml.get());
    props.saveIfNeeded();
}

void LayoutManager::windowMoved()
{
    if (! applying) save();
}

//==============================================================================
void LayoutManager::apply()
{
    const juce::ScopedValueSetter<bool> busy (applying, true);

    // windows: one per saved bounds (empty ones are kept: something may be dropped in)
    int needed = (int) windowBounds.size();
    for (auto& pl : places) needed = juce::jmax (needed, pl.window);
    while ((int) windows.size() > needed) windows.pop_back();
    while ((int) windows.size() < needed)
    {
        const int index = (int) windows.size() + 1;
        auto w = std::make_unique<Floating> (*this, index);
        auto b = index - 1 < (int) windowBounds.size() ? windowBounds[(size_t) index - 1] : juce::Rectangle<int>();
        // keep it on a screen that's there now
        bool onScreen = false;
        for (auto& d : juce::Desktop::getInstance().getDisplays().displays)
            onScreen = onScreen || d.userArea.intersects (b.withHeight (40));
        if (b.getWidth() < 200 || ! onScreen)
            b = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea.withSizeKeepingCentre (1000, 640)
                    .translated (40 * index, 40 * index);
        w->setBounds (b);
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
        // title: what's in it
        juce::StringArray names;
        for (auto& pl : places) if (pl.window == w->host.windowIndex) if (auto* p = find (pl.panel)) names.add (p->title);
        w->setName ("Hawthorne - " + (names.isEmpty() ? juce::String ("empty window (drag panels in)") : names.joinIntoString (", ")));
        w->setVisible (floatingVisible);
        layoutHost (w->host);
    }
    layoutHost (main);
    windowBounds.clear();
    for (auto& w : windows) windowBounds.push_back (w->getBounds());
    if (onChanged) onChanged();
}

void LayoutManager::layoutHost (PanelHost& h)
{
    auto r = h.getLocalBounds();
    struct Spec { bool vertical; juce::String a, b; int edge; juce::Rectangle<int> bounds; };
    std::vector<Spec> specs;
    std::vector<Place*> groups[5];        // top, bottom, left, right, centre
    for (auto& pl : places)
        if (pl.window == h.windowIndex)
            if (auto* p = find (pl.panel))
            {
                auto e = pl.edge;
                if (! p->sideOK && (e == Edge::left || e == Edge::right)) e = Edge::centre;
                groups[(int) e].push_back (&pl);
            }
    auto& es = edgeSizes[h.windowIndex];
    const int divT = 6;

    // a stack of panels along one axis: sized ones keep their size, fixed ones their natural
    // size, the rest share; whatever's left over goes to the last flexible one (or the last)
    auto stack = [&] (std::vector<Place*>& ps, juce::Rectangle<int> area, bool across)
    {
        const int len = across ? area.getWidth() : area.getHeight();
        std::vector<int> sz;
        int used = 0, flex = 0;
        for (auto* pl : ps)
        {
            auto* p = find (pl->panel);
            const bool fixed = across ? p->fixedW : p->fixedH;
            const int s = pl->size > 0 ? pl->size : fixed ? (across ? p->prefW : p->prefH) : -1;
            sz.push_back (s);
            if (s < 0) ++flex; else used += s;
        }
        const float scale = used > len && flex == 0 ? (float) len / (float) used : 1.0f;
        const int share = flex > 0 ? juce::jmax (0, len - used) / flex : 0;
        int total = 0, lastFlex = -1;
        for (size_t i = 0; i < sz.size(); ++i)
        {
            if (sz[i] < 0) { sz[i] = share; lastFlex = (int) i; }
            else sz[i] = (int) ((float) sz[i] * scale);
            total += sz[i];
        }
        const int extra = len - total;
        if (extra > 0 && ! sz.empty()) sz[(size_t) (lastFlex >= 0 ? lastFlex : (int) sz.size() - 1)] += extra;
        for (size_t i = 0; i < ps.size(); ++i)
        {
            auto* p = find (ps[i]->panel);
            const auto b = across ? area.removeFromLeft (juce::jmin (sz[i], area.getWidth()))
                                  : area.removeFromTop (juce::jmin (sz[i], area.getHeight()));
            p->setBounds (b);
            if (i + 1 < ps.size())
            {
                // a divider between this panel and the next
                specs.push_back ({ across, ps[i]->panel, ps[i + 1]->panel, -1,
                                   across ? juce::Rectangle<int> (b.getRight() - divT / 2, b.getY(), divT, b.getHeight())
                                          : juce::Rectangle<int> (b.getX(), b.getBottom() - divT / 2, b.getWidth(), divT) });
            }
        }
    };
    auto edgeDivider = [&] (int edge, juce::Rectangle<int> line)
    {
        specs.push_back ({ edge >= 2, {}, {}, edge, line });
    };
    auto thickness = [&] (int edge, std::vector<Place*>& ps, int avail)
    {
        int t = 0;
        for (auto* pl : ps) t = juce::jmax (t, edge < 2 ? find (pl->panel)->prefH : find (pl->panel)->prefW);
        if (es[(size_t) edge] > 0) t = es[(size_t) edge];
        return juce::jlimit (0, avail, t);
    };

    const bool centreEmpty = groups[4].empty();
    if (! groups[1].empty())
    {
        const bool all = centreEmpty && groups[0].empty() && groups[2].empty() && groups[3].empty();
        auto a = r.removeFromBottom (all ? r.getHeight() : thickness (1, groups[1], r.getHeight()));
        stack (groups[1], a, true);
        if (! all) edgeDivider (1, { a.getX(), a.getY() - divT / 2, a.getWidth(), divT });
    }
    if (! groups[0].empty())
    {
        const bool all = centreEmpty && groups[2].empty() && groups[3].empty();
        auto a = r.removeFromTop (all ? r.getHeight() : thickness (0, groups[0], r.getHeight()));
        stack (groups[0], a, true);
        if (! all) edgeDivider (0, { a.getX(), a.getBottom() - divT / 2, a.getWidth(), divT });
    }
    if (! groups[3].empty())
    {
        const bool all = centreEmpty && groups[2].empty();
        auto a = r.removeFromRight (all ? r.getWidth() : thickness (3, groups[3], r.getWidth()));
        stack (groups[3], a, false);
        if (! all) edgeDivider (3, { a.getX() - divT / 2, a.getY(), divT, a.getHeight() });
    }
    if (! groups[2].empty())
    {
        const bool all = centreEmpty;
        auto a = r.removeFromLeft (all ? r.getWidth() : thickness (2, groups[2], r.getWidth()));
        stack (groups[2], a, false);
        if (! all) edgeDivider (2, { a.getRight() - divT / 2, a.getY(), divT, a.getHeight() });
    }
    if (! centreEmpty) stack (groups[4], r, false);

    // the dividers: kept when they're the same ones (one may be being dragged)
    bool same = specs.size() == (size_t) h.dividers.size();
    for (size_t i = 0; same && i < specs.size(); ++i)
    {
        auto* d = h.dividers[(int) i];
        same = d->vertical == specs[i].vertical && d->a == specs[i].a && d->b == specs[i].b && d->edge == specs[i].edge;
    }
    if (! same)
    {
        h.dividers.clear();
        for (auto& sp : specs)
        {
            auto* d = h.dividers.add (new PanelHost::Divider (h, sp.vertical));
            d->a = sp.a; d->b = sp.b; d->edge = sp.edge;
            h.addAndMakeVisible (d);
        }
    }
    for (size_t i = 0; i < specs.size(); ++i)
    {
        h.dividers[(int) i]->setBounds (specs[i].bounds);
        h.dividers[(int) i]->toFront (false);
    }
    h.repaint();
}

void LayoutManager::dividerDragged (PanelHost::Divider& d, juce::Point<int> pos, bool finished)
{
    auto& h = d.host;
    if (d.edge >= 0)
    {
        auto& es = edgeSizes[h.windowIndex];
        const auto b = h.getLocalBounds();
        int v = d.edge == 0 ? pos.y - d.start : d.edge == 1 ? d.start - pos.y : d.edge == 2 ? pos.x - d.start : d.start - pos.x;
        es[(size_t) d.edge] = juce::jlimit (40, (d.edge < 2 ? b.getHeight() : b.getWidth()) - 40, v);
    }
    else
    {
        auto* pa = placeOf (d.a);
        auto* pb = placeOf (d.b);
        if (pa == nullptr || pb == nullptr) return;
        const int v = juce::jlimit (40, juce::jmax (40, d.total - 40), (d.vertical ? pos.x : pos.y) - d.start);
        pa->size = v;
        pb->size = d.total - v;
    }
    layoutHost (h);
    if (finished) save();
}

void LayoutManager::dividerReset (PanelHost::Divider& d)
{
    if (d.edge >= 0) edgeSizes[d.host.windowIndex][(size_t) d.edge] = 0;
    else
    {
        if (auto* pa = placeOf (d.a)) pa->size = 0;
        if (auto* pb = placeOf (d.b)) pb->size = 0;
    }
    layoutHost (d.host);
    save();
}

//==============================================================================
void LayoutManager::setEditing (bool on)
{
    editing = on;
    for (auto* p : panels) p->setEditing (on && isShown (*p));
    if (onChanged) onChanged();
}

void LayoutManager::setShown (Panel& p, bool show)
{
    if (show == isShown (p)) return;
    if (show)
    {
        for (auto& d : defaultPlaces)
            if (d.panel == p.id) places.push_back ({ p.id, 0, d.edge });
    }
    else
        places.erase (std::remove_if (places.begin(), places.end(), [&] (const Place& pl) { return pl.panel == p.id; }), places.end());
    apply();
    save();
}

void LayoutManager::move (Panel& p, int window, Edge edge)
{
    places.erase (std::remove_if (places.begin(), places.end(), [&] (const Place& pl) { return pl.panel == p.id; }), places.end());
    places.push_back ({ p.id, window, edge });
    apply();
    save();
}

int LayoutManager::newWindow (juce::Point<int> screenPos)
{
    auto b = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea.withSizeKeepingCentre (1000, 640);
    if (screenPos != juce::Point<int>())
        b = b.withPosition (screenPos - juce::Point<int> (120, 20));
    windowBounds.push_back (b);
    apply();
    save();
    return (int) windows.size();
}

void LayoutManager::windowClosed (int index)
{
    // its panels go back to where they live by default in the main window
    for (auto& pl : places)
    {
        if (pl.window == index)
        {
            pl.window = 0;
            for (auto& d : defaultPlaces) if (d.panel == pl.panel) pl.edge = d.edge;
        }
        else if (pl.window > index)
            --pl.window;
    }
    windowBounds.clear();
    for (auto& w : windows) if (w->host.windowIndex != index) windowBounds.push_back (w->getBounds());
    std::map<int, std::array<int, 4>> shifted;
    for (auto& [w, es] : edgeSizes) if (w != index) shifted[w > index ? w - 1 : w] = es;
    edgeSizes = shifted;
    juce::MessageManager::callAsync ([this] { windows.clear(); apply(); save(); });
}

void LayoutManager::setFloatingVisible (bool v)
{
    floatingVisible = v;
    for (auto& w : windows) w->setVisible (v);
}

//==============================================================================
void LayoutManager::dropTarget (juce::Point<int> pos, int& window, Edge& edge, juce::Rectangle<int>& zone)
{
    window = -1;
    for (int w = numWindows() - 1; w >= 0; --w)
    {
        auto* h = host (w);
        if (h == nullptr || ! h->isShowing() || ! h->getScreenBounds().contains (pos)) continue;
        window = w;
        const auto local = h->getLocalPoint (nullptr, pos);
        const auto b = h->getLocalBounds();
        const float fx = (float) local.x / (float) juce::jmax (1, b.getWidth());
        const float fy = (float) local.y / (float) juce::jmax (1, b.getHeight());
        const float dl = fx, dr = 1.0f - fx, dt = fy, db = 1.0f - fy;
        const float m = juce::jmin (juce::jmin (dl, dr), juce::jmin (dt, db));
        if (m > 0.22f)          { edge = Edge::centre; zone = b.reduced (b.getWidth() / 4, b.getHeight() / 4); }
        else if (m == db)       { edge = Edge::bottom; zone = b.withTrimmedTop (b.getHeight() * 3 / 4); }
        else if (m == dt)       { edge = Edge::top;    zone = b.withHeight (b.getHeight() / 4); }
        else if (m == dr)       { edge = Edge::right;  zone = b.withTrimmedLeft (b.getWidth() * 3 / 4); }
        else                    { edge = Edge::left;   zone = b.withWidth (b.getWidth() / 4); }
        return;
    }
}

void LayoutManager::dragPanel (Panel& p, juce::Point<int> pos, bool finished)
{
    int window = -1;
    Edge edge = Edge::centre;
    juce::Rectangle<int> zone;
    dropTarget (pos, window, edge, zone);
    if (! p.sideOK && (edge == Edge::left || edge == Edge::right)) edge = Edge::centre;

    for (int w = 0; w < numWindows(); ++w)
        if (auto* h = host (w))
        {
            const auto z = (w == window && ! finished) ? zone : juce::Rectangle<int>();
            if (z != h->dropZone) { h->dropZone = z; h->repaint(); }
        }
    if (! finished) return;

    if (window < 0)
    {
        // dropped outside every window: a new window there
        const int w = newWindow (pos);
        move (p, w, Edge::centre);
    }
    else
        move (p, window, edge);
}

void LayoutManager::showPanelMenu (Panel& p)
{
    auto* pl = placeOf (p.id);
    if (pl == nullptr) return;
    juce::PopupMenu m;
    m.addSectionHeader (p.title.toUpperCase());
    const Edge edges[] = { Edge::top, Edge::bottom, Edge::left, Edge::right, Edge::centre };
    const char* names[] = { "Dock at the top", "Dock at the bottom", "Dock on the left", "Dock on the right", "Centre" };
    for (int i = 0; i < 5; ++i)
        m.addItem (10 + i, names[i], p.sideOK || (edges[i] != Edge::left && edges[i] != Edge::right), pl->edge == edges[i]);
    m.addSeparator();
    juce::PopupMenu to;
    for (int w = 0; w < numWindows(); ++w)
        to.addItem (100 + w, w == 0 ? juce::String ("Main window") : "Window " + juce::String (w + 1), w != pl->window);
    to.addItem (99, "A new window");
    m.addSubMenu ("Move to", to);
    m.addSeparator();
    m.addItem (1, "Hide");
    m.showMenuAsync (juce::PopupMenu::Options(), [this, &p, edges] (int r)
    {
        auto* place = placeOf (p.id);
        if (r <= 0 || place == nullptr) return;
        if (r == 1) setShown (p, false);
        else if (r >= 10 && r < 15) move (p, place->window, edges[r - 10]);
        else if (r == 99) { const int w = newWindow(); move (p, w, Edge::centre); }
        else if (r >= 100) move (p, r - 100, place->edge);
    });
}

//==============================================================================
juce::StringArray LayoutManager::presetNames() const
{
    juce::StringArray names;
    if (auto xml = props.getUserSettings()->getXmlValue ("layoutPresets"))
        for (auto* e : xml->getChildWithTagNameIterator ("Layout"))
            names.add (e->getStringAttribute ("name"));
    return names;
}

void LayoutManager::savePreset (const juce::String& name)
{
    auto all = props.getUserSettings()->getXmlValue ("layoutPresets");
    if (all == nullptr) all = std::make_unique<juce::XmlElement> ("Presets");
    for (auto* e : all->getChildWithTagNameIterator ("Layout"))
        if (e->getStringAttribute ("name") == name) { all->removeChildElement (e, true); break; }
    auto xml = toXml();
    xml->setAttribute ("name", name);
    all->addChildElement (xml.release());
    props.getUserSettings()->setValue ("layoutPresets", all.get());
    props.saveIfNeeded();
}

void LayoutManager::loadPreset (const juce::String& name)
{
    if (auto all = props.getUserSettings()->getXmlValue ("layoutPresets"))
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
    if (auto all = props.getUserSettings()->getXmlValue ("layoutPresets"))
    {
        for (auto* e : all->getChildWithTagNameIterator ("Layout"))
            if (e->getStringAttribute ("name") == name) { all->removeChildElement (e, true); break; }
        props.getUserSettings()->setValue ("layoutPresets", all.get());
        props.saveIfNeeded();
    }
}

} // namespace hawthorne
