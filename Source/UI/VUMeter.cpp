#include "VUMeter.h"

namespace hawthorne
{

namespace
{
    constexpr float zeroVuPos   = 0.7079f;   // 0 VU sits at 70.8% of scale (+3 VU = 100%)
    constexpr float sweepDeg    = 48.0f;     // half-sweep either side of vertical
    constexpr float pinPos      = 1.12f;     // needle hits the stop just past +3
    constexpr float plateFrac   = 0.21f;     // bottom cover plate height (fraction of face)

    float posForVu (float vu) { return zeroVuPos * std::pow (10.0f, vu / 20.0f); }

    float vuForPos (float pos) { return 20.0f * std::log10 (juce::jmax (1.0e-4f, pos / zeroVuPos)); }
    constexpr float ledMinVu = -20.0f, ledMaxVu = 3.0f;
    constexpr int ledSegments = 24;
}

VUMeter::VUMeter (const juce::String& l) : label (l)
{
    setOpaque (false);
    setInterceptsMouseClicks (true, false);
}

void VUMeter::setLabel (const juce::String& l)
{
    if (l != label) { label = l; repaint(); }
}

//==============================================================================
void VUMeter::update (float rms, float peak, float dt)
{
    const float ref = std::pow (10.0f, referenceDbfs / 20.0f);
    const float target = juce::jmin (1.4f, rms / ref * zeroVuPos);

    // Integrate the meter movement in small steps for stability.
    constexpr float wn = 13.0f, zeta = 0.80f;
    const int steps = 8;
    const float h = juce::jlimit (0.0f, 0.1f, dt) / (float) steps;

    for (int i = 0; i < steps; ++i)
    {
        const float accel = wn * wn * (target - needle) - 2.0f * zeta * wn * velocity;
        velocity += accel * h;
        needle   += velocity * h;

        if (needle > pinPos) { needle = pinPos; velocity = juce::jmin (0.0f, velocity); }
        if (needle < 0.0f)   { needle = 0.0f;   velocity = juce::jmax (0.0f, velocity); }
    }

    const bool wasPeak = peakHold > 0.0f;
    if (peak >= std::pow (10.0f, -2.0f / 20.0f))   // within 2 dB of full scale
        peakHold = 1.0f;
    else
        peakHold = juce::jmax (0.0f, peakHold - dt);

    // LED meters: a fast peak bar on top of the VU level
    const float pkVu = 20.0f * std::log10 (juce::jmax (1.0e-6f, peak)) - referenceDbfs - 3.0f;
    if (pkVu > ledPeakDb) { ledPeakDb = pkVu; ledPeakHold = 0.8f; }
    else if ((ledPeakHold -= dt) <= 0.0f) ledPeakDb = juce::jmax (-60.0f, ledPeakDb - 26.0f * dt);

    if (std::abs (needle - drawnNeedle) > 0.0015f || wasPeak != (peakHold > 0.0f)
        || (skin().meter == MachineSkin::Meter::ledBar && ledPeakDb > ledMinVu - 1.0f))
        repaint();
}

void VUMeter::setRecLamp (RecLamp r, bool flashPhase)
{
    const bool lit = r == RecLamp::recording || (r == RecLamp::armed && flashPhase);
    if (r != recLamp || lit != recLit)
    {
        recLamp = r;
        recLit = lit;
        repaint();
    }
}

//==============================================================================
juce::Rectangle<float> VUMeter::faceArea() const
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    const float bezel = juce::jmax (3.0f, r.getWidth() * 0.045f);
    return r.reduced (bezel);
}

juce::Point<float> VUMeter::pivot() const
{
    auto f = faceArea();
    return { f.getCentreX(), f.getBottom() + f.getHeight() * 0.04f };
}

float VUMeter::radius() const
{
    auto f = faceArea();
    return juce::jmin (f.getWidth() * 0.50f, (pivot().y - f.getY()) - f.getHeight() * 0.26f);
}

float VUMeter::angleFor (float scalePos) const
{
    return juce::degreesToRadians (-sweepDeg + 2.0f * sweepDeg * scalePos);
}

void VUMeter::resized()
{
    renderFace();
}

void VUMeter::renderFace()
{
    const float scale = 2.0f; // render at 2x so it stays crisp on HiDPI
    const int w = juce::roundToInt ((float) getWidth() * scale);
    const int h = juce::roundToInt ((float) getHeight() * scale);

    if (w <= 0 || h <= 0)
        return;

    faceCache = juce::Image (juce::Image::ARGB, w, h, true);
    juce::Graphics g (faceCache);
    g.addTransform (juce::AffineTransform::scale (scale));

    if (skin().meter == MachineSkin::Meter::ledBar)
    {
        renderLedFace (g);
        return;
    }

    const auto& sk = skin();
    const juce::Colour faceTop = sk.faceTop, faceBottom = sk.faceBottom, ink = sk.meterInk, redInk = sk.meterRed;
    auto outer = getLocalBounds().toFloat().reduced (1.0f);
    auto face = faceArea();
    const auto P = pivot();
    const float R = radius();
    const float fh = face.getHeight();

    // Bezel
    g.setColour (sk.bezel);
    g.fillRoundedRectangle (outer, 5.0f);
    g.setGradientFill (juce::ColourGradient (Colours::edge.brighter (0.3f), outer.getX(), outer.getY(),
                                             Colours::black, outer.getX(), outer.getBottom(), false));
    g.drawRoundedRectangle (outer.reduced (0.5f), 5.0f, 1.2f);

    // Backlit face
    juce::ColourGradient grad (faceTop, face.getCentreX(), face.getY(), faceBottom, face.getCentreX(), face.getBottom(), false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (face, 2.0f);

    // Vignette towards the corners (lamp behind the face)
    juce::ColourGradient vig (juce::Colours::transparentBlack, face.getCentreX(), face.getY() + fh * 0.45f,
                              juce::Colour (0x30000000), face.getX(), face.getY(), true);
    g.setGradientFill (vig);
    g.fillRoundedRectangle (face, 2.0f);

    g.saveState();
    g.reduceClipRegion (face.toNearestInt());

    auto pointAt = [&] (float pos, float r)
    {
        const float a = angleFor (pos);
        return juce::Point<float> (P.x + r * std::sin (a), P.y - r * std::cos (a));
    };

    auto arc = [&] (float from, float to, float r)
    {
        juce::Path p;
        p.addCentredArc (P.x, P.y, r, r, 0.0f, angleFor (from), angleFor (to), true);
        return p;
    };

    // Scale arcs: black to 0 VU, heavy red band 0..+3
    const float lineW = juce::jmax (0.8f, fh * 0.012f);
    g.setColour (ink);
    g.strokePath (arc (posForVu (-20.0f), posForVu (0.0f), R), juce::PathStrokeType (lineW));
    g.setColour (redInk);
    g.strokePath (arc (posForVu (0.0f), 1.0f, R + fh * 0.012f), juce::PathStrokeType (fh * 0.035f));

    // Ticks and numerals
    struct Mark { float vu; const char* text; bool major; };
    const Mark marks[] = { { -20, "20", true }, { -10, "10", true }, { -7, "7", true }, { -6, nullptr, false },
                           { -5, "5", true },  { -4, nullptr, false }, { -3, "3", true }, { -2, "2", true },
                           { -1, "1", true },  { 0, "0", true }, { 0.5f, nullptr, false }, { 1, "1", true },
                           { 1.5f, nullptr, false }, { 2, "2", true }, { 3, "3", true } };

    const float numSize = juce::jmax (6.0f, fh * 0.115f);
    g.setFont (uiFont (numSize, true));

    for (auto& m : marks)
    {
        const float pos = posForVu (m.vu);
        const float len = m.major ? fh * 0.075f : fh * 0.045f;
        g.setColour (m.vu > 0.0f ? redInk : ink);
        g.drawLine (juce::Line<float> (pointAt (pos, R), pointAt (pos, R + len)), m.major ? lineW * 1.3f : lineW);

        if (m.text != nullptr)
        {
            const auto c = pointAt (pos, R + len + numSize * 0.75f);
            g.drawText (m.text, juce::Rectangle<float> (numSize * 2.0f, numSize * 1.2f).withCentre (c),
                        juce::Justification::centred, false);
        }
    }

    // Minus / plus signs at the ends of the scale
    {
        const float sz = numSize * 1.1f;
        g.setColour (ink);
        g.drawText ("-", juce::Rectangle<float> (sz, sz).withCentre (pointAt (posForVu (-20.0f), R - fh * 0.11f)),
                    juce::Justification::centred, false);
        g.setColour (redInk);
        g.drawText ("+", juce::Rectangle<float> (sz, sz).withCentre (pointAt (1.0f, R - fh * 0.11f)),
                    juce::Justification::centred, false);
    }

    // Percentage scale beneath the arc on larger meters
    if (face.getWidth() > 110.0f)
    {
        const float r2 = R - fh * 0.06f;
        g.setColour (ink.withAlpha (0.55f));
        g.strokePath (arc (0.0f, 1.0f, r2), juce::PathStrokeType (lineW * 0.6f));
        g.setFont (uiFont (numSize * 0.6f));
        for (float pc : { 0.0f, 0.5f, 1.0f })
        {
            g.drawLine (juce::Line<float> (pointAt (pc, r2), pointAt (pc, r2 - fh * 0.035f)), lineW * 0.6f);
            g.drawText (juce::String (juce::roundToInt (pc * 100.0f)),
                        juce::Rectangle<float> (numSize * 2.0f, numSize).withCentre (pointAt (pc, r2 - fh * 0.08f)),
                        juce::Justification::centred, false);
        }
    }

    // "VU"
    g.setColour (ink);
    g.setFont (juce::Font (juce::FontOptions ("Times New Roman", fh * 0.17f, juce::Font::bold)));
    g.drawText ("VU", juce::Rectangle<float> (face.getWidth(), fh * 0.2f)
                          .withCentre ({ face.getCentreX(), face.getY() + fh * (1.0f - plateFrac) - fh * 0.13f }),
                juce::Justification::centred, false);

    g.restoreState();
}

//==============================================================================
juce::Rectangle<float> VUMeter::ledArea() const
{
    auto face = faceArea();
    const float fh = face.getHeight();
    auto a = face.withBottom (face.getBottom() - fh * plateFrac).reduced (face.getWidth() * 0.06f, fh * 0.1f);
    return a.withTrimmedTop (a.getHeight() * 0.42f);
}

void VUMeter::renderLedFace (juce::Graphics& g)
{
    const auto& sk = skin();
    auto outer = getLocalBounds().toFloat().reduced (1.0f);
    auto face = faceArea();
    const float fh = face.getHeight();

    g.setColour (sk.bezel);
    g.fillRoundedRectangle (outer, 4.0f);
    g.setColour (sk.faceTop);
    g.fillRoundedRectangle (face, 2.0f);

    // unlit segments and the scale
    auto bar = ledArea();
    const float segW = bar.getWidth() / (float) ledSegments;
    for (int i = 0; i < ledSegments; ++i)
    {
        const float vu = ledMinVu + (ledMaxVu - ledMinVu) * ((float) i + 0.5f) / (float) ledSegments;
        const auto col = vu >= 0.0f ? juce::Colour (0xffff3b2e) : vu >= -6.0f ? juce::Colour (0xffffc928) : juce::Colour (0xff3dff6a);
        g.setColour (col.withAlpha (0.12f));
        g.fillRect (juce::Rectangle<float> (bar.getX() + (float) i * segW + 1.0f, bar.getY(), segW - 2.0f, bar.getHeight()));
    }
    const float numSize = juce::jmax (6.0f, fh * 0.12f);
    g.setFont (uiFont (numSize, true));
    for (float vu : { -20.0f, -10.0f, -5.0f, 0.0f, 3.0f })
    {
        const float x = bar.getX() + bar.getWidth() * (vu - ledMinVu) / (ledMaxVu - ledMinVu);
        g.setColour (vu > 0.0f ? sk.meterRed : sk.meterInk);
        g.drawText (vu > 0.0f ? "+" + juce::String ((int) vu) : juce::String ((int) vu),
                    juce::Rectangle<float> (numSize * 3.0f, numSize * 1.2f).withCentre ({ x, bar.getY() - numSize * 0.9f }),
                    juce::Justification::centred, false);
    }
    g.setColour (sk.meterInk.withAlpha (0.6f));
    g.setFont (uiFont (numSize * 0.8f, true));
    g.drawText ("dB VU", face.withHeight (fh * 0.22f).reduced (4.0f, 0.0f), juce::Justification::centredRight, false);
}

void VUMeter::paintLed (juce::Graphics& g)
{
    auto bar = ledArea();
    const float segW = bar.getWidth() / (float) ledSegments;
    const float level = vuForPos (needle);
    for (int i = 0; i < ledSegments; ++i)
    {
        const float vu = ledMinVu + (ledMaxVu - ledMinVu) * ((float) i + 0.5f) / (float) ledSegments;
        const bool on = vu <= level || (std::abs (vu - ledPeakDb) < (ledMaxVu - ledMinVu) / (float) ledSegments * 0.5f);
        if (! on) continue;
        const auto col = vu >= 0.0f ? juce::Colour (0xffff3b2e) : vu >= -6.0f ? juce::Colour (0xffffc928) : juce::Colour (0xff3dff6a);
        auto seg = juce::Rectangle<float> (bar.getX() + (float) i * segW + 1.0f, bar.getY(), segW - 2.0f, bar.getHeight());
        g.setColour (col.withAlpha (0.3f));
        g.fillRect (seg.expanded (1.0f));
        g.setColour (col);
        g.fillRect (seg);
    }
}

void VUMeter::paint (juce::Graphics& g)
{
    if (faceCache.isValid())
        g.drawImage (faceCache, getLocalBounds().toFloat());
    const auto& sk = skin();
    const bool led = sk.meter == MachineSkin::Meter::ledBar;

    auto face = faceArea();
    const auto P = pivot();
    const float fh = face.getHeight();
    auto plate = face.withTop (face.getBottom() - fh * plateFrac);

    if (led) paintLed (g);

    // Needle (with a soft shadow on the face)
    if (! led)
    {
        g.saveState();
        g.reduceClipRegion (face.withBottom (plate.getY()).toNearestInt());

        const float a = angleFor (needle);
        const float len = radius() + fh * 0.06f;
        const juce::Point<float> tip (P.x + len * std::sin (a), P.y - len * std::cos (a));
        const float wNeedle = juce::jmax (1.0f, fh * 0.016f);

        g.setColour (juce::Colour (0x26000000));
        g.drawLine (juce::Line<float> (P.translated (2.0f, 1.5f), tip.translated (2.0f, 1.5f)), wNeedle * 1.4f);
        g.setColour (sk.needle);
        g.drawLine (juce::Line<float> (P, tip), wNeedle);

        // Glass sheen
        juce::ColourGradient sheen (juce::Colour (0x1affffff), face.getX(), face.getY(),
                                    juce::Colours::transparentWhite, face.getCentreX(), face.getCentreY(), false);
        g.setGradientFill (sheen);
        g.fillRect (face);
        g.restoreState();
    }

    // Cover plate with label and lamps
    g.setColour (sk.plate);
    g.fillRect (plate);
    g.setColour (Colours::edge);
    g.drawHorizontalLine ((int) plate.getY(), plate.getX(), plate.getRight());

    const float lampD = juce::jmin (plate.getHeight() * 0.5f, 9.0f);

    auto lamp = [&] (juce::Rectangle<float> r, bool lit, juce::Colour on)
    {
        if (lit)
        {
            g.setColour (on.withAlpha (0.35f));
            g.fillEllipse (r.expanded (lampD * 0.35f));
        }
        g.setColour (lit ? on : on.darker (2.2f));
        g.fillEllipse (r);
    };

    lamp (juce::Rectangle<float> (lampD, lampD).withCentre ({ plate.getX() + lampD * 1.1f, plate.getCentreY() }),
          recLit, Colours::red);
    lamp (juce::Rectangle<float> (lampD, lampD).withCentre ({ plate.getRight() - lampD * 1.1f, plate.getCentreY() }),
          peakHold > 0.0f, juce::Colour (0xffff7a1a));

    g.setColour (sk.plateText);
    g.setFont (monoFont (juce::jmin (plate.getHeight() * 0.75f, 14.0f), true));
    g.drawText (label, plate.reduced (lampD * 2.2f, 0.0f), juce::Justification::centred, true);

    drawnNeedle = needle;
}

} // namespace hawthorne
