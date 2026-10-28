#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Engine/TapeEngine.h"
#include "HawthorneLookAndFeel.h"
#include "MachineSkin.h"

namespace hawthorne
{

//==============================================================================
/**
    The alignment panel (calibration): the reel's operating level and record bias,
    the line-up oscillator (watch the meters: the tone comes back off the repro head)
    and line-up tones recorded at the head of the reel.
*/
class AlignmentPanel : public juce::Component
{
public:
    AlignmentPanel (TapeEngine& e, Reel& r) : engine (e), reel (r)
    {
        const auto stock = (tape::Stock) reel.tapeStock;
        const bool tape = reel.tapeStock != 0 && reel.machine != tape::noMultitrackMachine;

        static const int levels[] = { 0, 3, 6, 9 };
        for (int i = 0; i < 4; ++i)
        {
            auto* b = levelKeys.add (new juce::TextButton (juce::String (levels[i] == 0 ? "0" : "+" + juce::String (levels[i]))
                                                           + "\n" + juce::String (tape::levelFluxivity (levels[i])) + " nWb/m"));
            b->setClickingTogglesState (false);
            b->setColour (juce::TextButton::buttonOnColourId, Colours::mustard);
            b->setColour (juce::TextButton::textColourOnId, juce::Colours::black);
            b->setEnabled (tape);
            b->onClick = [this, i] { engine.setAlignment (levels[i], reel.alignBias); refresh(); };
            addAndMakeVisible (b);
        }

        bias.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        bias.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 22);
        bias.setRange (-1.0, 4.0, 0.25);
        bias.textFromValueFunction = [] (double v) { return (v >= 0 ? "+" : "") + juce::String (v, 2).trimCharactersAtEnd ("0").trimCharactersAtEnd (".") + " dB"; };
        bias.valueFromTextFunction = [] (const juce::String& t) { return t.retainCharacters ("+-.0123456789").getDoubleValue(); };
        bias.setDoubleClickReturnValue (true, tape::recommendedBias (stock));
        bias.setValue (reel.alignBias, juce::dontSendNotification);
        bias.setColour (juce::Slider::rotarySliderFillColourId, Colours::mustard);
        bias.setColour (juce::Slider::thumbColourId, Colours::text);
        bias.setEnabled (tape);
        bias.onValueChange = [this] { engine.setAlignment (reel.alignLevel, bias.getValue()); refresh(); };
        addAndMakeVisible (bias);

        static const int freqs[] = { 0, 100, 1000, 10000 };
        static const char* names[] = { "OFF", "100 Hz", "1 kHz", "10 kHz" };
        for (int i = 0; i < 4; ++i)
        {
            auto* b = oscKeys.add (new juce::TextButton (names[i]));
            b->setColour (juce::TextButton::buttonOnColourId, i == 0 ? Colours::panelLight : Colours::red);
            b->onClick = [this, i] { engine.setOscillator (freqs[i]); refresh(); };
            addAndMakeVisible (b);
        }

        tonesButton.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffff6a50));
        tonesButton.onClick = [this] { if (onRecordTones) onRecordTones(); };
        addAndMakeVisible (tonesButton);

        doneButton.onClick = [this] { if (auto* dw = findParentComponentOfClass<juce::DialogWindow>()) dw->exitModalState (0); };
        addAndMakeVisible (doneButton);

        setSize (860, 520);
        refresh();
    }

    ~AlignmentPanel() override { engine.setOscillator (0); }

    std::function<void()> onRecordTones;

    void refresh()
    {
        static const int levels[] = { 0, 3, 6, 9 };
        for (int i = 0; i < 4; ++i) levelKeys[i]->setToggleState (reel.alignLevel == levels[i], juce::dontSendNotification);
        static const int freqs[] = { 0, 100, 1000, 10000 };
        for (int i = 0; i < 4; ++i) oscKeys[i]->setToggleState (engine.getOscillator() == freqs[i], juce::dontSendNotification);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3d40), 0, 0, juce::Colour (0xff2b2e31), 0, (float) getHeight(), false));
        g.fillAll();
        const auto stock = (tape::Stock) reel.tapeStock;
        const bool tape = reel.tapeStock != 0 && reel.machine != tape::noMultitrackMachine;

        auto head = getLocalBounds().removeFromTop (46);
        g.setColour (juce::Colour (0xff25282b));
        g.fillRect (head);
        g.setColour (Colours::mustard);
        g.setFont (monoFont (15.0f, true));
        g.drawText ("ALIGNMENT - " + reel.getName().toUpperCase() + " (" + tape::info (stock).label + ", "
                        + Reel::formatIps (reel.ips) + " IPS)", head.reduced (20, 0), juce::Justification::centredLeft);
        g.setColour (juce::Colour (0xffd8dcdf));
        g.setFont (juce::Font (juce::FontOptions ("Arial", 12.0f, juce::Font::bold)));
        g.drawText (tape::multitrackMachine (reel.machine).label, head.reduced (20, 0), juce::Justification::centredRight);

        auto section = [&] (juce::Rectangle<int> r, const juce::String& t)
        {
            g.setColour (juce::Colour (0xff9aa0a6));
            g.setFont (juce::Font (juce::FontOptions ("Arial", 11.0f, juce::Font::bold)).withExtraKerningFactor (0.15f));
            g.drawText (t, r, juce::Justification::centredLeft);
        };
        auto note = [&] (juce::Rectangle<int> r, const juce::String& t)
        {
            g.setColour (juce::Colour (0xffc9ced3));
            g.setFont (uiFont (13.0f));
            g.drawFittedText (t, r, juce::Justification::topLeft, 4, 1.0f);
        };

        section (levelLabel, "OPERATING LEVEL  (0 VU = -18 dBFS)");
        const int rl = tape::recommendedLevel (stock);
        note (levelNote, ! tape ? juce::String ("Clean tape / no machine: nothing to align - the oscillator still works.")
                                : "Hotter = less hiss, saturates sooner. " + juce::String (tape::info (stock).label).upToFirstOccurrenceOf (" ", false, false)
                                  + " is made for " + (rl == 0 ? juce::String ("0 (standard level)") : "+" + juce::String (rl)) + ".");
        section (biasLabel, "RECORD BIAS  (OVERBIAS AT 10 kHz)");
        note (biasNote, "Recommended for this tape: +" + juce::String (tape::recommendedBias (stock), 1) + " dB (double-click the knob).\n"
                        "Under: brighter, more distortion and noise. Over: darker, smoother, firmer low end.");
        g.setColour (juce::Colour (0xffc9ced3));
        g.setFont (monoFont (11.0f));
        g.drawText ("UNDER", bias.getBounds().withWidth (60).translated (-62, 0).withTrimmedTop (bias.getHeight() - 50), juce::Justification::centredRight);
        g.drawText ("OVER", bias.getBounds().withWidth (60).translated (bias.getWidth() + 2, 0).withTrimmedTop (bias.getHeight() - 50), juce::Justification::centredLeft);

        section (oscLabel, "LINE-UP OSCILLATOR  (0 VU into every track - watch the meters)");
        note (oscNote, "The meters show the tone back off the repro head: 10 kHz drops as you over-bias, and everything compresses as the level goes up.");
        section (tonesLabel, "LINE-UP TONES AT THE HEAD OF THE REEL");
        note (footNote, "Saved with the reel (the Alignment field on the tape box). Level and bias are printed into what you record from now on - "
                        "takes already on the tape keep the alignment they were recorded with; hiss follows the current level.");
    }

    void resized() override
    {
        auto r = getLocalBounds().withTrimmedTop (46).reduced (24, 16);
        auto bottom = r.removeFromBottom (40);
        doneButton.setBounds (bottom.removeFromRight (110));
        auto left = r.removeFromLeft (r.getWidth() / 2 + 20);
        auto right = r.withTrimmedLeft (20);

        levelLabel = left.removeFromTop (22);
        auto keys = left.removeFromTop (58);
        const int kw = (keys.getWidth() - 3 * 6) / 4;
        for (auto* b : levelKeys) { b->setBounds (keys.removeFromLeft (kw)); keys.removeFromLeft (6); }
        left.removeFromTop (6);
        levelNote = left.removeFromTop (40);
        left.removeFromTop (14);
        oscLabel = left.removeFromTop (22);
        auto ok = left.removeFromTop (32);
        for (auto* b : oscKeys) { b->setBounds (ok.removeFromLeft (84)); ok.removeFromLeft (6); }
        left.removeFromTop (6);
        oscNote = left.removeFromTop (38);
        left.removeFromTop (12);
        tonesLabel = left.removeFromTop (22);
        tonesButton.setBounds (left.removeFromTop (40).withWidth (360));
        footNote = bottom.withTrimmedRight (16);

        biasLabel = right.removeFromTop (22);
        right.removeFromTop (8);
        bias.setBounds (right.removeFromTop (190).withSizeKeepingCentre (170, 190));
        right.removeFromTop (8);
        biasNote = right.removeFromTop (80);
    }

private:
    TapeEngine& engine;
    Reel& reel;
    juce::OwnedArray<juce::TextButton> levelKeys, oscKeys;
    juce::Slider bias;
    juce::TextButton tonesButton { "RECORD TONES  (1 kHz / 10 kHz / 100 Hz, 30 s each)" }, doneButton { "DONE" };
    juce::Rectangle<int> levelLabel, levelNote, biasLabel, biasNote, oscLabel, oscNote, tonesLabel, footNote;
};

} // namespace hawthorne
