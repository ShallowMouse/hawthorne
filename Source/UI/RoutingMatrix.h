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
    RoutingMatrix (TapeEngine&, std::function<Reel*()> getReel);

    void setDevice (const juce::StringArray& inputNames, const juce::BigInteger& activeInputs,
                    const juce::StringArray& outputNames, const juce::BigInteger& activeOutputs);

    /** Network inputs (Hawthorne Link) shown after the device inputs. */
    struct NetInput { int id; juce::String name; int slot; int channel; bool live; };
    void setNetworkInputs (const std::vector<NetInput>&);

    std::function<void()> onChanged;

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    juce::String getTooltip() override;

private:
    enum class ColKind { none, input, network, mix, output };
    struct Col { ColKind kind = ColKind::none; int channel = -1; };

    static constexpr int cell = 22, labelW = 150, titleH = 22, numbersH = 30, rowGap = 10, colGap = 12;
    int tracks() const { auto* s = getReel(); return s != nullptr ? s->numTracks : kDefaultTracks; }
    int numRows() const { return tracks() + 4; }     // tracks, master L/R, click, LTC out
    bool isOutputOnly (int row) const { return row >= tracks() + 2; }

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
    std::function<Reel*()> getReel;
    std::vector<NetInput> netInputs;
    int inputIdOf (const Col&) const;
    juce::StringArray inNames, outNames;
    juce::BigInteger inActive, outActive;
    int numIn = 0, numOut = 0;
    int hoverRow = -1;
    Col hoverCol;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RoutingMatrix)
};

//==============================================================================
/** Latency strip above the matrix: the interface round trip (driver figure,
    or measured with a loopback cable) that every recording is shifted by. */
class LatencyPanel : public juce::Component,
                     private juce::Timer
{
public:
    LatencyPanel (TapeEngine&, std::function<Reel*()> getReel);

    void setDevice (const juce::StringArray& inputNames, const juce::BigInteger& activeInputs,
                    const juce::StringArray& outputNames, const juce::BigInteger& activeOutputs);
    void refresh();

    /** A measurement finished (0 = measurement cleared, use the driver's figure). */
    std::function<void (int)> onMeasured;

    static constexpr int preferredHeight = 76;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void startMeasure();
    static void fill (juce::ComboBox&, const juce::StringArray& names, const juce::BigInteger& active, const juce::String& none);

    TapeEngine& engine;
    std::function<Reel*()> getReel;

    juce::ComboBox pingOut, pingIn;
    juce::TextButton measureButton { "MEASURE" }, resetButton { "USE DRIVER" }, routeAllButton { "PATCH ALL TO ROUTE" };
public:
    std::function<void()> onPatched;
private:

    int runsLeft = 0;
    juce::Array<int> results;
    juce::String message;
    juce::Rectangle<int> rttText, messageArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LatencyPanel)
};

//==============================================================================
class RoutingWindow : public juce::DocumentWindow
{
public:
    RoutingWindow (TapeEngine&, std::function<Reel*()> getReel);
    ~RoutingWindow() override { clearContentComponent(); }

    RoutingMatrix& getMatrix() { return *matrix; }
    LatencyPanel& getLatencyPanel() { return content.latency; }
    void fitToMatrix();
    void closeButtonPressed() override { setVisible (false); }

private:
    struct Content : juce::Component
    {
        Content (TapeEngine& e, std::function<Reel*()> gs) : latency (e, gs)
        {
            addAndMakeVisible (latency);
            addAndMakeVisible (viewport);
        }
        void resized() override
        {
            auto r = getLocalBounds();
            latency.setBounds (r.removeFromTop (LatencyPanel::preferredHeight));
            viewport.setBounds (r);
        }
        LatencyPanel latency;
        juce::Viewport viewport;
    };

    Content content;
    RoutingMatrix* matrix = nullptr;
};

} // namespace hawthorne
