#pragma once

#include <juce_core/juce_core.h>

namespace hawthorne
{

/** hh:mm:ss.mmm */
inline juce::String formatTime (juce::int64 samples, double sampleRate)
{
    const auto totalMs = (juce::int64) ((double) juce::jmax ((juce::int64) 0, samples) / sampleRate * 1000.0);
    const int ms = (int) (totalMs % 1000);
    const int s  = (int) ((totalMs / 1000) % 60);
    const int m  = (int) ((totalMs / 60000) % 60);
    const int h  = (int) (totalMs / 3600000);
    return juce::String (h).paddedLeft ('0', 2) + ":" + juce::String (m).paddedLeft ('0', 2) + ":"
         + juce::String (s).paddedLeft ('0', 2) + "." + juce::String (ms).paddedLeft ('0', 3);
}

/** mm:ss (for lists) */
inline juce::String formatShortTime (juce::int64 samples, double sampleRate)
{
    const auto total = (juce::int64) ((double) juce::jmax ((juce::int64) 0, samples) / sampleRate);
    return juce::String (total / 60).paddedLeft ('0', 2) + ":" + juce::String (total % 60).paddedLeft ('0', 2);
}

} // namespace hawthorne
