#include "ChannelModule.h"

namespace hawthorne
{

ChannelModule::ChannelModule (int i) : index (i), meter ("Track " + juce::String (i + 1))
{
    meter.setTooltip ("Double-click to rename");
    meter.onDoubleClick = [this] { if (onRenameRequest) onRenameRequest (index); };
    meter.onRightClick  = [this] { if (onMenuRequest) onMenuRequest (index); };
    addAndMakeVisible (meter);

    armButton.setClickingTogglesState (false);
    armButton.setWantsKeyboardFocus (false);
    armButton.setColour (juce::TextButton::buttonOnColourId, Colours::red);
    armButton.setColour (juce::TextButton::textColourOnId, Colours::text);
    armButton.setTooltip ("Record ready (arm)");
    armButton.onClick = [this] { if (onArm) onArm (index, ! isArmed); };
    addAndMakeVisible (armButton);
}

void ChannelModule::setTrackName (const juce::String& n)
{
    meter.setLabel (n);
}

void ChannelModule::setArmed (bool armed, bool recording, bool flashPhase)
{
    if (armed != isArmed) { isArmed = armed; repaint(); }
    armButton.setToggleState (armed && (recording || flashPhase), juce::dontSendNotification);
    meter.setRecLamp (! armed ? VUMeter::RecLamp::off
                              : (recording ? VUMeter::RecLamp::recording : VUMeter::RecLamp::armed), flashPhase);
}

void ChannelModule::paint (juce::Graphics& g)
{
    // control strip under the meter
    auto strip = getLocalBounds().removeFromBottom (controlsHeight).toFloat().reduced (1.0f, 1.0f);
    g.setColour (isArmed ? Colours::redDim : Colours::black.brighter (0.05f));
    g.fillRoundedRectangle (strip, 3.0f);

    g.setColour (Colours::text);
    g.setFont (uiFont ((float) numberArea.getHeight() * 0.95f, true));
    g.drawText (juce::String (index + 1), numberArea, juce::Justification::centredRight);
}

void ChannelModule::resized()
{
    auto r = getLocalBounds();
    auto controls = r.removeFromBottom (controlsHeight).reduced (4, 3);
    meter.setBounds (r.reduced (0, 2));

    numberArea = controls.removeFromRight (juce::jmax (30, controls.getWidth() / 4));
    armButton.setBounds (controls.removeFromLeft (juce::jmin (controls.getWidth() - 8, 90)));
}

} // namespace hawthorne
