#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Engine/TapeEngine.h"
#include "HawthorneLookAndFeel.h"

namespace hawthorne
{

//==============================================================================
/**
    Patch matrix. Rows: tracks 1-24, then Master L / Master R.
    Columns: device INPUTS | MIX + device OUTPUTS.

      Track row  : one input (click to move it), MIX on/off, any number of direct outs.
      Master row : any number of inputs summed into the bus, any number of outputs.
*/
class RoutingMatrix : public juce::Component,
                      public juce::TooltipClient
{
public:
    RoutingMatrix (TapeEngine&, std::function<Session*()> getSession);

    void setDevice (const juce::StringArray& inputNames, const juce::BigInteger& activeInputs,
                    const juce::StringArray& outputNames, const juce::BigInteger& activeOutputs);

    std::function<void()> onChanged;

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    juce::String getTooltip() override;

private:
    enum class ColKind { none, input, mix, output };
    struct Col { ColKind kind = ColKind::none; int channel = -1; };

    static constexpr int cell = 22, labelW = 150, titleH = 22, numbersH = 30, rowGap = 10, colGap = 12;
    int tracks() const { auto* s = getSession(); return s != nullptr ? s->numTracks : kDefaultTracks; }
    int numRows() const { return tracks() + 2; }

public:
    /** Call when the track count changes. */
    void tracksChanged() { layout(); repaint(); }

private:

    void layout();
    int rowAt (int y) const;
    Col colAt (int x) const;
    int rowY (int row) const;
    int colX (const Col&) const;
    bool isOn (int row, const Col&) const;
    bool isEnabled (const Col&) const;
    juce::String rowName (int row) const;
    juce::String colName (const Col&) const;

    TapeEngine& engine;
    std::function<Session*()> getSession;
    juce::StringArray inNames, outNames;
    juce::BigInteger inActive, outActive;
    int numIn = 0, numOut = 0;
    int hoverRow = -1;
    Col hoverCol;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RoutingMatrix)
};

//==============================================================================
class RoutingWindow : public juce::DocumentWindow
{
public:
    RoutingWindow (TapeEngine&, std::function<Session*()> getSession);
    ~RoutingWindow() override { clearContentComponent(); }

    RoutingMatrix& getMatrix() { return *matrix; }
    void fitToMatrix();
    void closeButtonPressed() override { setVisible (false); }

private:
    juce::Viewport viewport;
    RoutingMatrix* matrix = nullptr;
};

} // namespace hawthorne
