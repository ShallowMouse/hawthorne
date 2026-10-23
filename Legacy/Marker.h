#pragma once

#include <juce_core/juce_core.h>

//==============================================================================
// Single marker with timestamp, auto-number, and label
class Marker
{
public:
    Marker(double timeInSeconds, const juce::String& labelText = "")
        : timestamp(timeInSeconds), label(labelText)
    {
    }
    
    // Getters
    double getTimestamp() const { return timestamp; }
    int getNumber() const { return number; }
    juce::String getLabel() const { return label; }
    juce::Colour getColour() const { return colour; }
    
    // Setters
    void setTimestamp(double time) { timestamp = time; }
    void setNumber(int num) { number = num; }
    void setLabel(const juce::String& text) { label = text; }
    void setColour(juce::Colour col) { colour = col; }
    
    // Comparison for sorting by time
    bool operator<(const Marker& other) const
    {
        return timestamp < other.timestamp;
    }
    
private:
    double timestamp;  // Position in seconds
    int number = 0;    // Auto-assigned based on time order
    juce::String label;
    juce::Colour colour = juce::Colours::orange;  // Default vintage orange
    
    JUCE_LEAK_DETECTOR(Marker)
};
