#include "ChannelModule.h"

namespace hawthorne
{

ChannelModule::ChannelModule (int i) : index (i), meter ("Track " + juce::String (i + 1))
{
    meter.setTooltip ("Double-click to rename");
    meter.onDoubleClick = [this] { if (onRenameRequest) onRenameRequest (index); };
    addAndMakeVisible (meter);

    inputBox.setTooltip ("Input");
    inputBox.setWantsKeyboardFocus (false);
    inputBox.onChange = [this]
    {
        const int id = inputBox.getSelectedId();
        if (id > 0 && onInputChanged)
            onInputChanged (index, id - 2); // id 1 = none (-1)
    };
    addAndMakeVisible (inputBox);

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

void ChannelModule::setInputChoices (const juce::StringArray& names, const juce::BigInteger& active, int selected)
{
    inputBox.clear (juce::dontSendNotification);
    inputBox.addItem ("--", 1);

    for (int ch = 0; ch < names.size(); ++ch)
    {
        inputBox.addItem ("In " + juce::String (ch + 1), ch + 2);
        inputBox.setItemEnabled (ch + 2, active[ch]);
    }

    if (selected >= names.size())
        inputBox.addItem ("In " + juce::String (selected + 1) + "?", selected + 2);

    inputBox.setSelectedId (selected + 2, juce::dontSendNotification);

    const bool ok = selected >= 0 && selected < names.size() && active[selected];
    inputBox.setColour (juce::ComboBox::textColourId, ok ? Colours::text : Colours::red);
    inputBox.setTooltip (ok ? names[selected] : (selected < 0 ? "No input" : "Input not active - enable it in Audio Settings"));
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
    const int w = controls.getWidth();
    inputBox.setBounds (controls.removeFromLeft (w / 2 - 2));
    controls.removeFromLeft (4);
    armButton.setBounds (controls.removeFromLeft (juce::jmin (controls.getWidth(), 60)));
}

} // namespace hawthorne
