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
    the input selector, the record-ready (ARM) button and a big track number.
*/
class ChannelModule : public juce::Component
{
public:
    explicit ChannelModule (int index);

    VUMeter& getMeter() { return meter; }

    void setTrackName (const juce::String&);
    void setInputChoices (const juce::StringArray& channelNames, const juce::BigInteger& active, int selected);
    void setArmed (bool armed, bool recording, bool flashPhase);

    std::function<void (int track, bool arm)>                 onArm;
    std::function<void (int track, int inputChannel)>         onInputChanged;
    std::function<void (int track)>                           onRenameRequest;

    static constexpr float meterAspect = 1.3f;
    static constexpr int   controlsHeight = 30;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    const int index;
    VUMeter meter;
    juce::ComboBox inputBox;
    juce::TextButton armButton { "ARM" };
    bool isArmed = false;
    juce::Rectangle<int> numberArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelModule)
};

} // namespace hawthorne
