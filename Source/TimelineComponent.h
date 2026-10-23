#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "VintageLookAndFeel.h"

class AudioEngine;  // Forward declaration

class TimelineComponent : public juce::Component
{
public:
    TimelineComponent()
    {
        setInterceptsMouseClicks(true, false);
    }
    
    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds();
        
        // Background
        g.fillAll(juce::Colour(VintageLookAndFeel::bgPanel));
        
        // Right border
        g.setColour(juce::Colour(VintageLookAndFeel::metalDark));
        g.drawLine(bounds.getWidth() - 1, 0, bounds.getWidth() - 1, bounds.getHeight(), 2.0f);
        
        // Calculate playhead Y position to match TrackComponent EXACTLY
        // TrackComponent has: 76px header, then waveform area (reduced by 4px), then 60px VU meter
        // Playhead is at waveArea.getY() + (waveArea.getHeight() * 0.3f)
        // NOTE: Tracks have 20px padding top/bottom in the viewport, so track height = bounds - 40
        int padding = 20;
        int totalHeight = bounds.getHeight() - (padding * 2);  // Match track height
        int headerHeight = 76;
        int vuHeight = 60;
        int reduction = 4;  // reduced(4, 4) on waveArea
        
        // Waveform area starts after header + reduction + top padding
        int waveAreaY = padding + headerHeight + reduction;
        // Waveform area height = total - header - VU - (reduction * 2 for top and bottom)
        int waveAreaHeight = totalHeight - headerHeight - vuHeight - (reduction * 2);
        
        // Playhead at 30% from BOTTOM (70% down from top) - tape head position
        float playheadY = waveAreaY + (waveAreaHeight * 0.7f);
        
        // Smooth scrolling: offset numbers by fractional second
        double wholeSecond = std::floor(currentTime);
        double fractionalSecond = currentTime - wholeSecond;
        
        // Pixels per second (controls zoom/speed of scroll)
        double zoom = settings ? settings->getWaveformZoom() : 1.0;
        double pixelsPerSecond = 60.0 * zoom;  // Base 60px/sec scaled by zoom
        
        // Smooth scroll offset (moves numbers UP as time increases)
        double scrollOffset = fractionalSecond * pixelsPerSecond;
        
        // Draw time markers
        g.setFont(juce::Font(12.0f, juce::Font::bold));
        
        // Calculate range of seconds to draw
        int visibleRange = 50;  // Draw seconds well beyond visible area
        int startSecond = (int)wholeSecond - visibleRange;
        int endSecond = (int)wholeSecond + visibleRange;
        
        for (int sec = startSecond; sec <= endSecond; sec++)
        {
            if (sec < 0) continue;  // No negative times
            
            // Calculate Y position for this second marker
            // Distance from current time in seconds
            double secondsFromCurrent = sec - currentTime;
            
            // Y position based on scroll direction setting
            bool scrollDown = settings ? settings->getWaveformScrollDown() : true;
            double yPos = scrollDown 
                ? playheadY - (secondsFromCurrent * pixelsPerSecond)  // Down: future above
                : playheadY + (secondsFromCurrent * pixelsPerSecond);  // Up: future below
            
            // Only draw if on screen
            if (yPos < -20 || yPos > bounds.getHeight() + 20) continue;
            
            // Draw main second tick mark
            g.setColour(juce::Colour(VintageLookAndFeel::panelCream));
            g.drawLine(bounds.getWidth() - 20, yPos, bounds.getWidth() - 5, yPos, 2.0f);
            
            // Draw time label (MM:SS)
            juce::String label = formatTime((double)sec);
            g.drawText(label, 2, yPos - 8, bounds.getWidth() - 24, 16, 
                      juce::Justification::centredRight);
            
            // Draw tenth-second tick marks between this second and next
            for (int tenth = 1; tenth < 10; tenth++)
            {
                double tenthYPos = yPos + (tenth * pixelsPerSecond / 10.0);
                
                if (tenthYPos >= 0 && tenthYPos <= bounds.getHeight())
                {
                    g.setColour(juce::Colour(VintageLookAndFeel::panelCream).withAlpha(0.3f));
                    g.drawLine(bounds.getWidth() - 12, tenthYPos, 
                              bounds.getWidth() - 5, tenthYPos, 1.0f);
                }
            }
        }
        
        // Draw markers (before playhead so playhead is on top)
        if (audioEngine)
        {
            auto* markerManager = audioEngine->getMarkerManager();
            if (markerManager)
            {
                const auto& markers = markerManager->getMarkers();
                
                for (const auto& marker : markers)
                {
                    double markerTime = marker->getTimestamp();
                    
                    // Calculate Y position for marker
                    double secondsFromCurrent = markerTime - currentTime;
                    bool scrollDown = settings ? settings->getWaveformScrollDown() : true;
                    double yPos = scrollDown
                        ? playheadY - (secondsFromCurrent * pixelsPerSecond)  // Down: future above
                        : playheadY + (secondsFromCurrent * pixelsPerSecond);  // Up: future below
                    
                    // Only draw if on screen
                    if (yPos < -20 || yPos > bounds.getHeight() + 20) continue;
                    
                    // Draw vertical marker line (orange/amber)
                    g.setColour(juce::Colour(VintageLookAndFeel::accentRed).withAlpha(0.8f));
                    g.drawLine(0, yPos, bounds.getWidth() - 25, yPos, 2.0f);
                    
                    // Draw marker number and label
                    juce::String markerText = juce::String(marker->getNumber());
                    if (marker->getLabel().isNotEmpty())
                        markerText += " " + marker->getLabel();
                    
                    // Background for marker text
                    int textWidth = g.getCurrentFont().getStringWidth(markerText) + 8;
                    g.setColour(juce::Colour(VintageLookAndFeel::accentRed).withAlpha(0.9f));
                    g.fillRect(2, (int)yPos - 10, textWidth, 20);
                    
                    // Marker text
                    g.setColour(juce::Colours::white);
                    g.setFont(juce::Font(10.0f, juce::Font::bold));
                    g.drawText(markerText, 2, (int)yPos - 8, textWidth, 16,
                              juce::Justification::centredLeft);
                }
            }
        }
        
        // Draw playhead indicator line (horizontal bar)
        g.setColour(juce::Colour(VintageLookAndFeel::accentAmber));
        g.fillRect(0, (int)playheadY - 1, bounds.getWidth(), 3);
        
        // Draw current time value at playhead
        g.setFont(juce::Font(11.0f, juce::Font::bold));
        juce::String currentLabel = formatTime(currentTime);
        
        // Background box for current time
        int labelWidth = 46;
        g.setColour(juce::Colour(VintageLookAndFeel::accentAmber).withAlpha(0.95f));
        g.fillRect(1, (int)playheadY - 10, labelWidth, 20);
        
        // Current time text
        g.setColour(juce::Colour(VintageLookAndFeel::bgDark));
        g.drawText(currentLabel, 1, (int)playheadY - 8, labelWidth, 16,
                  juce::Justification::centred);
    }
    
    void mouseDown(const juce::MouseEvent& event) override
    {
        handleScrub(event);
    }
    
    void mouseDrag(const juce::MouseEvent& event) override
    {
        handleScrub(event);
    }
    
    void setCurrentTime(double time)
    {
        if (currentTime != time)
        {
            currentTime = time;
            repaint();
        }
    }
    
    void setAudioEngine(AudioEngine* engine)
    {
        audioEngine = engine;
    }
    
    void setSettings(Settings* s)
    {
        settings = s;
    }
    
    double getCurrentTime() const { return currentTime; }
    
private:
    double currentTime = 0.0;
    AudioEngine* audioEngine = nullptr;
    Settings* settings = nullptr;
    double pixelsPerSecond = 60.0;
    
    void handleScrub(const juce::MouseEvent& event)
    {
        if (!audioEngine) return;
        
        // Calculate playhead Y position (same as in paint())
        auto bounds = getLocalBounds();
        int padding = 20;
        int totalHeight = bounds.getHeight() - (padding * 2);  // Match track height
        int headerHeight = 76;
        int vuHeight = 60;
        int reduction = 4;
        
        int waveAreaY = padding + headerHeight + reduction;
        int waveAreaHeight = totalHeight - headerHeight - vuHeight - (reduction * 2);
        float playheadY = waveAreaY + (waveAreaHeight * 0.7f);  // 30% from bottom
        
        // Calculate time from mouse Y position
        // Distance from playhead in pixels
        double pixelOffset = event.position.y - playheadY;
        
        // Pixels per second with zoom applied
        double zoom = settings ? settings->getWaveformZoom() : 1.0;
        double pixelsPerSecond = 60.0 * zoom;
        
        // Convert to seconds based on scroll direction
        bool scrollDown = settings ? settings->getWaveformScrollDown() : true;
        double timeOffset = scrollDown
            ? -pixelOffset / pixelsPerSecond   // Down: above = negative offset = forward
            : pixelOffset / pixelsPerSecond;    // Up: below = positive offset = forward
        
        // New time = current time + offset
        double newTime = currentTime + timeOffset;
        
        // Clamp to valid range
        if (newTime < 0.0) newTime = 0.0;
        
        // Seek to this position
        seekToTime(newTime);
    }
    
    void seekToTime(double time);  // Implemented after class definition
    
    juce::String formatTime(double seconds) const
    {
        int totalSecs = (int)seconds;
        int mins = totalSecs / 60;
        int secs = totalSecs % 60;
        
        // Format as MM:SS
        return juce::String(mins) + ":" + juce::String(secs).paddedLeft('0', 2);
    }
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TimelineComponent)
};

// Include AudioEngine header for implementation
#include "AudioEngine.h"

inline void TimelineComponent::seekToTime(double time)
{
    if (audioEngine)
    {
        audioEngine->seek(time);
    }
}
