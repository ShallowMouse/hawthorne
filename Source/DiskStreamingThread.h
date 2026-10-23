#pragma once

#include <juce_core/juce_core.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "RingBuffer.h"

//==============================================================================
// Background thread for streaming audio from disk to ring buffer
class DiskStreamingThread : public juce::Thread
{
public:
    DiskStreamingThread() : juce::Thread("DiskStreaming"), trackNumber(-1)
    {
        readBuffer.setSize(1, readChunkSize);
    }
    
    ~DiskStreamingThread()
    {
        stopThread(2000);
    }
    
    void setTrackNumber(int num) { trackNumber = num; }
    
    void setAudioFile(const juce::File& file, double sampleRate)
    {
        const juce::ScopedLock lock(readerLock);
        
        juce::String trackStr = trackNumber >= 0 ? ("Track " + juce::String(trackNumber) + " ") : "";
        juce::Logger::writeToLog(trackStr + "DiskStreamer: setAudioFile(" + file.getFullPathName() + ")");
        
        reader.reset();
        
        if (!file.existsAsFile())
        {
            juce::Logger::writeToLog(trackStr + "DiskStreamer: ERROR - File does not exist!");
            return;
        }
        
        juce::AudioFormatManager formatManager;
        formatManager.registerBasicFormats();
        
        reader.reset(formatManager.createReaderFor(file));
        
        if (reader != nullptr)
        {
            this->sampleRate = reader->sampleRate;
            totalSamples = reader->lengthInSamples;
            
            // CRITICAL: Resize readBuffer to match file channel count!
            int numChannels = static_cast<int>(reader->numChannels);
            readBuffer.setSize(numChannels, readChunkSize);
            
            juce::Logger::writeToLog(trackStr + "DiskStreamer: File loaded successfully");
            juce::Logger::writeToLog("  Channels: " + juce::String(reader->numChannels));
            juce::Logger::writeToLog("  Sample rate: " + juce::String(reader->sampleRate));
            juce::Logger::writeToLog("  Length: " + juce::String(totalSamples) + " samples");
            juce::Logger::writeToLog("  ReadBuffer resized to " + juce::String(numChannels) + " channel(s)");
        }
        else
        {
            juce::Logger::writeToLog(trackStr + "DiskStreamer: ERROR - Failed to create reader!");
        }
    }
    
    void setRingBuffer(RingBuffer* buffer)
    {
        ringBuffer = buffer;
    }
    
    void setPlaybackPosition(juce::int64 position)
    {
        targetPosition.store(position);
        needsRefill.store(true);
    }
    
    void startStreaming()
    {
        juce::String trackStr = trackNumber >= 0 ? ("Track " + juce::String(trackNumber) + " ") : "";
        juce::Logger::writeToLog(trackStr + "DiskStreamer: startStreaming() called");
        streaming.store(true);
        if (!isThreadRunning())
        {
            juce::Logger::writeToLog(trackStr + "DiskStreamer: Starting thread...");
            startThread(juce::Thread::Priority::normal);
        }
        else
        {
            juce::Logger::writeToLog(trackStr + "DiskStreamer: Thread already running");
        }
    }
    
    void stopStreaming()
    {
        streaming.store(false);
    }
    
    bool isStreaming() const { return streaming.load(); }
    
private:
    void run() override
    {
        while (!threadShouldExit())
        {
            if (!streaming.load())
            {
                wait(10);
                continue;
            }
            
            if (!reader || !ringBuffer)
            {
                wait(10);
                continue;
            }
            
            // Check if we need to refill (seek happened)
            if (needsRefill.load())
            {
                const juce::ScopedLock lock(readerLock);
                
                // Reset ring buffer
                ringBuffer->reset();
                
                // Set reader position to target
                juce::int64 target = targetPosition.load();
                currentReadPosition = target;
                
                needsRefill.store(false);
            }
            
            // Check if ring buffer needs more data
            int available = ringBuffer->getWriteSpace();
            
            if (available >= readChunkSize)
            {
                // Read a chunk from disk
                const juce::ScopedLock lock(readerLock);
                
                if (currentReadPosition >= totalSamples)
                {
                    // End of file
                    wait(10);
                    continue;
                }
                
                int samplesToRead = std::min(readChunkSize, (int)(totalSamples - currentReadPosition));
                
                if (reader && reader->read(&readBuffer, 0, samplesToRead, currentReadPosition, true, true))
                {
                    // DIAGNOSTIC: Log writes every 1000 chunks
                    static std::atomic<int> writeCount{0};
                    static std::atomic<int> writeFailures{0};
                    writeCount++;
                    
                    // Write to ring buffer - support both mono and stereo
                    int numChannels = readBuffer.getNumChannels();
                    int samplesWritten = 0;
                    
                    if (numChannels == 1)
                    {
                        // Mono
                        const float* channelData = readBuffer.getReadPointer(0);
                        const float* channels[1] = { channelData };
                        samplesWritten = ringBuffer->write(channels, samplesToRead, 0);
                    }
                    else if (numChannels == 2)
                    {
                        // Stereo
                        const float* leftData = readBuffer.getReadPointer(0);
                        const float* rightData = readBuffer.getReadPointer(1);
                        const float* channels[2] = { leftData, rightData };
                        samplesWritten = ringBuffer->write(channels, samplesToRead, 0);
                    }
                    else
                    {
                        // Unsupported channel count
                        juce::String trackStr = trackNumber >= 0 ? ("Track " + juce::String(trackNumber) + " ") : "";
                        juce::Logger::writeToLog(trackStr + "DiskStreamer: ERROR - Unsupported channel count: " + 
                                                 juce::String(numChannels));
                    }
                    
                    if (samplesWritten < samplesToRead)
                        writeFailures++;
                    
                    if (writeCount % 1000 == 0)
                    {
                        juce::String trackStr = trackNumber >= 0 ? ("Track " + juce::String(trackNumber) + " ") : "";
                        int available = ringBuffer->getWriteSpace();
                        juce::Logger::writeToLog(trackStr + "DiskStreamer: " + juce::String(writeCount.load()) + 
                                                 " writes, " + juce::String(writeFailures.load()) + 
                                                 " failures, available: " + juce::String(available));
                    }
                    
                    currentReadPosition += samplesToRead;
                }
                else
                {
                    // Read failed
                    static std::atomic<int> readFailures{0};
                    readFailures++;
                    
                    if (readFailures % 100 == 0)
                    {
                        juce::String trackStr = trackNumber >= 0 ? ("Track " + juce::String(trackNumber) + " ") : "";
                        juce::Logger::writeToLog(trackStr + "DiskStreamer: Read FAILED! (Total: " + 
                                                 juce::String(readFailures.load()) + ")");
                    }
                    
                    wait(10);
                }
            }
            else
            {
                // Ring buffer is full enough
                wait(5);
            }
        }
    }
    
    juce::CriticalSection readerLock;
    std::unique_ptr<juce::AudioFormatReader> reader;
    RingBuffer* ringBuffer = nullptr;
    juce::AudioBuffer<float> readBuffer;
    
    double sampleRate = 44100.0;
    juce::int64 totalSamples = 0;
    juce::int64 currentReadPosition = 0;
    
    static constexpr int readChunkSize = 4096;
    
    int trackNumber = -1;  // For diagnostic logging
    
    std::atomic<bool> streaming{false};
    std::atomic<bool> needsRefill{false};
    std::atomic<juce::int64> targetPosition{0};
};
