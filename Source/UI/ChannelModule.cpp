#include "ChannelModule.h"
#include "../Engine/NoiseReduction.h"

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

void ChannelModule::setNr (int system, int mode, int lamp)
{
    if (system == nrSystem && mode == nrMode && lamp == nrLamp) return;
    const bool relayout = (system != 0) != (nrSystem != 0);
    nrSystem = system; nrMode = mode; nrLamp = lamp;
    if (relayout) { resized(); repaint(); }
    else repaint (stripArea);       // (the whole strip: also clears it when NR is switched off)
}

juce::Rectangle<float> ChannelModule::segment (int mode) const
{
    auto r = nrArea.toFloat().reduced (5.0f, 5.0f);
    const float segW = juce::jmin (32.0f, (r.getWidth() * 0.62f) / 4.0f);
    auto seg = r.removeFromRight (segW * 4.0f);
    return seg.withWidth (segW).translated (segW * (float) mode, 0.0f);
}

void ChannelModule::mouseDown (const juce::MouseEvent& e)
{
    if (nrSystem == 0 || e.mods.isPopupMenu() || ! nrArea.contains (e.getPosition())) return;
    for (int m = 0; m < nr::numModes; ++m)
        if (segment (m).expanded (1.0f, 4.0f).contains (e.position))
        {
            if (onNrMode) onNrMode (index, m);
            return;
        }
}

void ChannelModule::paint (juce::Graphics& g)
{
    // noise reduction strip: the NR card for this track
    if (nrSystem != 0)
    {
        const bool dbx = nrSystem == nr::dbx;
        auto r = nrArea.toFloat().reduced (1.0f, 2.0f);
        const auto top = dbx ? juce::Colour (0xffcfccc3) : nrSystem == nr::dolbySR ? juce::Colour (0xff2c3036) : juce::Colour (0xff343434);
        const auto bot = dbx ? juce::Colour (0xffa9a59b) : nrSystem == nr::dolbySR ? juce::Colour (0xff1a1c20) : juce::Colour (0xff1e1e1e);
        g.setGradientFill (juce::ColourGradient (top, 0.0f, r.getY(), bot, 0.0f, r.getBottom(), false));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (dbx ? juce::Colour (0xff555555) : juce::Colours::black);
        g.drawRoundedRectangle (r, 3.0f, 1.0f);

        auto in = r.reduced (5.0f, 5.0f);
        const float h = in.getHeight();
        // the maker's mark
        if (dbx)
        {
            g.setColour (juce::Colour (0xffb0201a));
            g.setFont (juce::Font (juce::FontOptions ("Arial", h * 0.95f, juce::Font::bold | juce::Font::italic)));
            g.drawText ("dbx", in.removeFromLeft (h * 1.7f), juce::Justification::centredLeft, false);
        }
        else
        {
            const juce::String mark = nrSystem == nr::dolbySR ? "DD SR" : "DD";
            g.setFont (juce::Font (juce::FontOptions ("Arial", h * 0.62f, juce::Font::bold)));
            const float mw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), mark) + 7.0f;
            auto box = in.removeFromLeft (mw).withSizeKeepingCentre (mw, h * 0.86f);
            g.setColour (juce::Colour (0xffdddddd));
            g.drawRoundedRectangle (box, 2.5f, 1.4f);
            g.drawText (mark, box, juce::Justification::centred, false);
            if (nrSystem == nr::dolbyA)
            {
                in.removeFromLeft (3.0f);
                g.setColour (juce::Colour (0xffbbbbbb));
                g.drawText ("A", in.removeFromLeft (h * 0.6f), juce::Justification::centredLeft, false);
            }
        }
        // the lamp: green encoding, amber decoding
        in.removeFromLeft (4.0f);
        auto lamp = in.removeFromLeft (h).withSizeKeepingCentre (h * 0.62f, h * 0.62f);
        const auto lc = nrLamp == 1 ? juce::Colour (0xff40ff70) : nrLamp == 2 ? juce::Colour (0xffff8a1a) : juce::Colour (0xff2a2a2a);
        if (nrLamp != 0) { g.setColour (lc.withAlpha (0.35f)); g.fillEllipse (lamp.expanded (2.5f)); }
        g.setColour (lc);
        g.fillEllipse (lamp);

        // the switch: NR ENC DEC OUT
        const auto on = dbx ? juce::Colour (0xffb0201a) : Colours::mustard;
        g.setFont (juce::Font (juce::FontOptions ("Arial", juce::jmin (11.0f, h * 0.62f), juce::Font::bold)));
        for (int m = 0; m < nr::numModes; ++m)
        {
            auto s = segment (m);
            const bool sel = m == nrMode;
            g.setColour (sel ? on : (dbx ? juce::Colour (0xffe7e4dc) : juce::Colour (0xff2b2b2b)));
            g.fillRect (s);
            g.setColour (dbx ? juce::Colour (0xff555555) : juce::Colours::black);
            g.drawRect (s, 1.0f);
            g.setColour (sel ? (dbx ? juce::Colours::white : juce::Colour (0xff111111)) : (dbx ? juce::Colour (0xff333333) : juce::Colour (0xffbbbbbb)));
            g.drawText (nr::modeLabel (m), s, juce::Justification::centred, false);
        }
    }

    // control strip under the meter
    const auto& sk = MachineSkin::current();
    auto strip = getLocalBounds().removeFromBottom (controlsHeight).toFloat().reduced (1.0f, 1.0f);
    g.setColour (isArmed ? Colours::redDim : sk.bridgeRow.darker (0.25f));
    g.fillRoundedRectangle (strip, 3.0f);

    const bool lightStrip = ! isArmed && sk.bridgeRow.darker (0.25f).getPerceivedBrightness() > 0.5f;
    g.setColour (lightStrip ? juce::Colour (0xff1d1a14) : Colours::text);
    g.setFont (uiFont ((float) numberArea.getHeight() * 0.95f, true));
    g.drawText (juce::String (index + 1), numberArea, juce::Justification::centredRight);
}

void ChannelModule::resized()
{
    auto r = getLocalBounds();
    auto controls = r.removeFromBottom (controlsHeight).reduced (4, 3);
    // the NR strip's place is always kept, so the bridge doesn't move when NR is switched
    stripArea = r.removeFromBottom (nrHeight);
    nrArea = nrSystem != 0 ? stripArea : juce::Rectangle<int>();
    meter.setBounds (r.reduced (0, 2));

    numberArea = controls.removeFromRight (juce::jmax (30, controls.getWidth() / 4));
    armButton.setBounds (controls.removeFromLeft (juce::jmin (controls.getWidth() - 8, 90)));
}

} // namespace hawthorne
