#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Network/NetReceiver.h"
#include "HawthorneLookAndFeel.h"

namespace hawthorne
{

//==============================================================================
/**
    Hawthorne Link: senders found on the network, connect / disconnect, and
    how each connection is doing (locked bit-perfect or drift corrected,
    buffer, dropouts).
*/
class NetworkPanel : public juce::Component,
                     private juce::Timer
{
public:
    explicit NetworkPanel (NetReceiver&);

    /** Connections / settings changed: save them and update the routing matrix. */
    std::function<void()> onChanged;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void rebuildRows();
    void addByAddress();

    struct Row
    {
        juce::String name, detail;
        bool connected = false, seen = false;
        int slot = -1;
        std::unique_ptr<juce::TextButton> button;
    };

    NetReceiver& rx;
    std::vector<Row> rows;
    juce::TextButton addButton { "ADD BY ADDRESS..." };
    juce::Slider latency { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Rectangle<int> listArea, statusArea, helpArea;
    juce::String rowsKey;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NetworkPanel)
};

//==============================================================================
class NetworkWindow : public juce::DocumentWindow
{
public:
    explicit NetworkWindow (NetReceiver& rx)
        : juce::DocumentWindow ("Hawthorne - Network (Hawthorne Link)", Colours::panel, juce::DocumentWindow::closeButton),
          panel (rx)
    {
        setUsingNativeTitleBar (true);
        setContentNonOwned (&panel, false);
        setResizable (true, false);
        setResizeLimits (700, 420, 2000, 1400);
        centreWithSize (900, 560);
    }

    ~NetworkWindow() override { clearContentComponent(); }
    NetworkPanel& getPanel() { return panel; }
    void closeButtonPressed() override { setVisible (false); }

private:
    NetworkPanel panel;
};

} // namespace hawthorne
