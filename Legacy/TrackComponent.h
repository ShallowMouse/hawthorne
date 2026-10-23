#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include "VintageLookAndFeel.h"
#include "Track.h"  // For TrackType enum

//==============================================================================
// Custom Arm Button - Red when armed
//==============================================================================
class ArmButton : public juce::TextButton
{
public:
    ArmButton() : juce::TextButton("A") {}
    
    void paintButton(juce::Graphics& g, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override
    {
        auto bounds = getLocalBounds().toFloat();
        
        // Background color based on toggle state
        juce::Colour bgColor;
        if (getToggleState())  // Armed
        {
            if (shouldDrawButtonAsDown)
                bgColor = juce::Colour(0xffaa3333);  // Darker red when pressed
            else if (shouldDrawButtonAsHighlighted)
                bgColor = juce::Colour(0xffdd5555);  // Lighter red when hovered
            else
                bgColor = juce::Colour(0xffcc4444);  // Bright red when armed
        }
        else  // Not armed
        {
            if (shouldDrawButtonAsDown)
                bgColor = juce::Colour(0xff333333);
            else if (shouldDrawButtonAsHighlighted)
                bgColor = juce::Colour(0xff666666);
            else
                bgColor = juce::Colour(0xff555555);  // Gray when not armed
        }
        
        // Draw button background
        g.setColour(bgColor);
        g.fillRoundedRectangle(bounds, 3.0f);
        
        // Border
        g.setColour(juce::Colour(VintageLookAndFeel::metalDark));
        g.drawRoundedRectangle(bounds, 3.0f, 1.5f);
        
        // Button text
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(12.0f, juce::Font::bold));
        g.drawText(getButtonText(), bounds, juce::Justification::centred);
    }
};

//==============================================================================
// Custom Monitor Button - Speaker icon, toggles on/off
//==============================================================================
class MonitorButton : public juce::TextButton
{
public:
    MonitorButton() : juce::TextButton("M") {}
    
    void paintButton(juce::Graphics& g, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override
    {
        auto bounds = getLocalBounds().toFloat();
        
        // Background color based on toggle state
        juce::Colour bgColor;
        if (getToggleState())  // Monitoring on
        {
            if (shouldDrawButtonAsDown)
                bgColor = juce::Colour(0xff3a7a3a);  // Darker green
            else if (shouldDrawButtonAsHighlighted)
                bgColor = juce::Colour(0xff5aaa5a);  // Lighter green
            else
                bgColor = juce::Colour(0xff4a9a4a);  // Green when monitoring
        }
        else  // Monitoring off
        {
            if (shouldDrawButtonAsDown)
                bgColor = juce::Colour(0xff333333);
            else if (shouldDrawButtonAsHighlighted)
                bgColor = juce::Colour(0xff666666);
            else
                bgColor = juce::Colour(0xff555555);  // Gray when off
        }
        
        // Draw button background
        g.setColour(bgColor);
        g.fillRoundedRectangle(bounds, 3.0f);
        
        // Border
        g.setColour(juce::Colour(VintageLookAndFeel::metalDark));
        g.drawRoundedRectangle(bounds, 3.0f, 1.5f);
        
        // Draw speaker icon
        g.setColour(juce::Colours::white);
        drawSpeakerIcon(g, bounds.reduced(6.0f));
    }
    
private:
    void drawSpeakerIcon(juce::Graphics& g, juce::Rectangle<float> bounds)
    {
        auto w = bounds.getWidth();
        auto h = bounds.getHeight();
        auto cx = bounds.getCentreX();
        auto cy = bounds.getCentreY();
        
        // Speaker cone (trapezoid)
        juce::Path speaker;
        speaker.startNewSubPath(cx - w * 0.25f, cy - h * 0.2f);  // Top left
        speaker.lineTo(cx + w * 0.05f, cy - h * 0.35f);  // Top right
        speaker.lineTo(cx + w * 0.05f, cy + h * 0.35f);  // Bottom right
        speaker.lineTo(cx - w * 0.25f, cy + h * 0.2f);   // Bottom left
        speaker.closeSubPath();
        
        g.fillPath(speaker);
        
        // Sound waves (3 short arcs)
        juce::Path waves;
        for (int i = 0; i < 3; ++i)
        {
            float offset = (i + 1) * 0.15f * w;
            float startAngle = -juce::MathConstants<float>::pi * 0.3f;
            float endAngle = juce::MathConstants<float>::pi * 0.3f;
            
            waves.addCentredArc(cx + w * 0.15f, cy, 
                               offset * 0.7f, offset * 0.7f,
                               0.0f, startAngle, endAngle, true);
        }
        
        g.strokePath(waves, juce::PathStrokeType(1.5f));
    }
};

//==============================================================================
// Custom TextEditor - Click to Edit
//==============================================================================
class ClickToEditTextEditor : public juce::TextEditor
{
public:
    std::function<void(const juce::String&)> onNameCommitted;  // fired when editing finishes

    ClickToEditTextEditor() : juce::TextEditor() {}
    
    void focusGained(FocusChangeType cause) override
    {
        setReadOnly(false);
        setCaretVisible(true);
        juce::TextEditor::focusGained(cause);
    }
    
    void focusLost(FocusChangeType cause) override
    {
        setReadOnly(true);
        setCaretVisible(false);
        juce::TextEditor::focusLost(cause);
        if (onNameCommitted)
            onNameCommitted(getText());
    }
    
    // Also commit on Return key
    bool keyPressed(const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::returnKey)
        {
            unfocusAllComponents();
            return true;
        }
        return juce::TextEditor::keyPressed(key);
    }
};

//==============================================================================
// Track Component
//==============================================================================
class TrackComponent : public juce::Component, public juce::Timer
{
public:
    TrackComponent(int trackNumber, bool isMixdownTrack = false, TrackType type = TrackType::Normal)
        : trackNum(trackNumber), isMixdown(isMixdownTrack || type == TrackType::Mixdown),
          trackType(type)
    {
        // Track number label - different text per track type
        if (type == TrackType::Click)
        {
            trackNumberLabel.setText("CLICK", juce::dontSendNotification);
            trackNumberLabel.setColour(juce::Label::textColourId, juce::Colour(VintageLookAndFeel::accentAmber));
        }
        else if (type == TrackType::Timecode)
        {
            trackNumberLabel.setText("LTC", juce::dontSendNotification);
            trackNumberLabel.setColour(juce::Label::textColourId, juce::Colour(0xff6699ff));
        }
        else if (isMixdown)
        {
            trackNumberLabel.setText("MIXDOWN (L+R)", juce::dontSendNotification);
            trackNumberLabel.setColour(juce::Label::textColourId, juce::Colour(VintageLookAndFeel::accentGreen));
        }
        else
        {
            trackNumberLabel.setText("TRACK " + juce::String(trackNumber), juce::dontSendNotification);
            trackNumberLabel.setColour(juce::Label::textColourId, juce::Colour(VintageLookAndFeel::metalDark));
        }
        trackNumberLabel.setJustificationType(juce::Justification::centred);
        trackNumberLabel.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 
                                           isMixdown ? 12.0f : 11.0f, juce::Font::bold)
                                 .withExtraKerningFactor(0.05f));
        addAndMakeVisible(trackNumberLabel);
        
        // Scribble strip (track name input) - improved with handwritten font
        trackNameEditor.setMultiLine(false);
        trackNameEditor.setReturnKeyStartsNewLine(false);
        trackNameEditor.setScrollbarsShown(false);
        trackNameEditor.setCaretVisible(true);
        trackNameEditor.setPopupMenuEnabled(true);
        
        // JUCE 8 Font API: FontOptions(float size).withName(string)
        trackNameEditor.setFont(juce::Font(juce::FontOptions(13.0f).withName("Comic Sans MS").withStyle("Bold")));
        
        // Improved colors for better readability
        trackNameEditor.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xfffffef8));
        trackNameEditor.setColour(juce::TextEditor::textColourId, juce::Colour(0xff1a1510));
        trackNameEditor.setColour(juce::TextEditor::outlineColourId, juce::Colour(VintageLookAndFeel::metalDark));
        trackNameEditor.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colour(VintageLookAndFeel::accentAmber));
        trackNameEditor.setJustification(juce::Justification::centred);
        
        // Set default name for virtual tracks
        if (type == TrackType::Click)
            trackNameEditor.setText("Click");
        else if (type == TrackType::Timecode)
            trackNameEditor.setText("LTC");
        else
            trackNameEditor.setText("");
        
        // Start in read-only mode - only editable when clicked
        trackNameEditor.setReadOnly(true);
        trackNameEditor.setCaretVisible(false);
        
        // Fire callback when user finishes editing
        trackNameEditor.onNameCommitted = [this](const juce::String& newName) {
            if (onNameChanged) onNameChanged(newName);
        };
        
        addAndMakeVisible(trackNameEditor);
        
        // Arm button — virtual tracks can't be armed
        armButton.setButtonText("A");
        armButton.setClickingTogglesState(true);
        armButton.setEnabled(type == TrackType::Normal || type == TrackType::Mixdown);
        armButton.onClick = [this] { 
            armed = armButton.getToggleState();
            juce::Logger::writeToLog("Track " + juce::String(trackNum) + " ARM button clicked - armed = " + juce::String(armed ? "TRUE" : "FALSE"));
            if (onArmChanged) onArmChanged(); 
        };
        addAndMakeVisible(armButton);
        
        // Monitor button: regular tracks → monitor; virtual tracks → enable/disable
        if (type == TrackType::Click || type == TrackType::Timecode)
        {
            monitorButton.setButtonText("ON");
            monitorButton.setClickingTogglesState(true);
            monitorButton.setToggleState(true, juce::dontSendNotification);  // ON by default
            monitorButton.onClick = [this] {
                bool active = monitorButton.getToggleState();
                juce::Logger::writeToLog("Virtual track " + juce::String(trackNum) + 
                                        " enabled = " + juce::String(active ? "YES" : "NO"));
                if (onVirtualTrackEnabled) onVirtualTrackEnabled(active);
            };
        }
        else
        {
            monitorButton.setButtonText("M");
            monitorButton.setClickingTogglesState(true);
            monitorButton.onClick = [this] { 
                monitoring = monitorButton.getToggleState();
                juce::Logger::writeToLog("Track " + juce::String(trackNum) + " MONITOR button clicked - monitoring = " + juce::String(monitoring ? "ON" : "OFF"));
                if (onMonitorChanged) onMonitorChanged();
            };
        }
        addAndMakeVisible(monitorButton);
        
        // Start timer for waveform animation
        startTimer(50);
    }
    
    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds();
        
        // Track background - dark gradient
        juce::ColourGradient trackGradient(
            juce::Colour(0xff2a2620), 0.0f, 0.0f,
            juce::Colour(VintageLookAndFeel::bgTrack), 0.0f, (float)bounds.getHeight(), false);
        g.setGradientFill(trackGradient);
        g.fillRoundedRectangle(bounds.toFloat(), 6.0f);
        
        // Track border
        g.setColour(juce::Colour(VintageLookAndFeel::metalDark));
        g.drawRoundedRectangle(bounds.toFloat(), 6.0f, 2.0f);
        
        // Top highlight
        g.setColour(juce::Colours::white.withAlpha(0.05f));
        g.drawRoundedRectangle(bounds.reduced(2).toFloat(), 4.0f, 1.0f);
        
        // Paint track header with beige/cream gradient
        auto headerBounds = bounds.removeFromTop(76).reduced(6, 6);
        juce::ColourGradient headerGradient(
            juce::Colour(VintageLookAndFeel::tapeBeige), 0.0f, 0.0f,
            juce::Colour(0xffc4b49a), 0.0f, (float)headerBounds.getHeight(), false);
        g.setGradientFill(headerGradient);
        g.fillRoundedRectangle(headerBounds.toFloat(), 4.0f);
        
        // Header border
        g.setColour(juce::Colour(VintageLookAndFeel::metalDark));
        g.drawRoundedRectangle(headerBounds.toFloat(), 4.0f, 2.0f);
        
        // Header top highlight
        g.setColour(juce::Colours::white.withAlpha(0.3f));
        g.drawLine((float)headerBounds.getX() + 4, (float)headerBounds.getY() + 2,
                   (float)headerBounds.getRight() - 4, (float)headerBounds.getY() + 2, 1.0f);
        
        // Waveform area - black background
        auto waveArea = bounds.removeFromTop(bounds.getHeight() - 60).reduced(4, 4);
        g.setColour(juce::Colour(0xff0a0a08));
        g.fillRoundedRectangle(waveArea.toFloat(), 4.0f);
        
        // Waveform area border
        g.setColour(juce::Colour(VintageLookAndFeel::metalDark));
        g.drawRoundedRectangle(waveArea.toFloat(), 4.0f, 2.0f);
        
        // Inset shadow
        g.setColour(juce::Colours::black.withAlpha(0.6f));
        g.drawRoundedRectangle(waveArea.reduced(2).toFloat(), 2.0f, 1.0f);
        
        // ALWAYS draw tape background (full width)
        // This gives the look of magnetic tape in the track
        g.setColour(juce::Colour(0xff2a2418).withAlpha(0.6f));  // Brown "tape" color
        g.fillRect(waveArea);
        
        // ---- dB grid lines (drawn per-half for mixdown, always clipped to waveArea) ----
        {
            g.saveState();
            g.reduceClipRegion(waveArea);

            // Draw grid within one sub-area (full track for mono, each half for mixdown)
            auto drawGrid = [&](juce::Rectangle<int> area)
            {
                const float cx    = (float)area.getCentreX();
                const float wy    = (float)area.getY();
                const float wh    = (float)area.getHeight();
                // Apply amplitude zoom to dB grid scale
                double ampZoom = settings ? settings->getAmplitudeZoom() : 1.0;
                const float scale = area.getWidth() * 0.28f * (float)ampZoom;   // 28% → 56% span, scaled by zoom

                struct DbLine { float dB; uint32_t colour; const char* label; };
                const DbLine lines[] = {
                    { -6.0f,  0x30ff4444, "-6"  },
                    { -12.0f, 0x28ffaa00, "-12" },
                    { -18.0f, 0x2200dd00, "-18" },
                };

                g.setFont(juce::Font(juce::FontOptions(7.0f)));

                for (auto& ln : lines)
                {
                    float amp  = std::pow(10.0f, ln.dB / 20.0f);
                    float xOff = amp * scale;

                    g.setColour(juce::Colour(ln.colour));
                    g.drawVerticalLine(juce::roundToInt(cx + xOff), wy, wy + wh);
                    g.drawVerticalLine(juce::roundToInt(cx - xOff), wy, wy + wh);
                    // Label on right side only (avoids clutter)
                    g.drawText(ln.label,
                               juce::roundToInt(cx + xOff) - 8, (int)wy + 2, 16, 8,
                               juce::Justification::centred, false);
                }
            };

            if (isMixdown)
            {
                drawGrid(waveArea.withRight(waveArea.getCentreX() - 1)); // L half
                drawGrid(waveArea.withLeft (waveArea.getCentreX() + 1)); // R half
            }
            else
            {
                drawGrid(waveArea);
            }

            g.restoreState();
        }

        // Mixdown only: draw L/R divider and channel labels
        if (isMixdown)
        {
            const int midX = waveArea.getCentreX();
            g.setColour(juce::Colour(0xff3a3530));
            g.drawVerticalLine(midX, (float)waveArea.getY(), (float)waveArea.getBottom());
            g.setColour(juce::Colour(0xff667766));
            g.setFont(juce::Font(juce::FontOptions(9.0f)));
            g.drawText("L", waveArea.withRight(midX).withHeight(14), juce::Justification::centred, false);
            g.drawText("R", waveArea.withLeft(midX).withHeight(14), juce::Justification::centred, false);
        }
        // ---- end dB grid ----
        
        // Draw waveform if we have data
        if (!waveformPeaks.empty())
        {
            // Calculate playhead position as fraction (0.0 to 1.0)
            float playheadFraction = 0.7f;  // Playhead at 30% from BOTTOM (70% from top)
            
            // Match timeline scroll speed with zoom applied
            double zoom = settings ? settings->getWaveformZoom() : 1.0;
            double pixelsPerSecond = 60.0 * zoom;  // Base 60px/sec scaled by zoom
            double samplesPerPixel = sampleRate / pixelsPerSecond;
            
            if (audioTotalSamples > 0)
            {
                // Calculate which part of audio is visible
                // Playhead is at 70% down from top, showing audioCurrentSample at that Y position
                double centerSample = audioCurrentSample;
                
                // Check scroll direction from settings
                bool scrollDown = settings ? settings->getWaveformScrollDown() : true;
                
                // Calculate startSample (what appears at y=0, top of display)
                // Formula: samplePos(y) = startSample - (y * samplesPerPixel) for down
                //          samplePos(y) = startSample + (y * samplesPerPixel) for up
                double startSample;
                if (scrollDown)
                {
                    // Down: At playhead (y = height*0.7), show centerSample
                    // centerSample = startSample - (height * 0.7 * samplesPerPixel)
                    // Therefore: startSample = centerSample + (0.7 * height * samplesPerPixel)
                    startSample = centerSample + (playheadFraction * waveArea.getHeight() * samplesPerPixel);
                }
                else
                {
                    // Up: At playhead (y = height*0.7), show centerSample  
                    // centerSample = startSample + (height * 0.7 * samplesPerPixel)
                    // Therefore: startSample = centerSample - (0.7 * height * samplesPerPixel)
                    startSample = centerSample - (playheadFraction * waveArea.getHeight() * samplesPerPixel);
                }

                // For mixdown: draw waveform clipped to each half (L left, R right)
                // Both halves show the same peaks for now — separate L/R data coming later
                int numHalves = isMixdown ? 2 : 1;
                for (int half = 0; half < numHalves; ++half)
                {
                    auto halfArea = isMixdown
                        ? (half == 0 ? waveArea.withRight(waveArea.getCentreX() - 1)
                                     : waveArea.withLeft(waveArea.getCentreX() + 1))
                        : waveArea;

                    g.setColour(half == 1 ? juce::Colour(0xff4a7a9a).withAlpha(0.7f)   // R: blue-green
                                          : juce::Colour(0xff4a9a4a).withAlpha(0.7f)); // L/mono: green
                    const float cx     = (float)halfArea.getCentreX();
                    // Apply amplitude zoom to waveform scale
                    double ampZoom = settings ? settings->getAmplitudeZoom() : 1.0;
                    const float scaleX = halfArea.getWidth() * 0.28f * (float)ampZoom;

                    g.saveState();
                    g.reduceClipRegion(halfArea);

                    for (int y = 0; y < waveArea.getHeight(); ++y)
                    {
                        // Calculate sample position based on scroll direction
                        double samplePos = scrollDown
                            ? startSample - (y * samplesPerPixel)  // Down: y=0 is future
                            : startSample + (y * samplesPerPixel);  // Up: y=0 is past
                        
                        if (samplePos >= 0 && samplePos < audioTotalSamples)
                        {
                            int peakIndex = static_cast<int>((samplePos / audioTotalSamples) * waveformPeaks.size());
                            if (peakIndex >= 0 && peakIndex < (int)waveformPeaks.size())
                            {
                                float peak = waveformPeaks[peakIndex];
                                float waveWidth = peak * scaleX;
                                float yPos = waveArea.getY() + y;
                                g.drawLine(cx - waveWidth, yPos, cx + waveWidth, yPos, 1.0f);
                            }
                        }
                    }

                    g.restoreState();
                }
            }
        }
        else if (recording)
        {
            // During recording, show live incoming waveform using the same
            // scrolling-window logic as playback: newest sample is at the
            // playhead, older audio scrolls upward above it, nothing below.
            
            if (!recordingWaveform.empty())
            {
                float playheadFraction = 0.7f;  // Playhead at 30% from BOTTOM (70% from top)
                
                // Match timeline scroll speed with zoom applied
                double zoom = settings ? settings->getWaveformZoom() : 1.0;
                double pixelsPerSecond = 60.0 * zoom;  // Base 60px/sec scaled by zoom
                double samplesPerPixel = sampleRate / pixelsPerSecond;
                
                // Check scroll direction from settings
                bool scrollDown = settings ? settings->getWaveformScrollDown() : true;
                
                // Calculate startSample based on scroll direction
                double startSample;
                if (scrollDown)
                {
                    // Down: At playhead (70% from top), show current audio position
                    startSample = audioCurrentSample + (playheadFraction * waveArea.getHeight() * samplesPerPixel);
                }
                else
                {
                    // Up: At playhead (70% from top), show current audio position
                    startSample = audioCurrentSample - (playheadFraction * waveArea.getHeight() * samplesPerPixel);
                }
                
                int numRecSamples = (int)recordingWaveform.size();
                // Oldest sample available in the ring buffer
                double oldestAvailable = audioCurrentSample - (double)numRecSamples;
                
                // --- Pass 1: reddish tape background tint where we have audio ---
                g.setColour(juce::Colour(0xff3a2418).withAlpha(0.5f));
                
                for (int y = 0; y < waveArea.getHeight(); ++y)
                {
                    // Calculate sample position based on scroll direction
                    double samplePos = scrollDown
                        ? startSample - ((double)y * samplesPerPixel)  // Down: y=0 is future
                        : startSample + ((double)y * samplesPerPixel);  // Up: y=0 is past
                    
                    // Only render past audio (before current position)
                    if (samplePos >= oldestAvailable && samplePos < audioCurrentSample)
                    {
                        int bufferIndex = (int)(samplePos - oldestAvailable);
                        if (bufferIndex >= 0 && bufferIndex < numRecSamples)
                        {
                            float amplitude = std::abs(recordingWaveform[bufferIndex]);
                            if (amplitude > 0.001f)
                            {
                                float yPos = (float)waveArea.getY() + (float)y;
                                g.drawLine((float)waveArea.getX(), yPos,
                                          (float)waveArea.getRight(), yPos, 1.0f);
                            }
                        }
                    }
                }
                
                // --- Pass 2: red recording waveform ---
                g.setColour(juce::Colour(VintageLookAndFeel::accentRed).withAlpha(0.7f));
                
                g.saveState();
                g.reduceClipRegion(waveArea);
                for (int y = 0; y < waveArea.getHeight(); ++y)
                {
                    // Calculate sample position based on scroll direction
                    double samplePos = scrollDown
                        ? startSample - ((double)y * samplesPerPixel)  // Down: y=0 is future
                        : startSample + ((double)y * samplesPerPixel);  // Up: y=0 is past
                    
                    if (samplePos >= oldestAvailable && samplePos < audioCurrentSample)
                    {
                        int bufferIndex = (int)(samplePos - oldestAvailable);
                        if (bufferIndex >= 0 && bufferIndex < numRecSamples)
                        {
                            float amplitude = std::abs(recordingWaveform[bufferIndex]);
                            
                            float centerX = (float)waveArea.getCentreX();
                            // Apply amplitude zoom to recording waveform
                            double ampZoom = settings ? settings->getAmplitudeZoom() : 1.0;
                            float waveWidth = amplitude * ((float)waveArea.getWidth() * 0.28f * (float)ampZoom);
                            
                            float leftX  = centerX - waveWidth;
                            float rightX = centerX + waveWidth;
                            float yPos   = (float)waveArea.getY() + (float)y;
                            
                            g.drawLine(leftX, yPos, rightX, yPos, 1.0f);
                        }
                    }
                }
                g.restoreState();
            }
            else
            {
                // Fallback: simple level indicator drawn AT the playhead
                float centerX = (float)waveArea.getCentreX();
                float indicatorWidth = level * ((float)waveArea.getWidth() * 0.24f);
                float playheadY = (float)waveArea.getY() + ((float)waveArea.getHeight() * 0.7f);  // 30% from bottom
                
                g.setColour(juce::Colour(VintageLookAndFeel::accentRed).withAlpha(0.7f));
                g.drawLine(centerX - indicatorWidth, playheadY,
                          centerX + indicatorWidth, playheadY, 2.0f);
            }
        }
        
        // Draw scrolling tape reference lines (HORIZONTAL)
        if (isPlaying)
        {
            g.setColour(juce::Colour(0xff1a1a18).withAlpha(0.3f));
            
            // Draw horizontal reference lines that scroll down at 60 pixels/second
            // This creates the illusion of tape moving under the playhead
            int numLines = 8;
            float lineSpacing = waveArea.getHeight() / (float)numLines;
            // playbackPosition loops 0.0-1.0 every 10 seconds
            // Multiply by 6.0 to get ~60 pixels/second scroll speed
            float scrollOffset = fmod(playbackPosition * 6.0f * lineSpacing, lineSpacing);
            
            for (int i = -1; i < numLines + 1; ++i)
            {
                float lineY = waveArea.getY() + (i * lineSpacing) - scrollOffset;
                if (lineY >= waveArea.getY() && lineY <= waveArea.getBottom())
                {
                    g.drawLine((float)waveArea.getX(), lineY,
                             (float)waveArea.getRight(), lineY, 1.0f);
                }
            }
        }
        
        // Draw musical time grid (if enabled)
        if (settings && settings->getGridEnabled())
        {
            // Get current position in seconds
            double currentTime = audioCurrentSample / sampleRate;
            
            // Calculate grid parameters based on BPM and resolution
            double secondsPerBeat = 60.0 / currentBPM;
            double secondsPerBar = secondsPerBeat * 4.0;  // Assuming 4/4 time
            
            // Grid interval based on resolution setting
            double gridInterval;
            int gridResolution = settings->getGridResolution();
            switch (gridResolution)
            {
                case 0: gridInterval = secondsPerBar;        break;  // Bars
                case 1: gridInterval = secondsPerBeat;       break;  // Beats (1/4 notes)
                case 2: gridInterval = secondsPerBeat / 2.0; break;  // 1/8 notes
                case 3: gridInterval = secondsPerBeat / 4.0; break;  // 1/16 notes
                case 4: gridInterval = secondsPerBeat / 8.0; break;  // 1/32 notes
                default: gridInterval = secondsPerBeat;      break;
            }
            
            // Get scroll parameters (matching waveform logic)
            double zoom = settings->getWaveformZoom();
            double pixelsPerSecond = 60.0 * zoom;
            bool scrollDown = settings->getWaveformScrollDown();
            float playheadFraction = 0.7f;
            
            // Calculate Y position of playhead
            float playheadY = (float)waveArea.getY() + ((float)waveArea.getHeight() * playheadFraction);
            
            // Find the first grid line to draw
            double firstGridTime = std::floor(currentTime / gridInterval) * gridInterval;
            
            // Draw grid lines extending from playhead position
            int maxLines = 100;  // Safety limit
            for (int i = -maxLines; i < maxLines; ++i)
            {
                double gridTime = firstGridTime + (i * gridInterval);
                if (gridTime < 0.0) continue;
                
                // Calculate distance from current time
                double timeDelta = gridTime - currentTime;
                
                // Convert to Y position based on scroll direction
                double yPos = scrollDown
                    ? playheadY - (timeDelta * pixelsPerSecond)   // Down: future above
                    : playheadY + (timeDelta * pixelsPerSecond);  // Up: future below
                
                // Only draw if on screen
                if (yPos < waveArea.getY() - 10 || yPos > waveArea.getBottom() + 10)
                    continue;
                
                // Draw grid line with different colors for different divisions
                bool isBar = (std::abs(std::fmod(gridTime, secondsPerBar)) < 0.001);
                bool isBeat = (std::abs(std::fmod(gridTime, secondsPerBeat)) < 0.001);
                
                if (isBar)
                {
                    // Bar lines - brighter green
                    g.setColour(juce::Colour(VintageLookAndFeel::accentGreen).withAlpha(0.4f));
                    g.drawLine((float)waveArea.getX(), (float)yPos,
                              (float)waveArea.getRight(), (float)yPos, 2.0f);
                }
                else if (isBeat && gridResolution > 1)
                {
                    // Beat lines - medium green
                    g.setColour(juce::Colour(VintageLookAndFeel::accentGreen).withAlpha(0.25f));
                    g.drawLine((float)waveArea.getX(), (float)yPos,
                              (float)waveArea.getRight(), (float)yPos, 1.5f);
                }
                else
                {
                    // Subdivision lines - subtle green
                    g.setColour(juce::Colour(VintageLookAndFeel::accentGreen).withAlpha(0.15f));
                    g.drawLine((float)waveArea.getX(), (float)yPos,
                              (float)waveArea.getRight(), (float)yPos, 1.0f);
                }
            }
        }
        
        // HORIZONTAL playhead - ALWAYS VISIBLE on all tracks
        // Color changes based on state
        {
            // Fixed position at 30% from BOTTOM edge (70% from top) - tape head location
            float playheadY = waveArea.getY() + (waveArea.getHeight() * 0.7f);
            
            // Choose color based on state
            juce::Colour playheadColor;
            
            if (recording)
            {
                // SOLID RED when recording
                playheadColor = juce::Colour(VintageLookAndFeel::accentRed);
            }
            else if (armed)
            {
                // PULSING RED when armed (whether playing or stopped)
                float pulseAlpha = 0.5f + 0.5f * std::sin(pulsePhase);
                playheadColor = juce::Colour(VintageLookAndFeel::accentRed).withAlpha(pulseAlpha);
            }
            else
            {
                // WHITE for normal playback or stopped
                playheadColor = juce::Colours::white.withAlpha(0.9f);
            }
            
            g.setColour(playheadColor);
            
            // Draw STATIC HORIZONTAL line (tape head) - LEFT TO RIGHT
            float leftX = (float)waveArea.getX();
            float rightX = (float)waveArea.getRight();
            
            g.drawLine(leftX, playheadY, rightX, playheadY, 2.5f);
            
            // Draw small triangle at left to indicate tape head position
            juce::Path arrow;
            float arrowSize = 6.0f;
            arrow.addTriangle(leftX - 8, playheadY - arrowSize,
                            leftX - 8, playheadY + arrowSize,
                            leftX - 2, playheadY);
            g.fillPath(arrow);
        }
    }
    
    void paintOverChildren(juce::Graphics& g) override
    {
        // 3-LED VU meter helper — draws one meter in the given area for the given linear level
        auto drawVUMeter = [&](juce::Rectangle<int> vuArea, float linLevel, const char* chanLabel)
        {
            // Panel background
            juce::ColourGradient vuGradient(
                juce::Colour(0xff2a2620), juce::Point<float>(0.0f, 0.0f),
                juce::Colour(0xff1a1512), juce::Point<float>(0.0f, (float)vuArea.getHeight()),
                false);
            g.setGradientFill(vuGradient);
            g.fillRoundedRectangle(vuArea.toFloat(), 4.0f);
            g.setColour(juce::Colour(VintageLookAndFeel::metalDark));
            g.drawRoundedRectangle(vuArea.toFloat(), 4.0f, 2.0f);

            // Channel label (L / R)
            if (chanLabel && chanLabel[0] != '\0')
            {
                g.setColour(juce::Colour(0xff887766));
                g.setFont(juce::Font(juce::FontOptions(8.0f)));
                g.drawText(chanLabel, vuArea.withHeight(12), juce::Justification::centred, false);
            }

            // LEDs
            auto meterArea = vuArea.reduced(12, 10);
            if (chanLabel && chanLabel[0] != '\0')
                meterArea.removeFromTop(8);   // make room for label

            const int ledW = meterArea.getWidth() / 3;
            auto led1 = meterArea.withWidth(ledW).reduced(2, 2);
            auto led2 = meterArea.withX(meterArea.getX() + ledW).withWidth(ledW).reduced(2, 2);
            auto led3 = meterArea.withX(meterArea.getX() + ledW * 2).withWidth(ledW).reduced(2, 2);

            float dB = (linLevel > 0.00001f) ? 20.0f * std::log10(linLevel) : -100.0f;
            bool on1 = linLevel > 0.001f;
            bool on2 = dB > -18.0f;
            bool on3 = dB > -6.0f;

            struct LedDef { juce::Rectangle<int> area; bool on; uint32_t onCol; uint32_t offCol; };
            LedDef leds[3] = {
                { led1, on1, 0xff00ff00, 0xff003300 },
                { led2, on2, 0xffffaa00, 0xff332200 },
                { led3, on3, 0xffff0000, 0xff330000 },
            };
            for (auto& led : leds)
            {
                if (led.on)
                {
                    g.setColour(juce::Colour(led.onCol));
                    g.fillRoundedRectangle(led.area.toFloat(), 3.0f);
                    g.setColour(juce::Colour(led.onCol).withAlpha(0.3f));
                    g.fillRoundedRectangle(led.area.expanded(2).toFloat(), 4.0f);
                }
                else
                {
                    g.setColour(juce::Colour(led.offCol));
                    g.fillRoundedRectangle(led.area.toFloat(), 3.0f);
                }
                g.setColour(juce::Colour(VintageLookAndFeel::metalDark));
                g.drawRoundedRectangle(led.area.toFloat(), 3.0f, 1.0f);
            }
        };

        auto bounds = getLocalBounds();
        auto vuStrip = bounds.removeFromBottom(60).reduced(8, 8);

        if (isMixdown)
        {
            // Two meters side by side: L on left, R on right, with 4px gap
            auto gap = 4;
            auto half = (vuStrip.getWidth() - gap) / 2;
            auto leftMeter  = vuStrip.withWidth(half);
            auto rightMeter = vuStrip.withX(vuStrip.getX() + half + gap).withWidth(vuStrip.getWidth() - half - gap);
            drawVUMeter(leftMeter,  vuLevel,      "L");
            drawVUMeter(rightMeter, vuLevelRight, "R");
        }
        else
        {
            drawVUMeter(vuStrip, vuLevel, "");
        }
    }
    
    void resized() override
    {
        auto bounds = getLocalBounds();
        
        // Skip VU area at bottom
        bounds.removeFromBottom(60);
        
        // Track header at top
        auto header = bounds.removeFromTop(76);
        
        // Header background painted separately
        header.reduce(6, 6);
        
        trackNumberLabel.setBounds(header.removeFromTop(18));
        
        // Scribble strip
        auto scribbleArea = header.removeFromTop(24).reduced(4, 2);
        trackNameEditor.setBounds(scribbleArea);
        
        // Arm/Monitor buttons
        auto buttonArea = header.removeFromTop(28).reduced(8, 0);
        auto buttonW = (buttonArea.getWidth() - 3) / 2;
        armButton.setBounds(buttonArea.removeFromLeft(buttonW));
        buttonArea.removeFromLeft(3);
        monitorButton.setBounds(buttonArea);
    }
    
    void paintHeader(juce::Graphics& g, juce::Rectangle<int> headerBounds)
    {
        // Paint track header with beige/cream gradient
        juce::ColourGradient headerGradient(
            juce::Colour(VintageLookAndFeel::tapeBeige), 0.0f, 0.0f,
            juce::Colour(0xffc4b49a), 0.0f, (float)headerBounds.getHeight(), false);
        g.setGradientFill(headerGradient);
        g.fillRoundedRectangle(headerBounds.toFloat(), 4.0f);
        
        // Header border
        g.setColour(juce::Colour(VintageLookAndFeel::metalDark));
        g.drawRoundedRectangle(headerBounds.toFloat(), 4.0f, 2.0f);
        
        // Top highlight
        g.setColour(juce::Colours::white.withAlpha(0.3f));
        g.drawLine((float)headerBounds.getX() + 4, (float)headerBounds.getY() + 2,
                   (float)headerBounds.getRight() - 4, (float)headerBounds.getY() + 2, 1.0f);
    }
    
    void timerCallback() override
    {
        // Update pulse phase for armed state
        pulsePhase += 0.15f;
        if (pulsePhase > juce::MathConstants<float>::twoPi)
            pulsePhase -= juce::MathConstants<float>::twoPi;
        
        repaint();
    }
    
    void setArmed(bool shouldArm)
    {
        armed = shouldArm;
        armButton.setToggleState(shouldArm, juce::dontSendNotification);
    }
    
    void setRecording(bool isRecording)
    {
        recording = isRecording;
        repaint();  // Repaint to show recording state in playhead
    }
    
    void setLevel(float newLevel)
    {
        level = newLevel;
        vuLevel = newLevel;  // Direct assign - audio engine handles decay
    }

    void setLevelRight(float newLevel)
    {
        vuLevelRight = newLevel;
    }
    
    // Get current VU LED states (for OSC output)
    void getVULedStates(bool& led1, bool& led2, bool& led3)
    {
        // Calculate dB from linear amplitude
        float dB = -100.0f;  // Default very quiet
        if (vuLevel > 0.00001f)
        {
            dB = 20.0f * std::log10(vuLevel);
        }
        
        // LED thresholds (same as paintOverChildren)
        led1 = vuLevel > 0.001f;  // Any signal (> -60dB)
        led2 = dB > -18.0f;        // Over -18dB (amber threshold)
        led3 = dB > -6.0f;         // Over -6dB (red/hot threshold)
    }
    
    void setPlaybackState(bool playing, float position)
    {
        isPlaying = playing;
        playbackPosition = juce::jlimit(0.0f, 1.0f, position);
    }
    
    void setWaveformData(const std::vector<float>& peaks, double currentSample, double totalSamples)
    {
        waveformPeaks = peaks;
        audioCurrentSample = currentSample;
        audioTotalSamples = totalSamples;
    }
    
    void setSettings(Settings* s)
    {
        settings = s;
    }
    
    void setBPM(double bpm)
    {
        currentBPM = bpm;
    }
    
    bool isArmed() const { return armed; }
    
    // Sync armed state from AudioEngine (called by timer 30fps, no logging)
    void setArmedState(bool shouldBeArmed)
    {
        if (armed != shouldBeArmed)
        {
            armed = shouldBeArmed;
            armButton.setToggleState(armed, juce::dontSendNotification);
            repaint();
        }
    }
    
    // Sync monitoring state (called by timer 30fps, no logging)
    void setMonitoringState(bool shouldBeMonitoring)
    {
        if (monitoring != shouldBeMonitoring)
        {
            monitoring = shouldBeMonitoring;
            monitorButton.setToggleState(monitoring, juce::dontSendNotification);
            repaint();
        }
    }
    
    bool isMonitoring() const { return monitoring; }
    
    // Set recording waveform data for real-time display
    void setRecordingWaveform(const std::vector<float>& waveform)
    {
        recordingWaveform = waveform;
    }
    
    std::function<void()> onArmChanged;
    std::function<void()> onMonitorChanged;
    std::function<void(bool)> onVirtualTrackEnabled;  // Click/Timecode ON/OFF
    std::function<void(const juce::String&)> onNameChanged;  // fired on commit

    juce::String getTrackName() const { return trackNameEditor.getText(); }
    void setTrackName(const juce::String& name) { trackNameEditor.setText(name, false); }
    
    // VU LED states for OSC output (public so TapeUI can check them)
    bool lastLed1State = false;
    bool lastLed2State = false;
    bool lastLed3State = false;

private:
    int trackNum;
    bool isMixdown = false;
public:
    bool isMixdownTrack() const { return isMixdown; }
    TrackType trackType = TrackType::Normal;
    juce::Label trackNumberLabel;
    ClickToEditTextEditor trackNameEditor;
    ArmButton armButton;
    MonitorButton monitorButton;
    
    bool armed = false;
    bool recording = false;
    bool monitoring = false;
    bool isPlaying = false;
    float level = 0.0f;
    float vuLevel = 0.0f;
    float vuLevelRight = 0.0f;   // R-channel peak for stereo/mixdown tracks
    float pulsePhase = 0.0f;  // For pulsing armed playhead
    float playbackPosition = 0.0f;  // 0.0 to 1.0 (left to right)
    
    
    // Waveform data
    std::vector<float> waveformPeaks;
    double audioCurrentSample = 0.0;
    double audioTotalSamples = 0.0;
    double sampleRate = 48000.0;
    double currentBPM = 120.0;
    Settings* settings = nullptr;
    
    // Recording waveform for real-time display
    std::vector<float> recordingWaveform;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrackComponent)
};
