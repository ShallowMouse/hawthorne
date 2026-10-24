#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "HawthorneLookAndFeel.h"

namespace hawthorne
{

//==============================================================================
/**
    Analogue VU meter.

    Ballistics follow IEC 60268-17: a second-order movement (wn = 13 rad/s,
    zeta = 0.80) that reaches 99% of a step in 300 ms with ~1.5% overshoot.
    Scale is linear in voltage, 0 VU = -18 dBFS RMS (reference level).
    A peak lamp catches transients the needle can't show.
*/
class VUMeter : public juce::Component,
                public juce::SettableTooltipClient
{
public:
    explicit VUMeter (const juce::String& label);

    /** Feed RMS and sample peak measured since the previous call. */
    void update (float rms, float peak, float dtSeconds);

    enum class RecLamp { off, armed, recording };
    void setRecLamp (RecLamp, bool flashPhase);
    void setLabel (const juce::String&);

    static constexpr float referenceDbfs = -18.0f;   // level that reads 0 VU

    std::function<void()> onDoubleClick;
    void mouseDoubleClick (const juce::MouseEvent&) override { if (onDoubleClick) onDoubleClick(); }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Rectangle<float> faceArea() const;
    juce::Point<float> pivot() const;
    float radius() const;
    float angleFor (float scalePos) const;     // scalePos: 0 = rest, 1 = +3 VU
    void renderFace();

    juce::String label;
    juce::Image faceCache;

    float needle = 0.0f, velocity = 0.0f;
    float drawnNeedle = -1.0f;
    float peakHold = 0.0f;
    RecLamp recLamp = RecLamp::off;
    bool recLit = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VUMeter)
};

} // namespace hawthorne
