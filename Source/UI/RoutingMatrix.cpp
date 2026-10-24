#include "RoutingMatrix.h"

namespace hawthorne
{

RoutingMatrix::RoutingMatrix (TapeEngine& e, std::function<Reel*()> gs)
    : engine (e), getReel (std::move (gs))
{
    layout();
}

void RoutingMatrix::setDevice (const juce::StringArray& in, const juce::BigInteger& inA,
                               const juce::StringArray& out, const juce::BigInteger& outA)
{
    inNames = in;   inActive = inA;
    outNames = out; outActive = outA;
    numIn  = in.size();
    numOut = out.size();
    layout();
    repaint();
}

void RoutingMatrix::layout()
{
    const int w = labelW + juce::jmax (1, numIn) * cell + colGap + cell + 4 + juce::jmax (1, numOut) * cell + 12;
    const int h = titleH + numbersH + tracks() * cell + rowGap + 2 * cell + 12;
    setSize (w, h);
}

//==============================================================================
int RoutingMatrix::rowY (int row) const
{
    return titleH + numbersH + row * cell + (row >= tracks() ? rowGap : 0);
}

int RoutingMatrix::colX (const Col& c) const
{
    const int inStart  = labelW;
    const int mixX     = inStart + numIn * cell + colGap;
    const int outStart = mixX + cell + 4;

    switch (c.kind)
    {
        case ColKind::input:  return inStart + c.channel * cell;
        case ColKind::mix:    return mixX;
        case ColKind::output: return outStart + c.channel * cell;
        case ColKind::none:   break;
    }
    return -1000;
}

int RoutingMatrix::rowAt (int y) const
{
    for (int r = 0; r < numRows(); ++r)
        if (y >= rowY (r) && y < rowY (r) + cell)
            return r;
    return -1;
}

RoutingMatrix::Col RoutingMatrix::colAt (int x) const
{
    for (int i = 0; i < numIn; ++i)
        if (x >= colX ({ ColKind::input, i }) && x < colX ({ ColKind::input, i }) + cell)
            return { ColKind::input, i };

    if (x >= colX ({ ColKind::mix, 0 }) && x < colX ({ ColKind::mix, 0 }) + cell)
        return { ColKind::mix, 0 };

    for (int i = 0; i < numOut; ++i)
        if (x >= colX ({ ColKind::output, i }) && x < colX ({ ColKind::output, i }) + cell)
            return { ColKind::output, i };

    return {};
}

bool RoutingMatrix::isEnabled (const Col& c) const
{
    switch (c.kind)
    {
        case ColKind::input:  return inActive[c.channel];
        case ColKind::output: return outActive[c.channel];
        case ColKind::mix:    return true;
        case ColKind::none:   break;
    }
    return false;
}

bool RoutingMatrix::isOn (int row, const Col& c) const
{
    auto* s = getReel();
    if (s == nullptr || row < 0)
        return false;

    const auto bit = [&] (const ChannelList& l) { return l.contains (c.channel); };

    if (row < tracks())
    {
        const auto& tr = s->tracks[(size_t) row];
        switch (c.kind)
        {
            case ColKind::input:  return tr.input == c.channel;
            case ColKind::mix:    return tr.toMix;
            case ColKind::output: return bit (tr.outputs);
            case ColKind::none:   break;
        }
        return false;
    }

    const int side = row - tracks();
    switch (c.kind)
    {
        case ColKind::input:  return bit (s->master.inputs[(size_t) side]);
        case ColKind::output: return bit (s->master.outputs[(size_t) side]);
        case ColKind::mix:
        case ColKind::none:   break;
    }
    return false;
}

juce::String RoutingMatrix::rowName (int row) const
{
    if (row >= tracks())
        return row == tracks() ? "MASTER L" : "MASTER R";

    auto* s = getReel();
    return juce::String (row + 1).paddedLeft ('0', 2) + "  "
         + (s != nullptr ? s->tracks[(size_t) row].name : juce::String ("Track " + juce::String (row + 1)));
}

juce::String RoutingMatrix::colName (const Col& c) const
{
    switch (c.kind)
    {
        case ColKind::input:  return "In " + juce::String (c.channel + 1) + (inNames[c.channel].isNotEmpty() ? " (" + inNames[c.channel] + ")" : "");
        case ColKind::output: return "Out " + juce::String (c.channel + 1) + (outNames[c.channel].isNotEmpty() ? " (" + outNames[c.channel] + ")" : "");
        case ColKind::mix:    return "the master mix";
        case ColKind::none:   break;
    }
    return {};
}

//==============================================================================
void RoutingMatrix::paint (juce::Graphics& g)
{
    g.fillAll (Colours::panel);

    const int gridTop = titleH + numbersH;
    const int inX = colX ({ ColKind::input, 0 }), mixX = colX ({ ColKind::mix, 0 }), outX = colX ({ ColKind::output, 0 });

    // Section titles
    g.setFont (monoFont (13.0f, true));
    g.setColour (Colours::mustard);
    g.drawText ("INPUTS", inX, 2, juce::jmax (cell * 3, numIn * cell), titleH - 4, juce::Justification::centredLeft);
    g.drawText ("OUTPUTS", mixX, 2, (outX - mixX) + juce::jmax (cell * 3, numOut * cell), titleH - 4, juce::Justification::centredLeft);
    g.drawHorizontalLine (titleH - 2, (float) inX, (float) (inX + numIn * cell));
    g.drawHorizontalLine (titleH - 2, (float) mixX, (float) (outX + numOut * cell));

    // Column numbers
    g.setFont (monoFont (11.0f, true));
    auto drawColHeader = [&] (const Col& c, const juce::String& text)
    {
        const bool hot = hoverCol.kind == c.kind && hoverCol.channel == c.channel;
        g.setColour (hot ? Colours::mustard : (isEnabled (c) ? Colours::text : Colours::textDim.withAlpha (0.5f)));
        g.drawText (text, colX (c), titleH, cell, numbersH - 4, juce::Justification::centredBottom);
    };
    for (int i = 0; i < numIn; ++i)  drawColHeader ({ ColKind::input, i }, juce::String (i + 1));
    drawColHeader ({ ColKind::mix, 0 }, "MIX");
    for (int i = 0; i < numOut; ++i) drawColHeader ({ ColKind::output, i }, juce::String (i + 1));

    if (numIn == 0 || numOut == 0)
    {
        g.setColour (Colours::red);
        g.setFont (uiFont (13.0f));
        g.drawText ("No audio device - open Audio Settings", labelW, gridTop - 18, 400, 16, juce::Justification::centredLeft);
    }

    // Rows
    for (int r = 0; r < numRows(); ++r)
    {
        const int y = rowY (r);
        const bool isMaster = r >= tracks();
        const bool hotRow = r == hoverRow;

        g.setFont (isMaster ? monoFont (12.0f, true) : uiFont (12.0f, hotRow));
        g.setColour (isMaster ? Colours::mustard : (hotRow ? Colours::mustard : Colours::text));
        g.drawText (rowName (r), 8, y, labelW - 14, cell, juce::Justification::centredLeft, true);

        auto drawCell = [&] (const Col& c)
        {
            if (isMaster && c.kind == ColKind::mix)
                return;   // the master doesn't feed itself

            auto rc = juce::Rectangle<float> ((float) colX (c), (float) y, (float) cell, (float) cell).reduced (1.5f);
            const bool hot = hotRow || (hoverCol.kind == c.kind && hoverCol.channel == c.channel);
            const bool groupShade = (c.kind != ColKind::mix) && ((c.channel / 8) % 2 == 1);
            const bool enabled = isEnabled (c);

            g.setColour (Colours::black.brighter (groupShade ? 0.06f : 0.0f).brighter (hot ? 0.12f : 0.0f));
            g.fillRoundedRectangle (rc, 2.0f);

            if (isOn (r, c))
            {
                g.setColour (enabled ? Colours::mustard : Colours::red);
                g.fillEllipse (rc.reduced (4.0f));
            }
            else if (! enabled)
            {
                g.setColour (Colours::edge.withAlpha (0.6f));
                g.drawLine (rc.getX() + 4, rc.getBottom() - 4, rc.getRight() - 4, rc.getY() + 4, 1.0f);
            }
        };

        for (int i = 0; i < numIn; ++i)  drawCell ({ ColKind::input, i });
        drawCell ({ ColKind::mix, 0 });
        for (int i = 0; i < numOut; ++i) drawCell ({ ColKind::output, i });
    }

    // Divider above the master rows
    g.setColour (Colours::mustardDim);
    g.drawHorizontalLine (rowY (tracks()) - rowGap / 2, 8.0f, (float) getWidth() - 8.0f);
}

//==============================================================================
void RoutingMatrix::mouseMove (const juce::MouseEvent& e)
{
    const int r = rowAt (e.y);
    const auto c = colAt (e.x);
    if (r != hoverRow || c.kind != hoverCol.kind || c.channel != hoverCol.channel)
    {
        hoverRow = r;
        hoverCol = c;
        repaint();
    }
}

void RoutingMatrix::mouseExit (const juce::MouseEvent&)
{
    hoverRow = -1;
    hoverCol = {};
    repaint();
}

juce::String RoutingMatrix::getTooltip()
{
    if (hoverRow < 0 || hoverCol.kind == ColKind::none)
        return {};

    if (! isEnabled (hoverCol))
        return colName (hoverCol) + " is not enabled - turn it on in Audio Settings";

    const bool isMaster = hoverRow >= tracks();
    if (isMaster && hoverCol.kind == ColKind::mix)
        return {};

    const auto who = isMaster ? rowName (hoverRow) : "Track " + juce::String (hoverRow + 1);
    const bool on = isOn (hoverRow, hoverCol);

    if (hoverCol.kind == ColKind::input)
        return who + (on ? " is fed from " : " <- click to feed from ") + colName (hoverCol);

    return who + (on ? " goes to " : " -> click to send to ") + colName (hoverCol);
}

void RoutingMatrix::mouseDown (const juce::MouseEvent& e)
{
    auto* s = getReel();
    const int r = rowAt (e.y);
    const auto c = colAt (e.x);

    if (s == nullptr || r < 0 || c.kind == ColKind::none || ! isEnabled (c))
        return;

    const auto flip = [&] (ChannelList l)
    {
        if (l.contains (c.channel)) l.removeFirstMatchingValue (c.channel);
        else                        { l.add (c.channel); l.sort(); }
        return l;
    };

    if (r < tracks())
    {
        auto& tr = s->tracks[(size_t) r];
        switch (c.kind)
        {
            case ColKind::input:  engine.setInput (r, tr.input == c.channel ? -1 : c.channel); break;
            case ColKind::mix:    engine.setTrackToMix (r, ! tr.toMix); break;
            case ColKind::output: engine.setTrackOutputs (r, flip (tr.outputs)); break;
            case ColKind::none:   break;
        }
    }
    else
    {
        const int side = r - tracks();
        switch (c.kind)
        {
            case ColKind::input:  engine.setMasterInputs (side, flip (s->master.inputs[(size_t) side])); break;
            case ColKind::output: engine.setMasterOutputs (side, flip (s->master.outputs[(size_t) side])); break;
            case ColKind::mix:
            case ColKind::none:   return;
        }
    }

    repaint();
    if (onChanged) onChanged();
}

//==============================================================================
RoutingWindow::RoutingWindow (TapeEngine& e, std::function<Reel*()> gs)
    : juce::DocumentWindow ("Hawthorne - Routing", Colours::panel, juce::DocumentWindow::closeButton)
{
    setUsingNativeTitleBar (true);
    matrix = new RoutingMatrix (e, std::move (gs));
    viewport.setViewedComponent (matrix, true);
    viewport.setScrollBarsShown (true, true);
    setContentNonOwned (&viewport, false);
    setResizable (true, false);
    setResizeLimits (400, 300, 4000, 3000);

    fitToMatrix();
}

void RoutingWindow::fitToMatrix()
{
    auto area = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() != nullptr
              ? juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea
              : juce::Rectangle<int> (0, 0, 1400, 900);
    const int sb = viewport.getScrollBarThickness();
    centreWithSize (juce::jmin (matrix->getWidth() + sb + 2, area.getWidth() - 80),
                    juce::jmin (matrix->getHeight() + sb + 2, area.getHeight() - 80));
}

} // namespace hawthorne
