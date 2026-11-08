#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PartHeader.h"
#include "HawthorneLookAndFeel.h"

namespace hawthorne
{

//==============================================================================
/** Hawthorne's icons (in place of words on keys, header buttons and lamps). Drawn as
    vector paths in a square, in whatever colour the key / lamp uses. */
enum class Icon
{
    none,
    // transport
    rtz, rew, ffw, cue, stop, play, rec, mark, click,
    // header
    library, sheet, routing, network, tempo, timecode, layout, done, machine, settings, help, fullScreen, exitFullScreen,
    // autolocator, speed, master
    punchIn, punchOut, autoPunch, rehearse, loop, vari, arm, tape, input
};

inline void drawIcon (juce::Graphics& g, Icon icon, juce::Rectangle<float> area, juce::Colour c)
{
    if (icon == Icon::none) return;
    const float s = juce::jmin (area.getWidth(), area.getHeight());
    auto b = area.withSizeKeepingCentre (s, s);
    const float x = b.getX(), y = b.getY();
    auto P = [&] (float fx, float fy) { return juce::Point<float> (x + fx * s, y + fy * s); };
    const float stroke = juce::jmax (1.2f, s * 0.085f);
    juce::PathStrokeType st (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    juce::Path p;
    g.setColour (c);

    auto tri = [&] (float x0, float x1, float y0, float y1, bool right)
    {
        juce::Path t;
        if (right) t.addTriangle (P (x0, y0), P (x1, (y0 + y1) * 0.5f), P (x0, y1));
        else       t.addTriangle (P (x1, y0), P (x0, (y0 + y1) * 0.5f), P (x1, y1));
        g.fillPath (t);
    };
    auto line = [&] (float x0, float y0, float x1, float y1) { g.drawLine ({ P (x0, y0), P (x1, y1) }, stroke); };
    auto ring = [&] (float cx, float cy, float rad) { g.drawEllipse (juce::Rectangle<float> (rad * 2 * s, rad * 2 * s).withCentre (P (cx, cy)), stroke); };
    auto dot  = [&] (float cx, float cy, float rad) { g.fillEllipse (juce::Rectangle<float> (rad * 2 * s, rad * 2 * s).withCentre (P (cx, cy))); };
    auto box  = [&] (float x0, float y0, float x1, float y1) { g.drawRoundedRectangle (juce::Rectangle<float> (P (x0, y0), P (x1, y1)), s * 0.05f, stroke); };

    switch (icon)
    {
        case Icon::rtz:   g.fillRect (juce::Rectangle<float> (P (0.14f, 0.25f), P (0.24f, 0.75f))); tri (0.24f, 0.54f, 0.25f, 0.75f, false); tri (0.54f, 0.84f, 0.25f, 0.75f, false); break;
        case Icon::rew:   tri (0.12f, 0.48f, 0.25f, 0.75f, false); tri (0.48f, 0.84f, 0.25f, 0.75f, false); break;
        case Icon::ffw:   tri (0.16f, 0.52f, 0.25f, 0.75f, true);  tri (0.52f, 0.88f, 0.25f, 0.75f, true); break;
        case Icon::stop:  g.fillRoundedRectangle (juce::Rectangle<float> (P (0.27f, 0.27f), P (0.73f, 0.73f)), s * 0.04f); break;
        case Icon::play:  tri (0.3f, 0.78f, 0.22f, 0.78f, true); break;
        case Icon::rec:   dot (0.5f, 0.5f, 0.24f); break;
        case Icon::cue:
            // headphones: hear the tape while it spools
            p.addCentredArc (x + 0.5f * s, y + 0.55f * s, 0.3f * s, 0.3f * s, 0.0f, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, true);
            g.strokePath (p, st);
            g.fillRoundedRectangle (juce::Rectangle<float> (P (0.14f, 0.52f), P (0.3f, 0.8f)), s * 0.05f);
            g.fillRoundedRectangle (juce::Rectangle<float> (P (0.7f, 0.52f), P (0.86f, 0.8f)), s * 0.05f);
            break;
        case Icon::mark:
            // a flag
            line (0.3f, 0.16f, 0.3f, 0.86f);
            p.startNewSubPath (P (0.3f, 0.18f)); p.lineTo (P (0.78f, 0.31f)); p.lineTo (P (0.3f, 0.46f)); p.closeSubPath();
            g.fillPath (p);
            break;
        case Icon::input:
            // a signal going in: an arrow into a bracket
            line (0.12f, 0.5f, 0.56f, 0.5f);
            tri (0.5f, 0.68f, 0.36f, 0.64f, true);
            line (0.80f, 0.22f, 0.80f, 0.78f);
            line (0.66f, 0.22f, 0.80f, 0.22f);
            line (0.66f, 0.78f, 0.80f, 0.78f);
            break;
        case Icon::click:
        case Icon::tempo:
            // a metronome
            p.startNewSubPath (P (0.38f, 0.14f)); p.lineTo (P (0.62f, 0.14f)); p.lineTo (P (0.78f, 0.86f)); p.lineTo (P (0.22f, 0.86f)); p.closeSubPath();
            g.strokePath (p, st);
            line (0.5f, 0.72f, 0.7f, 0.28f);
            line (0.3f, 0.66f, 0.7f, 0.66f);
            break;
        case Icon::library:
            // tape boxes on a shelf
            box (0.14f, 0.22f, 0.3f, 0.8f); box (0.36f, 0.22f, 0.52f, 0.8f);
            p.startNewSubPath (P (0.6f, 0.26f)); p.lineTo (P (0.74f, 0.22f)); p.lineTo (P (0.88f, 0.76f)); p.lineTo (P (0.74f, 0.8f)); p.closeSubPath();
            g.strokePath (p, st);
            line (0.08f, 0.86f, 0.92f, 0.86f);
            break;
        case Icon::sheet:
            // a session sheet: a page with lines
            box (0.22f, 0.12f, 0.78f, 0.88f);
            for (float ly : { 0.32f, 0.47f, 0.62f }) line (0.33f, ly, 0.67f, ly);
            line (0.33f, 0.76f, 0.52f, 0.76f);
            break;
        case Icon::routing:
            // a patch matrix
            for (float f : { 0.3f, 0.5f, 0.7f }) { line (f, 0.16f, f, 0.84f); line (0.16f, f, 0.84f, f); }
            dot (0.3f, 0.5f, 0.07f); dot (0.5f, 0.7f, 0.07f); dot (0.7f, 0.3f, 0.07f);
            break;
        case Icon::network:
            // linked computers
            ring (0.5f, 0.24f, 0.1f); ring (0.22f, 0.74f, 0.1f); ring (0.78f, 0.74f, 0.1f);
            line (0.45f, 0.33f, 0.27f, 0.65f); line (0.55f, 0.33f, 0.73f, 0.65f); line (0.32f, 0.74f, 0.68f, 0.74f);
            break;
        case Icon::timecode:
            // a clock
            ring (0.5f, 0.5f, 0.34f);
            line (0.5f, 0.5f, 0.5f, 0.28f); line (0.5f, 0.5f, 0.66f, 0.58f);
            break;
        case Icon::layout:
            // two screens
            box (0.08f, 0.2f, 0.52f, 0.62f); box (0.56f, 0.3f, 0.92f, 0.68f);
            line (0.3f, 0.62f, 0.3f, 0.76f); line (0.2f, 0.78f, 0.4f, 0.78f);
            line (0.74f, 0.68f, 0.74f, 0.78f); line (0.66f, 0.8f, 0.82f, 0.8f);
            break;
        case Icon::done:
            p.startNewSubPath (P (0.2f, 0.52f)); p.lineTo (P (0.42f, 0.74f)); p.lineTo (P (0.82f, 0.28f));
            g.strokePath (p, juce::PathStrokeType (stroke * 1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            break;
        case Icon::machine:
        case Icon::tape:
            // a reel-to-reel: two reels and the tape
            ring (0.28f, 0.42f, 0.18f); ring (0.72f, 0.42f, 0.18f);
            dot (0.28f, 0.42f, 0.04f); dot (0.72f, 0.42f, 0.04f);
            line (0.28f, 0.6f, 0.72f, 0.6f);
            if (icon == Icon::machine) line (0.12f, 0.82f, 0.88f, 0.82f);
            break;
        case Icon::settings:
        {
            // a gear
            const auto cc = P (0.5f, 0.5f);
            for (int i = 0; i < 8; ++i)
            {
                const float a = (float) i * juce::MathConstants<float>::twoPi / 8.0f;
                g.drawLine ({ cc + juce::Point<float> (std::sin (a), -std::cos (a)) * (0.26f * s), cc + juce::Point<float> (std::sin (a), -std::cos (a)) * (0.4f * s) }, stroke * 1.5f);
            }
            ring (0.5f, 0.5f, 0.26f);
            ring (0.5f, 0.5f, 0.1f);
            break;
        }
        case Icon::help:
            g.setFont (monoFont (s * 0.78f, true));
            g.drawText ("?", b, juce::Justification::centred);
            break;
        case Icon::fullScreen:
        case Icon::exitFullScreen:
        {
            // four corners - pointing out (full screen) or in (leave it)
            const bool out = icon == Icon::fullScreen;
            const float a = 0.16f, bb = 0.38f, z = 0.84f, w = 0.62f;
            auto corner = [&] (float cx, float cy, float dx, float dy)
            {
                juce::Path k;
                k.startNewSubPath (P (cx, cy + dy)); k.lineTo (P (cx, cy)); k.lineTo (P (cx + dx, cy));
                g.strokePath (k, st);
            };
            if (out) { corner (a, a, bb - a, bb - a); corner (z, a, w - z, bb - a); corner (a, z, bb - a, w - z); corner (z, z, w - z, w - z); }
            else     { corner (bb, bb, a - bb, a - bb); corner (w, bb, z - w, a - bb); corner (bb, w, a - bb, z - w); corner (w, w, z - w, z - w); }
            break;
        }
        case Icon::punchIn:
            // into a bracket
            line (0.66f, 0.18f, 0.66f, 0.82f); line (0.66f, 0.18f, 0.8f, 0.18f); line (0.66f, 0.82f, 0.8f, 0.82f);
            line (0.14f, 0.5f, 0.5f, 0.5f); tri (0.42f, 0.6f, 0.36f, 0.64f, true);
            break;
        case Icon::punchOut:
            line (0.34f, 0.18f, 0.34f, 0.82f); line (0.2f, 0.18f, 0.34f, 0.18f); line (0.2f, 0.82f, 0.34f, 0.82f);
            line (0.44f, 0.5f, 0.78f, 0.5f); tri (0.72f, 0.9f, 0.36f, 0.64f, true);
            break;
        case Icon::autoPunch:
            // a record dot between the punch points
            line (0.18f, 0.22f, 0.18f, 0.78f); line (0.82f, 0.22f, 0.82f, 0.78f);
            dot (0.5f, 0.5f, 0.17f);
            break;
        case Icon::rehearse:
            // the same, hollow: nothing recorded
            line (0.18f, 0.22f, 0.18f, 0.78f); line (0.82f, 0.22f, 0.82f, 0.78f);
            ring (0.5f, 0.5f, 0.15f);
            break;
        case Icon::loop:
        {
            // round and round
            p.addCentredArc (x + 0.5f * s, y + 0.5f * s, 0.3f * s, 0.3f * s, 0.0f, 0.5f, juce::MathConstants<float>::twoPi - 0.2f, true);
            g.strokePath (p, st);
            const auto tip = P (0.5f, 0.5f) + juce::Point<float> (std::sin (-0.2f), -std::cos (-0.2f)) * (0.3f * s);
            juce::Path h;
            h.addTriangle (tip + juce::Point<float> (-0.02f * s, -0.13f * s), tip + juce::Point<float> (0.16f * s, 0.0f), tip + juce::Point<float> (-0.02f * s, 0.13f * s));
            g.fillPath (h);
            break;
        }
        case Icon::vari:
        {
            // a speed dial
            p.addCentredArc (x + 0.5f * s, y + 0.62f * s, 0.36f * s, 0.36f * s, 0.0f, -2.2f, 2.2f, true);
            g.strokePath (p, st);
            line (0.5f, 0.62f, 0.7f, 0.34f);
            dot (0.5f, 0.62f, 0.06f);
            break;
        }
        case Icon::arm:
            // a record ring: ready to record
            ring (0.5f, 0.5f, 0.24f);
            dot (0.5f, 0.5f, 0.1f);
            break;
        default: break;
    }
}

/** A text button that shows an icon instead of its words (the words stay as its name and tooltip). */
class IconButton : public juce::TextButton
{
public:
    IconButton (const juce::String& name, Icon i) : juce::TextButton (name), icon (i) { setTooltip (name); }
    void setIcon (Icon i) { if (i != icon) { icon = i; repaint(); } }
    Icon getIcon() const { return icon; }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        // the keys every part has (PartHeader.h), with Hawthorne's icons
        const bool on = getToggleState();
        const auto part = partui::accent ("hawthorne");
        const auto cap = partui::drawKey (g, getLocalBounds().toFloat().reduced (1.0f), on, over, down, part);
        const float sz = juce::jmin (22.0f, cap.getWidth() - 12.0f, cap.getHeight() - 10.0f);
        drawIcon (g, icon, cap.withSizeKeepingCentre (sz, sz), partui::keyInk (on, over, isEnabled(), part));
    }

private:
    Icon icon;
};

} // namespace hawthorne
