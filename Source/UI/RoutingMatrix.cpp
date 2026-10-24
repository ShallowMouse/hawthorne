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

void RoutingMatrix::setNetworkInputs (const std::vector<NetInput>& n)
{
    const bool sameLayout = n.size() == netInputs.size();
    netInputs = n;
    if (! sameLayout) layout();
    repaint();
}

int RoutingMatrix::inputIdOf (const Col& c) const
{
    if (c.kind == ColKind::network)
        return juce::isPositiveAndBelow (c.channel, (int) netInputs.size()) ? netInputs[(size_t) c.channel].id : -1;
    return c.channel;
}

void RoutingMatrix::layout()
{
    const int netW = netInputs.empty() ? 0 : colGap + (int) netInputs.size() * cell;
    const int w = labelW + juce::jmax (1, numIn) * cell + netW + colGap + cell + 4 + juce::jmax (1, numOut) * cell + 12;
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
    const int netStart = inStart + numIn * cell + colGap;
    const int mixX     = netInputs.empty() ? inStart + numIn * cell + colGap
                                           : netStart + (int) netInputs.size() * cell + colGap;
    const int outStart = mixX + cell + 4;

    switch (c.kind)
    {
        case ColKind::input:  return inStart + c.channel * cell;
        case ColKind::network: return netStart + c.channel * cell;
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

    for (int i = 0; i < (int) netInputs.size(); ++i)
        if (x >= colX ({ ColKind::network, i }) && x < colX ({ ColKind::network, i }) + cell)
            return { ColKind::network, i };

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
        case ColKind::network: return juce::isPositiveAndBelow (c.channel, (int) netInputs.size()) && netInputs[(size_t) c.channel].live;
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

    const auto bit = [&] (const ChannelList& l) { return l.contains (c.kind == ColKind::network ? inputIdOf (c) : c.channel); };

    if (row < tracks())
    {
        const auto& tr = s->tracks[(size_t) row];
        switch (c.kind)
        {
            case ColKind::input:  return tr.input == c.channel;
            case ColKind::network: return tr.input == inputIdOf (c);
            case ColKind::mix:    return tr.toMix;
            case ColKind::output: return bit (tr.outputs);
            case ColKind::none:   break;
        }
        return false;
    }

    const int side = row - tracks();
    switch (c.kind)
    {
        case ColKind::input:
        case ColKind::network: return bit (s->master.inputs[(size_t) side]);
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
        case ColKind::network: return juce::isPositiveAndBelow (c.channel, (int) netInputs.size()) ? netInputs[(size_t) c.channel].name : juce::String();
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
    if (! netInputs.empty())
    {
        // one title per sender
        int i = 0;
        while (i < (int) netInputs.size())
        {
            int j = i;
            while (j + 1 < (int) netInputs.size() && netInputs[(size_t) j + 1].slot == netInputs[(size_t) i].slot) ++j;
            const int x0 = colX ({ ColKind::network, i }), x1 = colX ({ ColKind::network, j }) + cell;
            g.setColour (Colours::mustard);
            g.drawText ("NET " + juce::String (netInputs[(size_t) i].slot + 1), x0, 2, juce::jmax (x1 - x0, cell * 3), titleH - 4,
                        juce::Justification::centredLeft, true);
            g.drawHorizontalLine (titleH - 2, (float) x0, (float) x1 - 2.0f);
            i = j + 1;
        }
    }
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
    for (int i = 0; i < (int) netInputs.size(); ++i) drawColHeader ({ ColKind::network, i }, juce::String (netInputs[(size_t) i].channel + 1));
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
            const bool groupShade = c.kind == ColKind::network ? (netInputs[(size_t) c.channel].slot % 2 == 1)
                                                               : (c.kind != ColKind::mix) && ((c.channel / 8) % 2 == 1);
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
        for (int i = 0; i < (int) netInputs.size(); ++i) drawCell ({ ColKind::network, i });
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
        return colName (hoverCol) + (hoverCol.kind == ColKind::network ? " isn't receiving audio - see NETWORK"
                                                                       : " is not enabled - turn it on in Audio Settings");

    const bool isMaster = hoverRow >= tracks();
    if (isMaster && hoverCol.kind == ColKind::mix)
        return {};

    const auto who = isMaster ? rowName (hoverRow) : "Track " + juce::String (hoverRow + 1);
    const bool on = isOn (hoverRow, hoverCol);

    if (hoverCol.kind == ColKind::input || hoverCol.kind == ColKind::network)
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

    const int id = inputIdOf (c);
    const auto flip = [&] (ChannelList l)
    {
        const int v = c.kind == ColKind::network ? id : c.channel;
        if (l.contains (v)) l.removeFirstMatchingValue (v);
        else                { l.add (v); l.sort(); }
        return l;
    };

    if (r < tracks())
    {
        auto& tr = s->tracks[(size_t) r];
        switch (c.kind)
        {
            case ColKind::input:  engine.setInput (r, tr.input == c.channel ? -1 : c.channel); break;
            case ColKind::network: engine.setInput (r, tr.input == id ? -1 : id); break;
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
            case ColKind::input:
            case ColKind::network: engine.setMasterInputs (side, flip (s->master.inputs[(size_t) side])); break;
            case ColKind::output: engine.setMasterOutputs (side, flip (s->master.outputs[(size_t) side])); break;
            case ColKind::mix:
            case ColKind::none:   return;
        }
    }

    repaint();
    if (onChanged) onChanged();
}

//==============================================================================
LatencyPanel::LatencyPanel (TapeEngine& e, std::function<Reel*()> gs) : engine (e), getReel (std::move (gs))
{
    for (auto* c : { &pingOut, &pingIn })
    {
        c->setWantsKeyboardFocus (false);
        addAndMakeVisible (c);
    }
    for (auto* b : { &measureButton, &resetButton })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }

    pingOut.setTooltip ("Output the test click is sent from. Patch it straight back to the input with a cable.");
    pingIn.setTooltip ("Input the cable comes back on");
    measureButton.setTooltip ("Sends a click out and times it coming back. Patch a cable from the output to the input first - the click is audible on that output.");
    resetButton.setTooltip ("Forget the measurement and trust the figure the audio driver reports");

    measureButton.onClick = [this] { startMeasure(); };
    resetButton.onClick   = [this] { engine.setMeasuredRoundTrip (0); if (onMeasured) onMeasured (0); message = "Using the driver's figure."; refresh(); };
}

void LatencyPanel::fill (juce::ComboBox& c, const juce::StringArray& names, const juce::BigInteger& active, const juce::String& none)
{
    const int keep = c.getSelectedId();
    c.clear (juce::dontSendNotification);
    c.addItem (none, 1000);
    for (int ch = 0; ch < names.size(); ++ch)
        if (active[ch])
            c.addItem (juce::String (ch + 1) + "  " + names[ch], ch + 1);
    c.setSelectedId (keep != 0 ? keep : 1000, juce::dontSendNotification);
}

void LatencyPanel::setDevice (const juce::StringArray& inN, const juce::BigInteger& inA,
                              const juce::StringArray& outN, const juce::BigInteger& outA)
{
    fill (pingOut, outN, outA, "-");
    fill (pingIn, inN, inA, "-");
    refresh();
}

void LatencyPanel::refresh()
{
    repaint();
}

static int selectedChannel (const juce::ComboBox& c)
{
    const int id = c.getSelectedId();
    return id > 0 && id != 1000 ? id - 1 : -1;
}

void LatencyPanel::startMeasure()
{
    const int out = selectedChannel (pingOut), in = selectedChannel (pingIn);

    if (out < 0 || in < 0)
    {
        message = "Choose an output and an input, and patch a cable between them.";
        repaint();
        return;
    }
    if (engine.isRecording())
    {
        message = "Stop recording first.";
        repaint();
        return;
    }

    results.clear();
    runsLeft = 3;
    if (! engine.startPing (out, in))
    {
        message = "The audio device isn't running.";
        repaint();
        return;
    }
    message = "Measuring...";
    repaint();
    startTimer (20);
}

void LatencyPanel::timerCallback()
{
    const auto state = engine.getPingState();
    if (state == TapeEngine::PingState::running)
        return;

    if (state == TapeEngine::PingState::failed)
    {
        stopTimer();
        message = "Nothing came back. Check the cable and the levels.";
        repaint();
        return;
    }

    results.add (engine.getPingResult());

    if (--runsLeft > 0)
    {
        engine.startPing (selectedChannel (pingOut), selectedChannel (pingIn));
        return;
    }

    stopTimer();
    int lo = results[0], hi = results[0], sum = 0;
    for (auto v : results) { lo = juce::jmin (lo, v); hi = juce::jmax (hi, v); sum += v; }
    const int value = juce::roundToInt ((double) sum / results.size());

    if (hi - lo > 2)
    {
        message = "Readings disagree (" + juce::String (lo) + " - " + juce::String (hi) + " samples). Not saved - try again.";
        repaint();
        return;
    }

    engine.setMeasuredRoundTrip (value);
    if (onMeasured) onMeasured (value);

    message = "Measured " + juce::String (value) + " samples, 3 readings agree. Saved for this audio device.";
    refresh();
}

void LatencyPanel::paint (juce::Graphics& g)
{
    g.fillAll (Colours::panel);
    g.setColour (Colours::edge);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());

    const double sr = juce::jmax (1.0, engine.getDeviceSampleRate());
    auto fmt = [sr] (int smp) { return juce::String (smp) + " smp  (" + juce::String ((double) smp / sr * 1000.0, 2) + " ms)"; };

    g.setFont (monoFont (12.0f, true));
    g.setColour (Colours::mustard);
    g.drawText ("ROUND TRIP", 12, 8, 120, 26, juce::Justification::centredLeft);

    g.setFont (monoFont (12.0f));
    g.setColour (Colours::text);
    const bool measured = engine.getRoundTrip() != engine.getReportedRoundTrip();
    g.drawText ("driver " + fmt (engine.getReportedRoundTrip()) + "     using " + fmt (engine.getRoundTrip())
                    + (measured ? "  - measured" : ""),
                rttText, juce::Justification::centredLeft, true);

    g.setFont (monoFont (10.0f, true));
    g.setColour (Colours::textDim);
    g.drawText ("OUT", pingOut.getBounds().translated (-32, 0).withWidth (30), juce::Justification::centredLeft);
    g.drawText ("IN",  pingIn.getBounds().translated (-24, 0).withWidth (22), juce::Justification::centredLeft);

    g.setFont (monoFont (12.0f));
    juce::String info = message;
    if (info.isEmpty())
        info = engine.getMasterBusLatency() > 0
                 ? "Inputs are patched to the master, so the internal mix waits " + fmt (engine.getMasterBusLatency()) + " to meet them."
                 : juce::String ("Everything recorded is shifted back by this figure so it lands in step with the tape.");
    g.drawText (info, messageArea, juce::Justification::centredLeft, true);
}

void LatencyPanel::resized()
{
    auto r = getLocalBounds().reduced (12, 8);
    auto row1 = r.removeFromTop (26);
    row1.removeFromLeft (120);
    row1.removeFromLeft (32); pingOut.setBounds (row1.removeFromLeft (150)); row1.removeFromLeft (6);
    row1.removeFromLeft (24); pingIn.setBounds (row1.removeFromLeft (150));  row1.removeFromLeft (8);
    measureButton.setBounds (row1.removeFromLeft (90)); row1.removeFromLeft (6);
    resetButton.setBounds (row1.removeFromLeft (100)); row1.removeFromLeft (14);
    rttText = row1;

    r.removeFromTop (8);
    messageArea = r.removeFromTop (22);
}

//==============================================================================
RoutingWindow::RoutingWindow (TapeEngine& e, std::function<Reel*()> gs)
    : juce::DocumentWindow ("Hawthorne - Routing", Colours::panel, juce::DocumentWindow::closeButton),
      content (e, gs)
{
    setUsingNativeTitleBar (true);
    matrix = new RoutingMatrix (e, std::move (gs));
    content.viewport.setViewedComponent (matrix, true);
    content.viewport.setScrollBarsShown (true, true);
    setContentNonOwned (&content, false);
    setResizable (true, false);
    setResizeLimits (900, 400, 4000, 3000);

    fitToMatrix();
}

void RoutingWindow::fitToMatrix()
{
    auto area = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() != nullptr
              ? juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea
              : juce::Rectangle<int> (0, 0, 1400, 900);
    const int sb = content.viewport.getScrollBarThickness();
    centreWithSize (juce::jmin (area.getWidth() - 80, juce::jmax (1400, matrix->getWidth() + sb + 2)),
                    juce::jmin (matrix->getHeight() + sb + 2 + LatencyPanel::preferredHeight, area.getHeight() - 80));
}

} // namespace hawthorne
