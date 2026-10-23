#pragma once

#include <juce_core/juce_core.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <vector>
#include <atomic>

//==============================================================================
// Background thread for generating waveform peaks without blocking UI
// Cubase-style: UI stays responsive, peaks appear when ready
//==============================================================================
class WaveformPeakGenerator : public juce::Thread
{
public:
    WaveformPeakGenerator() : juce::Thread("WaveformPeaks")
    {
        startThread(juce::Thread::Priority::low);
    }
    
    ~WaveformPeakGenerator()
    {
        stopThread(2000);
    }
    
    // Request peak generation for a file
    void generatePeaksAsync(const juce::File& file, int resolution, juce::int64 lengthInSamples)
    {
        const juce::ScopedLock lock(requestLock);
        
        pendingFile = file;
        pendingResolution = resolution;
        pendingLength = lengthInSamples;
        hasRequest.store(true);
        
        notify();  // Wake up thread
    }
    
    // Check if peaks are ready and get them (non-blocking)
    bool tryGetPeaks(std::vector<float>& outPeaks, int& outResolution)
    {
        if (!peaksReady.load())
            return false;
            
        const juce::ScopedLock lock(peaksLock);
        
        if (generatedPeaks.empty())
            return false;
            
        outPeaks = generatedPeaks;
        outResolution = generatedResolution;
        
        return true;
    }
    
    // Clear current peaks (when loading new file)
    void clearPeaks()
    {
        peaksReady.store(false);
        
        const juce::ScopedLock lock(peaksLock);
        generatedPeaks.clear();
        generatedResolution = 0;
    }

private:
    void run() override
    {
        while (!threadShouldExit())
        {
            if (!hasRequest.load())
            {
                wait(100);
                continue;
            }
            
            // Get request data
            juce::File fileToProcess;
            int resolution;
            juce::int64 lengthInSamples;
            
            {
                const juce::ScopedLock lock(requestLock);
                fileToProcess = pendingFile;
                resolution = pendingResolution;
                lengthInSamples = pendingLength;
                hasRequest.store(false);
            }
            
            // Generate peaks (this is the slow part - done off message thread)
            std::vector<float> peaks = generatePeaksSync(fileToProcess, resolution, lengthInSamples);
            
            // Store results
            {
                const juce::ScopedLock lock(peaksLock);
                generatedPeaks = std::move(peaks);
                generatedResolution = resolution;
            }
            
            peaksReady.store(true);
        }
    }
    
    std::vector<float> generatePeaksSync(const juce::File& file, int resolution, juce::int64 lengthInSamples)
    {
        std::vector<float> peaks;
        
        if (!file.existsAsFile() || lengthInSamples == 0)
            return peaks;
        
        juce::AudioFormatManager formatManager;
        formatManager.registerBasicFormats();
        
        auto reader = std::unique_ptr<juce::AudioFormatReader>(
            formatManager.createReaderFor(file));
        
        if (!reader)
            return peaks;
        
        juce::int64 samplesPerPeak = lengthInSamples / resolution;
        if (samplesPerPeak < 1)
            samplesPerPeak = 1;
        
        peaks.resize(resolution, 0.0f);
        
        juce::AudioBuffer<float> buffer(1, (int)samplesPerPeak);
        
        for (int i = 0; i < resolution; ++i)
        {
            if (threadShouldExit())
                break;
                
            juce::int64 startSample = i * samplesPerPeak;
            
            if (startSample >= lengthInSamples)
                break;
            
            int samplesToRead = juce::jmin((int)samplesPerPeak, 
                                          (int)(lengthInSamples - startSample));
            
            if (reader->read(&buffer, 0, samplesToRead, startSample, true, true))
            {
                // Find peak in this chunk
                float peak = 0.0f;
                for (int j = 0; j < samplesToRead; ++j)
                {
                    float sample = std::abs(buffer.getSample(0, j));
                    if (sample > peak)
                        peak = sample;
                }
                
                peaks[i] = peak;
            }
        }
        
        return peaks;
    }
    
    // Request data
    juce::CriticalSection requestLock;
    juce::File pendingFile;
    int pendingResolution = 0;
    juce::int64 pendingLength = 0;
    std::atomic<bool> hasRequest{false};
    
    // Generated peaks
    juce::CriticalSection peaksLock;
    std::vector<float> generatedPeaks;
    int generatedResolution = 0;
    std::atomic<bool> peaksReady{false};
};
