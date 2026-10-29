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
    /** Noise reduction strip under the meter: the reel's system (0 = none: no strip), this
        track's switch (nr::Mode) and its lamp (0 off, 1 encoding, 2 decoding). */
    void setNr (int system, int mode, int lamp);

    std::function<void (int track, bool arm)>                 onArm;
    std::function<void (int track)>                           onRenameRequest;
    std::function<void (int track)>                           onMenuRequest;     // right-click: import / export
    std::function<void (int track, int mode)>                 onNrMode;
    void mouseUp (const juce::MouseEvent& e) override { if (e.mods.isPopupMenu() && onMenuRequest) onMenuRequest (index); }
    void mouseDown (const juce::MouseEvent&) override;

    static constexpr float meterAspect = 1.3f;
    static constexpr int   controlsHeight = 30;
    static constexpr int   nrHeight = 28;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    const int index;
    VUMeter meter;
    juce::TextButton armButton { "ARM" };
    bool isArmed = false;
    juce::Rectangle<int> numberArea, nrArea, stripArea;
    int nrSystem = 0, nrMode = 0, nrLamp = 0;
    juce::Rectangle<float> segment (int mode) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelModule)
};

} // namespace hawthorne
