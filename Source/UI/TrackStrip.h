#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "HawthorneLookAndFeel.h"

namespace hawthorne
{

//==============================================================================
/** Vertical peak meter with hold and a latching clip light (click to reset). */
class LevelMeter : public juce::Component, public juce::SettableTooltipClient
{
public:
    void pushPeak (float linearPeak);   // call at UI rate
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { clipped = false; repaint(); }
    void reset() { level = holdLevel = -100.0f; clipped = false; repaint(); }

private:
    static float toDb (float x) { return x > 1.0e-5f ? 20.0f * std::log10 (x) : -100.0f; }
    static float dbToFrac (float db);

    float level = -100.0f, holdLevel = -100.0f;
    int holdFrames = 0;
    bool clipped = false;
};

//==============================================================================
class TrackStrip : public juce::Component
{
public:
    explicit TrackStrip (int index);

    void setTrackName (const juce::String&);
    void setInputChoices (const juce::StringArray& channelNames, const juce::BigInteger& active, int selected);
    void setArmed (bool armed, bool recording, bool flashPhase);
    void setTakeCount (int n);
    void pushPeak (float p) { meter.pushPeak (p); }
    void resetMeter()       { meter.reset(); }

    std::function<void (int track, bool arm)>                onArm;
    std::function<void (int track, int inputChannel)>        onInputChanged;
    std::function<void (int track, const juce::String& name)> onRename;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    const int index;
    juce::Label nameLabel;
    juce::ComboBox inputBox;
    juce::TextButton armButton { "REC" };
    LevelMeter meter;
    int takeCount = 0;
    bool isArmed = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackStrip)
};

} // namespace hawthorne
