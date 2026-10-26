#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "HawthorneLookAndFeel.h"
#include "VUMeter.h"

namespace hawthorne
{

//==============================================================================
/**
    One track on the meter bridge, laid out like a Studer channel module:
    the VU (track name on its plate, double-click to rename), and underneath
    the record-ready (ARM) button and a big track number. Inputs are patched
    in the routing matrix.
*/
class ChannelModule : public juce::Component
{
public:
    explicit ChannelModule (int index);

    VUMeter& getMeter() { return meter; }

    void setTrackName (const juce::String&);
    void setArmed (bool armed, bool recording, bool flashPhase);

    std::function<void (int track, bool arm)>                 onArm;
    std::function<void (int track)>                           onRenameRequest;
    std::function<void (int track)>                           onMenuRequest;     // right-click: import / export
    void mouseUp (const juce::MouseEvent& e) override { if (e.mods.isPopupMenu() && onMenuRequest) onMenuRequest (index); }

    static constexpr float meterAspect = 1.3f;
    static constexpr int   controlsHeight = 30;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    const int index;
    VUMeter meter;
    juce::TextButton armButton { "ARM" };
    bool isArmed = false;
    juce::Rectangle<int> numberArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelModule)
};

} // namespace hawthorne
