#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>

//==============================================================================
// Ring buffer for streaming audio from disk
// Thread-safe circular buffer for producer/consumer pattern
class RingBuffer
{
public:
    RingBuffer(int numChannels = 1, int bufferSize = 192000)  // 4 seconds at 48kHz
        : channels(numChannels)
    {
        buffer.setSize(numChannels, bufferSize);
        buffer.clear();
        bufferSize = bufferSize;
    }
    
    // Write samples to ring buffer (called by disk reader thread)
    int write(const float* const* sourceData, int numSamples, int startChannel = 0)
    {
        if (numSamples <= 0) return 0;
        
        // LOCK-FREE: No lock needed for single producer/consumer
        // Use atomic read of readPosition for calculating available space
        int writePos = writePosition.load(std::memory_order_relaxed);
        int readPos = readPosition.load(std::memory_order_acquire);  // Sync with reader
        int bufferSize = buffer.getNumSamples();
        
        // Calculate available write space
        int available;
        if (writePos >= readPos)
            available = bufferSize - (writePos - readPos) - 1;  // -1 to distinguish full from empty
        else
            available = readPos - writePos - 1;
        
        int samplesToWrite = juce::jmin(numSamples, available);
        
        if (samplesToWrite <= 0)
            return 0;  // Buffer full
        
        // Write in up to two chunks (handling wrap-around)
        int samplesBeforeWrap = bufferSize - writePos;
        int firstChunk = juce::jmin(samplesToWrite, samplesBeforeWrap);
        int secondChunk = samplesToWrite - firstChunk;
        
        // Write first chunk
        for (int ch = 0; ch < channels; ++ch)
        {
            buffer.copyFrom(ch, writePos, sourceData[startChannel + ch], firstChunk);
        }
        
        // Write second chunk (if wrapping)
        if (secondChunk > 0)
        {
            for (int ch = 0; ch < channels; ++ch)
            {
                buffer.copyFrom(ch, 0, sourceData[startChannel + ch] + firstChunk, secondChunk);
            }
        }
        
        // Update write position - release semantics ensures writes are visible to reader
        writePosition.store((writePos + samplesToWrite) % bufferSize, std::memory_order_release);
        
        return samplesToWrite;
    }
    
    // Read samples from ring buffer (called by audio thread)
    int read(float* const* destData, int numSamples, int startChannel = 0)
    {
        if (numSamples <= 0) return 0;
        
        // LOCK-FREE: No lock needed for single producer/consumer
        // Use atomic read of writePosition for calculating available samples
        int readPos = readPosition.load(std::memory_order_relaxed);
        int writePos = writePosition.load(std::memory_order_acquire);  // Sync with writer
        int bufferSize = buffer.getNumSamples();
        
        // Calculate available samples to read
        int available;
        if (writePos >= readPos)
            available = writePos - readPos;
        else
            available = bufferSize - readPos + writePos;
        
        int samplesToRead = juce::jmin(numSamples, available);
        
        if (samplesToRead <= 0)
        {
            // Buffer underrun - fill with silence
            for (int ch = 0; ch < channels; ++ch)
            {
                juce::FloatVectorOperations::clear(destData[startChannel + ch], numSamples);
            }
            return 0;
        }
        
        // Read in up to two chunks (handling wrap-around)
        int samplesBeforeWrap = bufferSize - readPos;
        int firstChunk = juce::jmin(samplesToRead, samplesBeforeWrap);
        int secondChunk = samplesToRead - firstChunk;
        
        // Read first chunk
        for (int ch = 0; ch < channels; ++ch)
        {
            juce::FloatVectorOperations::copy(
                destData[startChannel + ch],
                buffer.getReadPointer(ch, readPos),
                firstChunk);
        }
        
        // Read second chunk (if wrapping)
        if (secondChunk > 0)
        {
            for (int ch = 0; ch < channels; ++ch)
            {
                juce::FloatVectorOperations::copy(
                    destData[startChannel + ch] + firstChunk,
                    buffer.getReadPointer(ch, 0),
                    secondChunk);
            }
        }
        
        // Update read position - release semantics ensures reads are complete before position update
        readPosition.store((readPos + samplesToRead) % bufferSize, std::memory_order_release);
        
        return samplesToRead;
    }
    
    // Get available samples to read
    int getReadAvailable() const
    {
        int writePos = writePosition.load(std::memory_order_acquire);
        int readPos = readPosition.load(std::memory_order_relaxed);
        int bufferSize = buffer.getNumSamples();
        
        if (writePos >= readPos)
            return writePos - readPos;
        else
            return bufferSize - readPos + writePos;
    }
    
    // Get available space to write
    int getWriteSpace() const
    {
        int writePos = writePosition.load(std::memory_order_relaxed);
        int readPos = readPosition.load(std::memory_order_acquire);
        int bufferSize = buffer.getNumSamples();
        
        int used;
        if (writePos >= readPos)
            used = writePos - readPos;
        else
            used = bufferSize - readPos + writePos;
        
        return bufferSize - used - 1;  // -1 to distinguish full from empty
    }
    
    // Reset buffer (ONLY call when no threads are reading/writing!)
    void reset()
    {
        writePosition.store(0, std::memory_order_release);
        readPosition.store(0, std::memory_order_release);
        buffer.clear();
    }
    
    int getBufferSize() const { return buffer.getNumSamples(); }
    
private:
    juce::AudioBuffer<float> buffer;
    std::atomic<int> writePosition{0};
    std::atomic<int> readPosition{0};
    int channels;
    // LOCK-FREE: No CriticalSection needed for single producer/consumer pattern
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RingBuffer)
};
