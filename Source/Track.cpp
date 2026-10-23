#include "Track.h"
#include "RingBuffer.h"
#include "DiskStreamingThread.h"
#include "WaveformPeakGenerator.h"

//==============================================================================
Track::Track(int trackNumber)
    : trackNumber(trackNumber),
      trackName("Track " + juce::String(trackNumber)),
      backgroundThread("Recording Disk Writer")
{
    wavFormat = std::make_unique<juce::WavAudioFormat>();
    recordingBuffer.resize(recordingBufferSize, 0.0f);
    
    // Pre-allocate resampling buffers for playback (avoid allocation on audio thread)
    // Worst case: 4x speed up = need 4x samples, plus 4 for cubic interpolation
    // At 48kHz, 2048 buffer size: 2048 * 4 + 4 = 8196 samples needed
    resampleSourceBuffer.resize(16384);  // Generous size for any speed/buffer combination
    resampleSourceBufferRight.resize(16384);  // For stereo
    
    // Start background thread for non-blocking disk writes
    backgroundThread.startThread(juce::Thread::Priority::normal);
    
    // Create ring buffer for disk streaming (8 seconds at 48kHz for ultra-smooth playback)
    playbackRingBuffer = std::make_unique<RingBuffer>(1, 384000);
    
    // Create disk streaming thread
    diskStreamer = std::make_unique<DiskStreamingThread>();
    diskStreamer->setTrackNumber(trackNumber);  // For diagnostic logging
    diskStreamer->setRingBuffer(playbackRingBuffer.get());
    
    // Create async waveform peak generator (Cubase-style - non-blocking)
    peakGenerator = std::make_unique<WaveformPeakGenerator>();
    
    juce::Logger::writeToLog("Track " + juce::String(trackNumber) + ": Initialized with disk streaming");
}

Track::~Track()
{
    stopRecording();
    
    if (diskStreamer)
    {
        diskStreamer->stopStreaming();
    }
    
    // Stop background thread
    backgroundThread.stopThread(1000);  // Wait up to 1 second for clean shutdown
}

void Track::setStereo(bool isStereo)
{
    stereoMode = isStereo;
    
    if (stereoMode)
    {
        // Allocate stereo buffers
        recordingBufferRight.resize(recordingBufferSize, 0.0f);
        
        // Update ring buffer for stereo (2 channels, 8 seconds at 48kHz)
        playbackRingBuffer = std::make_unique<RingBuffer>(2, 384000);
        diskStreamer->setRingBuffer(playbackRingBuffer.get());
        
        juce::Logger::writeToLog("Track " + juce::String(trackNumber) + ": Set to STEREO mode");
    }
    else
    {
        // Mono mode
        recordingBufferRight.clear();
        recordingBufferRight.shrink_to_fit();
        
        // Update ring buffer for mono (1 channel)
        playbackRingBuffer = std::make_unique<RingBuffer>(1, 192000);
        diskStreamer->setRingBuffer(playbackRingBuffer.get());
        
        juce::Logger::writeToLog("Track " + juce::String(trackNumber) + ": Set to MONO mode");
    }
}

void Track::startRecording(const juce::File& outputFile, juce::int64 startPosition)
{
    juce::Logger::writeToLog("Track " + juce::String(trackNumber) + ": startRecording()");
    juce::Logger::writeToLog("  Output file: " + outputFile.getFullPathName());
    juce::Logger::writeToLog("  Start position (silence pad): " + juce::String(startPosition) + " samples");
    
    // No RAM buffer allocation needed! Write directly to disk
    recordingFile = outputFile;
    recordingStartPosition = startPosition;
    
    // Save current playback file for tape-like punch-in merging
    previousPlaybackFile = juce::File();  // Clear first
    previousAudioLength = 0;
    
    if (playbackFile.existsAsFile() && playbackFile == outputFile)
    {
        // CRITICAL: Old and new file are the SAME (e.g., Track1.wav)
        // We need to COPY the old file to temp before deleting it!
        juce::File tempOldFile = outputFile.getParentDirectory().getChildFile(
            outputFile.getFileNameWithoutExtension() + "_old_" + juce::String(juce::Random::getSystemRandom().nextInt()) + ".wav");
        
        if (playbackFile.copyFileTo(tempOldFile))
        {
            previousPlaybackFile = tempOldFile;
            previousAudioLength = audioLengthInSamples;
            
            juce::Logger::writeToLog("Track " + juce::String(trackNumber) + ": Punch-in recording from position " + 
                                    juce::String(startPosition) + " samples");
            juce::Logger::writeToLog("  Copied old audio to temp file: " + tempOldFile.getFileName() + 
                                    " (" + juce::String(previousAudioLength) + " samples)");
        }
        else
        {
            juce::Logger::writeToLog("Track " + juce::String(trackNumber) + ": ERROR - Could not copy old file for punch-in merge!");
        }
    }
    else if (playbackFile.existsAsFile() && playbackFile != outputFile)
    {
        // Old and new are different files - can use old file directly
        previousPlaybackFile = playbackFile;
        previousAudioLength = audioLengthInSamples;
        
        juce::Logger::writeToLog("Track " + juce::String(trackNumber) + ": Punch-in recording from position " + 
                                juce::String(startPosition) + " samples");
        juce::Logger::writeToLog("  Previous audio file: " + playbackFile.getFileName() + 
                                " (" + juce::String(previousAudioLength) + " samples)");
    }
    else
    {
        juce::Logger::writeToLog("Track " + juce::String(trackNumber) + ": Recording from position " + 
                                juce::String(startPosition) + " samples (no previous audio)");
    }
    
    // Create audio file writer
    const juce::ScopedLock lock(recordingWriterLock);
    
    // Delete existing file if it exists
    if (recordingFile.existsAsFile())
        recordingFile.deleteFile();
    
    // Create file output stream
    auto fileStream = recordingFile.createOutputStream();
    
    if (fileStream != nullptr)
    {
        // Create WAV writer with configured bit depth (mono or stereo based on track mode)
        int numChannels = stereoMode ? 2 : 1;
        juce::String channelMode = stereoMode ? "stereo" : "mono";
        juce::Logger::writeToLog("  Creating WAV writer: " + juce::String(bitDepth) + "-bit " + channelMode);
        
        // Create the base writer
        auto* baseWriter = wavFormat->createWriterFor(
            fileStream.release(),
            sampleRate,
            numChannels,  // 1 for mono, 2 for stereo
            bitDepth,  // Use configured bit depth (16, 24, or 32)
            {},
            0);
        
        if (baseWriter)
        {
            // Wrap in ThreadedWriter for non-blocking disk I/O
            // INCREASED buffer size: 262144 samples = 5.5 seconds at 48kHz
            // (was 32768 = 680ms, which may have been too small for slow disks)
            recordingWriter.reset(new juce::AudioFormatWriter::ThreadedWriter(
                baseWriter,  // Takes ownership
                backgroundThread,  // Background thread for disk writes
                262144  // FIFO buffer size in samples - 8x larger than before!
            ));
            
            juce::Logger::writeToLog("  ThreadedWriter created with 262KB FIFO buffer (5.5 sec @ 48kHz)");
            
            // Pad file with silence up to startPosition.
            // CRITICAL for overdub: the disk streamer reads at file position ==
            // engine position.  If we start recording at engine position N the
            // first N samples in the file MUST be silence so that, on playback,
            // the streamer lands on the real audio at the right time.
            // Safe to write here: recording is still false so the audio callback
            // won't enter writeAudio() for this track.
            if (startPosition > 0)
            {
                static constexpr int padChunkSize = 48000;  // 1-second chunks
                std::vector<float> silence(padChunkSize, 0.0f);
                const float* channels[1] = { silence.data() };
                
                juce::int64 remaining = startPosition;
                while (remaining > 0)
                {
                    int chunk = static_cast<int>(juce::jmin(remaining, static_cast<juce::int64>(padChunkSize)));
                    recordingWriter->write(channels, chunk);
                    remaining -= chunk;
                }
                juce::Logger::writeToLog("  Padded " + juce::String(startPosition) + " samples of silence");
            }
            
            recording = true;
            juce::Logger::writeToLog("  Recording writer created - using threaded non-blocking disk I/O");
            
            // Pre-allocate clamping buffers to avoid memory allocation on audio thread
            // Use maximum buffer size (typically 2048 samples or less)
            clampBuffer.resize(8192);  // Large enough for any buffer size
            if (stereoMode)
                clampBufferRight.resize(8192);
        }
        else
        {
            juce::Logger::writeToLog("  ERROR: Failed to create recording writer");
        }
    }
    else
    {
        juce::Logger::writeToLog("  ERROR: Failed to create output stream");
    }
}

void Track::stopRecording()
{
    recording = false;
    
    const juce::ScopedLock lock(recordingWriterLock);
    
    // Close file writer (ThreadedWriter automatically flushes on destruction)
    if (recordingWriter)
    {
        // Reset will destroy the ThreadedWriter, which automatically:
        // 1. Flushes the FIFO buffer
        // 2. Waits for background thread to finish writing
        // 3. Closes the file cleanly
        recordingWriter.reset();
        
        // Free clamping buffers (save memory when not recording)
        clampBuffer.clear();
        clampBuffer.shrink_to_fit();
        clampBufferRight.clear();
        clampBufferRight.shrink_to_fit();
        
        juce::Logger::writeToLog("Track " + juce::String(trackNumber) + ": Recording stopped, file written");
        
        // Report recording statistics
        int calls = recordingWriteCalls.load();
        int failures = recordingWriteFailures.load();
        if (calls > 0)
        {
            float lossPercent = 100.0f * failures / calls;
            juce::Logger::writeToLog("Track " + juce::String(trackNumber) + 
                                     " recording stats: " + juce::String(failures) + 
                                     " / " + juce::String(calls) + " write failures (" +
                                     juce::String(lossPercent, 1) + "% audio loss)");
            
            if (failures > 0)
            {
                juce::Logger::writeToLog("WARNING: " + juce::String(lossPercent, 1) + 
                                         "% of audio was LOST due to FIFO overflow!");
                juce::Logger::writeToLog("Possible causes: Disk too slow, too many tracks, increase FIFO size");
            }
        }
        
        // Reset counters for next recording
        recordingWriteCalls = 0;
        recordingWriteFailures = 0;
        
        // TAPE-LIKE PUNCH-IN: Merge with previous audio if it exists
        if (previousPlaybackFile.existsAsFile() && previousPlaybackFile != recordingFile)
        {
            juce::Logger::writeToLog("Track " + juce::String(trackNumber) + ": Merging punch-in recording with previous audio");
            mergePunchInRecording();
        }
        else
        {
            // No previous audio - just load the new recording
            loadAudioFile(recordingFile);
        }
    }
}

void Track::mergePunchInRecording()
{
    // Tape-like punch-in: merge old audio with new recording
    // Keep old audio before/after the recorded section
    
    juce::Logger::writeToLog("Track " + juce::String(trackNumber) + ": Starting punch-in merge");
    juce::Logger::writeToLog("  Old file: " + previousPlaybackFile.getFileName() + 
                            " (" + juce::String(previousAudioLength) + " samples)");
    juce::Logger::writeToLog("  New recording: " + recordingFile.getFileName());
    juce::Logger::writeToLog("  Punch-in position: " + juce::String(recordingStartPosition) + " samples");
    
    // Read old file
    std::unique_ptr<juce::AudioFormatReader> oldReader(wavFormat->createReaderFor(
        new juce::FileInputStream(previousPlaybackFile), true));
    
    // Read new file
    std::unique_ptr<juce::AudioFormatReader> newReader(wavFormat->createReaderFor(
        new juce::FileInputStream(recordingFile), true));
    
    if (!oldReader || !newReader)
    {
        juce::Logger::writeToLog("  ERROR: Could not open files for merging - using new recording only");
        loadAudioFile(recordingFile);
        return;
    }
    
    juce::Logger::writeToLog("  Old file: " + juce::String(oldReader->lengthInSamples) + " samples");
    juce::Logger::writeToLog("  New file: " + juce::String(newReader->lengthInSamples) + " samples");
    
    // Determine final length: max of old and new
    juce::int64 finalLength = juce::jmax(oldReader->lengthInSamples, newReader->lengthInSamples);
    
    // Determine channel count (should match between old and new, use new file's channels)
    int numChannels = newReader->numChannels;
    juce::String channelMode = (numChannels == 2) ? "stereo" : "mono";
    juce::Logger::writeToLog("  Merging as " + channelMode + " (" + juce::String(numChannels) + " channels)");
    
    // Create merged file
    juce::File mergedFile = recordingFile.getParentDirectory().getChildFile(
        recordingFile.getFileNameWithoutExtension() + "_merged.wav");
    
    auto mergedStream = mergedFile.createOutputStream();
    if (!mergedStream)
    {
        juce::Logger::writeToLog("  ERROR: Could not create merged file");
        loadAudioFile(recordingFile);
        return;
    }
    
    // Create writer for merged file
    std::unique_ptr<juce::AudioFormatWriter> mergedWriter(wavFormat->createWriterFor(
        mergedStream.release(),
        sampleRate,
        numChannels,  // Use actual channel count from recording
        bitDepth,
        {},
        0));
    
    if (!mergedWriter)
    {
        juce::Logger::writeToLog("  ERROR: Could not create merged writer");
        loadAudioFile(recordingFile);
        return;
    }
    
    // Merge in chunks
    const int chunkSize = 8192;
    juce::AudioBuffer<float> oldBuffer(numChannels, chunkSize);
    juce::AudioBuffer<float> newBuffer(numChannels, chunkSize);
    juce::AudioBuffer<float> outputBuffer(numChannels, chunkSize);
    
    juce::int64 position = 0;
    
    while (position < finalLength)
    {
        int samplesToProcess = (int)juce::jmin((juce::int64)chunkSize, finalLength - position);
        
        // Read from old file (or zeros if past end)
        if (position < oldReader->lengthInSamples)
        {
            oldReader->read(&oldBuffer, 0, samplesToProcess, position, true, true);
        }
        else
        {
            oldBuffer.clear();
        }
        
        // Read from new file (or zeros if past end)
        if (position < newReader->lengthInSamples)
        {
            newReader->read(&newBuffer, 0, samplesToProcess, position, true, true);
        }
        else
        {
            newBuffer.clear();
        }
        
        // Decide which to use: new recording overwrites old from recordingStartPosition onward
        if (position >= recordingStartPosition)
        {
            // Use new recording - copy all channels to output buffer
            for (int ch = 0; ch < numChannels; ++ch)
            {
                outputBuffer.copyFrom(ch, 0, newBuffer, ch, 0, samplesToProcess);
            }
        }
        else
        {
            // Use old audio - copy all channels to output buffer
            for (int ch = 0; ch < numChannels; ++ch)
            {
                outputBuffer.copyFrom(ch, 0, oldBuffer, ch, 0, samplesToProcess);
            }
        }
        
        // Write merged chunk (all channels)
        const float* channels[2];  // Support up to stereo
        for (int ch = 0; ch < numChannels; ++ch)
        {
            channels[ch] = outputBuffer.getReadPointer(ch);
        }
        mergedWriter->writeFromFloatArrays(channels, numChannels, samplesToProcess);
        
        position += samplesToProcess;
    }
    
    mergedWriter->flush();
    mergedWriter.reset();
    
    juce::Logger::writeToLog("  Merge complete - " + juce::String(finalLength) + " samples written");
    
    // Delete old files and rename merged file
    if (recordingFile.existsAsFile())
        recordingFile.deleteFile();
    
    // Delete temp old file if it exists
    if (previousPlaybackFile.existsAsFile())
    {
        juce::Logger::writeToLog("  Deleting temp old file: " + previousPlaybackFile.getFileName());
        previousPlaybackFile.deleteFile();
    }
    
    if (mergedFile.moveFileTo(recordingFile))
    {
        juce::Logger::writeToLog("  Merged file renamed to: " + recordingFile.getFileName());
        loadAudioFile(recordingFile);
    }
    else
    {
        juce::Logger::writeToLog("  ERROR: Could not rename merged file");
        loadAudioFile(mergedFile);
    }
}

void Track::writeAudio(const float* data, int numSamples, juce::int64 positionInSamples)
{
    if (!recording)
        return;
    
    const juce::ScopedLock lock(recordingWriterLock);
    
    if (!recordingWriter)
        return;
    
    // Clamp input to prevent hot signals from clipping
    // Use pre-allocated buffer (no memory allocation on audio thread!)
    for (int i = 0; i < numSamples; ++i)
    {
        clampBuffer[i] = juce::jlimit(-1.0f, 1.0f, data[i]);
    }
    
    // Write to background thread FIFO (non-blocking, fast!)
    const float* channelData[1] = { clampBuffer.data() };
    
    // ThreadedWriter::write() returns false if FIFO is full (disk can't keep up)
    recordingWriteCalls++;
    bool writeSuccess = recordingWriter->write(channelData, numSamples);
    
    // Track failures (no logging on audio thread - would cause jitter!)
    if (!writeSuccess)
        recordingWriteFailures++;
    
    // Also update live waveform display buffer (use clamped data)
    {
        const juce::ScopedLock displayLock(recordingBufferLock);
        
        int writePos = recordingBufferWritePos.load();
        
        for (int i = 0; i < numSamples; ++i)
        {
            recordingBuffer[writePos] = clampBuffer[i];
            writePos = (writePos + 1) % recordingBufferSize;
        }
        
        recordingBufferWritePos.store(writePos);
    }
}

void Track::writeAudioStereo(const float* leftData, const float* rightData, int numSamples, juce::int64 positionInSamples)
{
    if (!recording || !stereoMode)
        return;
    
    const juce::ScopedLock lock(recordingWriterLock);
    
    if (!recordingWriter)
        return;
    
    // Clamp input to prevent hot signals from clipping
    // Use pre-allocated buffers (no memory allocation on audio thread!)
    for (int i = 0; i < numSamples; ++i)
    {
        clampBuffer[i] = juce::jlimit(-1.0f, 1.0f, leftData[i]);
        clampBufferRight[i] = juce::jlimit(-1.0f, 1.0f, rightData[i]);
    }
    
    // Write to background thread FIFO (non-blocking, fast!)
    const float* channelData[2] = { clampBuffer.data(), clampBufferRight.data() };
    
    // ThreadedWriter::write() returns false if FIFO is full (disk can't keep up)
    recordingWriteCalls++;
    bool writeSuccess = recordingWriter->write(channelData, numSamples);
    
    // Track failures (no logging on audio thread - would cause jitter!)
    if (!writeSuccess)
        recordingWriteFailures++;
    
    // Also update live waveform display buffers (use clamped data)
    {
        const juce::ScopedLock displayLock(recordingBufferLock);
        
        int writePos = recordingBufferWritePos.load();
        
        for (int i = 0; i < numSamples; ++i)
        {
            recordingBuffer[writePos] = clampBuffer[i];  // Left channel
            recordingBufferRight[writePos] = clampBufferRight[i];  // Right channel
            writePos = (writePos + 1) % recordingBufferSize;
        }
        
        recordingBufferWritePos.store(writePos);
    }
}

void Track::saveToFile()
{
    // Not needed with disk streaming - already written to disk!
    // This method kept for compatibility
}

void Track::readAudio(float* outputBuffer, int numSamples, double startPosition)
{
    // Read from ring buffer (filled by background disk streaming thread)
    if (!playbackRingBuffer || !hasAudio())
    {
        juce::FloatVectorOperations::clear(outputBuffer, numSamples);
        return;
    }
    
    // CRITICAL: Ensure disk streamer is running at correct position
    if (diskStreamer && !diskStreamer->isStreaming())
    {
        // Start streaming from current playback position
        juce::Logger::writeToLog("Track " + juce::String(trackNumber) + 
                                 ": Starting disk streaming at position " + 
                                 juce::String(startPosition, 2) + "s");
        diskStreamer->setPlaybackPosition(static_cast<juce::int64>(startPosition * sampleRate));
        diskStreamer->startStreaming();
    }
    
    // Check if ring buffer has minimum samples before playing
    // This prevents crackling during startup while buffer fills
    const int minimumSamples = 24000;  // 0.5 seconds @ 48kHz
    int available = playbackRingBuffer->getReadAvailable();
    
    if (available < minimumSamples)
    {
        // Not enough samples yet - return silence and let buffer fill
        // This is better than underrunning which causes clicks/pops
        juce::FloatVectorOperations::clear(outputBuffer, numSamples);
        
        static bool logged = false;
        if (!logged)
        {
            juce::Logger::writeToLog("Track " + juce::String(trackNumber) + 
                                     ": Buffering... (" + juce::String(available) + 
                                     "/" + juce::String(minimumSamples) + " samples)");
            logged = true;
        }
        return;
    }
    
    float* bufferPtr[1] = { outputBuffer };
    int samplesRead = playbackRingBuffer->read(bufferPtr, numSamples);
    
    // DIAGNOSTIC: Log underruns
    static std::atomic<int> underrunCount{0};
    static std::atomic<int> totalReads{0};
    totalReads++;
    
    if (samplesRead < numSamples)
    {
        underrunCount++;
        
        // Log every 100 underruns
        if (underrunCount % 100 == 0)
        {
            int available = playbackRingBuffer->getReadAvailable();
            juce::Logger::writeToLog("Track " + juce::String(trackNumber) + 
                                     " UNDERRUN! Requested: " + juce::String(numSamples) + 
                                     " Got: " + juce::String(samplesRead) +
                                     " Available: " + juce::String(available) +
                                     " (Total: " + juce::String(underrunCount.load()) + "/" + 
                                     juce::String(totalReads.load()) + ")");
        }
        
        // Fill remaining with silence
        juce::FloatVectorOperations::clear(outputBuffer + samplesRead, numSamples - samplesRead);
    }
    
    // DIAGNOSTIC: Log ring buffer status every 1000 reads
    if (totalReads % 1000 == 0)
    {
        int available = playbackRingBuffer->getReadAvailable();
        int bufferSize = playbackRingBuffer->getBufferSize();
        float fillPercent = 100.0f * available / bufferSize;
        juce::Logger::writeToLog("Track " + juce::String(trackNumber) + 
                                 " ring buffer: " + juce::String(available) + "/" + 
                                 juce::String(bufferSize) + " (" + 
                                 juce::String(fillPercent, 1) + "% full)");
    }
    
    // Filtering temporarily disabled to isolate static issue
    // Will re-enable with corrected implementation
}

void Track::readAudioStereo(float* leftBuffer, float* rightBuffer, int numSamples, double startPosition)
{
    // Read stereo from ring buffer (filled by background disk streaming thread)
    if (!playbackRingBuffer || !hasAudio() || !stereoMode)
    {
        juce::FloatVectorOperations::clear(leftBuffer, numSamples);
        juce::FloatVectorOperations::clear(rightBuffer, numSamples);
        return;
    }
    
    // CRITICAL: Ensure disk streamer is running at correct position
    if (diskStreamer && !diskStreamer->isStreaming())
    {
        // Start streaming from current playback position
        juce::Logger::writeToLog("Track " + juce::String(trackNumber) + 
                                 ": Starting STEREO disk streaming at position " + 
                                 juce::String(startPosition, 2) + "s");
        diskStreamer->setPlaybackPosition(static_cast<juce::int64>(startPosition * sampleRate));
        diskStreamer->startStreaming();
    }
    
    // Check if ring buffer has minimum samples before playing
    // This prevents crackling during startup while buffer fills
    const int minimumSamples = 24000;  // 0.5 seconds @ 48kHz
    int available = playbackRingBuffer->getReadAvailable();
    
    if (available < minimumSamples)
    {
        // Not enough samples yet - return silence and let buffer fill
        juce::FloatVectorOperations::clear(leftBuffer, numSamples);
        juce::FloatVectorOperations::clear(rightBuffer, numSamples);
        
        static bool logged = false;
        if (!logged)
        {
            juce::Logger::writeToLog("Track " + juce::String(trackNumber) + 
                                     ": STEREO Buffering... (" + juce::String(available) + 
                                     "/" + juce::String(minimumSamples) + " samples)");
            logged = true;
        }
        return;
    }
    
    float* bufferPtr[2] = { leftBuffer, rightBuffer };
    int samplesRead = playbackRingBuffer->read(bufferPtr, numSamples);
    
    // DIAGNOSTIC: Log underruns
    static std::atomic<int> underrunCount{0};
    static std::atomic<int> totalReads{0};
    totalReads++;
    
    if (samplesRead < numSamples)
    {
        underrunCount++;
        
        // Log every 100 underruns
        if (underrunCount % 100 == 0)
        {
            int available = playbackRingBuffer->getReadAvailable();
            juce::Logger::writeToLog("Track " + juce::String(trackNumber) + 
                                     " STEREO UNDERRUN! Requested: " + juce::String(numSamples) + 
                                     " Got: " + juce::String(samplesRead) +
                                     " Available: " + juce::String(available) +
                                     " (Total: " + juce::String(underrunCount.load()) + "/" + 
                                     juce::String(totalReads.load()) + ")");
        }
        
        // Fill remaining with silence
        juce::FloatVectorOperations::clear(leftBuffer + samplesRead, numSamples - samplesRead);
        juce::FloatVectorOperations::clear(rightBuffer + samplesRead, numSamples - samplesRead);
    }
    
    // Filtering temporarily disabled to isolate static issue
    // Will re-enable with corrected implementation
}

// Varispeed playback with cubic Hermite interpolation resampling (high quality)
// Supports reverse playback - pass negative speed to play backwards
void Track::readAudioResampled(float* outputBuffer, int numSamples, double startPosition, double speed)
{
    if (!playbackRingBuffer || !hasAudio() || speed == 0.0)
    {
        juce::FloatVectorOperations::clear(outputBuffer, numSamples);
        return;
    }
    
    // Handle reverse playback (negative speed)
    bool reverse = speed < 0.0;
    double absSpeed = std::abs(speed);
    
    // Calculate how many source samples we need to read
    // Add extra samples for cubic interpolation (needs 4 points)
    int sourceSamplesNeeded = static_cast<int>(std::ceil(numSamples * absSpeed)) + 4;
    
    // Ensure pre-allocated buffer is large enough (should be from constructor)
    if (sourceSamplesNeeded > (int)resampleSourceBuffer.size())
    {
        // Should never happen with proper pre-allocation, but safety check
        juce::Logger::writeToLog("WARNING: Resample buffer too small, expanding");
        resampleSourceBuffer.resize(sourceSamplesNeeded);
    }
    
    // Read source samples from ring buffer into pre-allocated buffer
    float* sourcePtr[1] = { resampleSourceBuffer.data() };
    playbackRingBuffer->read(sourcePtr, sourceSamplesNeeded);
    
    // If reverse, flip the buffer so we read backwards
    if (reverse)
    {
        std::reverse(resampleSourceBuffer.begin(), resampleSourceBuffer.begin() + sourceSamplesNeeded);
    }
    
    // Apply anti-aliasing low-pass filter when speeding up (pitch shifting up)
    if (absSpeed > 1.0)
    {
        // Simple one-pole low-pass filter to reduce aliasing
        // Cutoff frequency scales with speed ratio
        float alpha = 1.0f / static_cast<float>(absSpeed);
        for (int i = 1; i < sourceSamplesNeeded; ++i)
        {
            float input = resampleSourceBuffer[i];  // Read ORIGINAL input first
            resampleSourceBuffer[i] = resampleSourceBuffer[i-1] + alpha * (input - resampleSourceBuffer[i-1]);
        }
    }
    
    // Resample with cubic Hermite interpolation (4-point, C1 continuous)
    double sourcePos = 1.0;  // Start at 1 to have room for interpolation
    for (int i = 0; i < numSamples; ++i)
    {
        int index = static_cast<int>(sourcePos);
        double frac = sourcePos - index;
        
        // Bounds check - need 4 points for cubic
        if (index >= 1 && index < sourceSamplesNeeded - 2)
        {
            // Get 4 points for cubic interpolation
            float y0 = resampleSourceBuffer[index - 1];  // Point before
            float y1 = resampleSourceBuffer[index];      // Current point
            float y2 = resampleSourceBuffer[index + 1];  // Next point
            float y3 = resampleSourceBuffer[index + 2];  // Point after
            
            // Cubic Hermite interpolation (4-point, 3rd order)
            // Smoother than linear, maintains C1 continuity
            float c0 = y1;
            float c1 = 0.5f * (y2 - y0);
            float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
            float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
            
            float t = static_cast<float>(frac);
            outputBuffer[i] = ((c3 * t + c2) * t + c1) * t + c0;
        }
        else
        {
            outputBuffer[i] = 0.0f;
        }
        
        sourcePos += absSpeed;
    }
    
    // Cubic interpolation can overshoot - clamp to prevent clipping artifacts
    for (int i = 0; i < numSamples; ++i)
    {
        outputBuffer[i] = juce::jlimit(-1.0f, 1.0f, outputBuffer[i]);
    }
    
    // DC offset removal - remove any DC component
    float dcOffset = 0.0f;
    for (int i = 0; i < numSamples; ++i)
        dcOffset += outputBuffer[i];
    dcOffset /= numSamples;
    
    if (std::abs(dcOffset) > 0.0001f)  // Only if significant
    {
        for (int i = 0; i < numSamples; ++i)
            outputBuffer[i] -= dcOffset;
    }
}

void Track::readAudioStereoResampled(float* leftBuffer, float* rightBuffer, int numSamples, double startPosition, double speed)
{
    if (!playbackRingBuffer || !hasAudio() || !stereoMode || speed == 0.0)
    {
        juce::FloatVectorOperations::clear(leftBuffer, numSamples);
        juce::FloatVectorOperations::clear(rightBuffer, numSamples);
        return;
    }
    
    // Handle reverse playback (negative speed)
    bool reverse = speed < 0.0;
    double absSpeed = std::abs(speed);
    
    // Calculate how many source samples we need to read
    // Add extra samples for cubic interpolation (needs 4 points)
    int sourceSamplesNeeded = static_cast<int>(std::ceil(numSamples * absSpeed)) + 4;
    
    // Ensure pre-allocated buffers are large enough (should be from constructor)
    if (sourceSamplesNeeded > (int)resampleSourceBuffer.size())
    {
        // Should never happen with proper pre-allocation, but safety check
        juce::Logger::writeToLog("WARNING: Stereo resample buffer too small, expanding");
        resampleSourceBuffer.resize(sourceSamplesNeeded);
        resampleSourceBufferRight.resize(sourceSamplesNeeded);
    }
    
    // Read source samples from ring buffer into pre-allocated buffers (stereo)
    float* sourcePtr[2] = { resampleSourceBuffer.data(), resampleSourceBufferRight.data() };
    playbackRingBuffer->read(sourcePtr, sourceSamplesNeeded);
    
    // If reverse, flip both buffers so we read backwards
    if (reverse)
    {
        std::reverse(resampleSourceBuffer.begin(), resampleSourceBuffer.begin() + sourceSamplesNeeded);
        std::reverse(resampleSourceBufferRight.begin(), resampleSourceBufferRight.begin() + sourceSamplesNeeded);
    }
    
    // Apply anti-aliasing low-pass filter when speeding up (pitch shifting up)
    if (absSpeed > 1.0)
    {
        float alpha = 1.0f / static_cast<float>(absSpeed);
        for (int i = 1; i < sourceSamplesNeeded; ++i)
        {
            float inputL = resampleSourceBuffer[i];   // Read ORIGINAL input first
            float inputR = resampleSourceBufferRight[i];  // Read ORIGINAL input first
            resampleSourceBuffer[i] = resampleSourceBuffer[i-1] + alpha * (inputL - resampleSourceBuffer[i-1]);
            resampleSourceBufferRight[i] = resampleSourceBufferRight[i-1] + alpha * (inputR - resampleSourceBufferRight[i-1]);
        }
    }
    
    // Resample both channels with cubic Hermite interpolation
    double sourcePos = 1.0;  // Start at 1 to have room for interpolation
    for (int i = 0; i < numSamples; ++i)
    {
        int index = static_cast<int>(sourcePos);
        double frac = sourcePos - index;
        
        // Bounds check - need 4 points for cubic
        if (index >= 1 && index < sourceSamplesNeeded - 2)
        {
            // LEFT CHANNEL - Cubic Hermite interpolation
            float yL0 = resampleSourceBuffer[index - 1];
            float yL1 = resampleSourceBuffer[index];
            float yL2 = resampleSourceBuffer[index + 1];
            float yL3 = resampleSourceBuffer[index + 2];
            
            float cL0 = yL1;
            float cL1 = 0.5f * (yL2 - yL0);
            float cL2 = yL0 - 2.5f * yL1 + 2.0f * yL2 - 0.5f * yL3;
            float cL3 = 0.5f * (yL3 - yL0) + 1.5f * (yL1 - yL2);
            
            float t = static_cast<float>(frac);
            leftBuffer[i] = ((cL3 * t + cL2) * t + cL1) * t + cL0;
            
            // RIGHT CHANNEL - Cubic Hermite interpolation
            float yR0 = resampleSourceBufferRight[index - 1];
            float yR1 = resampleSourceBufferRight[index];
            float yR2 = resampleSourceBufferRight[index + 1];
            float yR3 = resampleSourceBufferRight[index + 2];
            
            float cR0 = yR1;
            float cR1 = 0.5f * (yR2 - yR0);
            float cR2 = yR0 - 2.5f * yR1 + 2.0f * yR2 - 0.5f * yR3;
            float cR3 = 0.5f * (yR3 - yR0) + 1.5f * (yR1 - yR2);
            
            rightBuffer[i] = ((cR3 * t + cR2) * t + cR1) * t + cR0;
        }
        else
        {
            leftBuffer[i] = 0.0f;
            rightBuffer[i] = 0.0f;
        }
        
        sourcePos += absSpeed;
    }
    
    // Cubic interpolation can overshoot - clamp to prevent clipping artifacts
    for (int i = 0; i < numSamples; ++i)
    {
        leftBuffer[i] = juce::jlimit(-1.0f, 1.0f, leftBuffer[i]);
        rightBuffer[i] = juce::jlimit(-1.0f, 1.0f, rightBuffer[i]);
    }
    
    // DC offset removal for both channels
    float dcOffsetL = 0.0f, dcOffsetR = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        dcOffsetL += leftBuffer[i];
        dcOffsetR += rightBuffer[i];
    }
    dcOffsetL /= numSamples;
    dcOffsetR /= numSamples;
    
    if (std::abs(dcOffsetL) > 0.0001f)
    {
        for (int i = 0; i < numSamples; ++i)
            leftBuffer[i] -= dcOffsetL;
    }
    if (std::abs(dcOffsetR) > 0.0001f)
    {
        for (int i = 0; i < numSamples; ++i)
            rightBuffer[i] -= dcOffsetR;
    }
}

void Track::loadAudioFile(const juce::File& audioFile)
{
    juce::Logger::writeToLog("Track " + juce::String(trackNumber) + ": Loading " + audioFile.getFullPathName());
    
    if (!audioFile.existsAsFile())
    {
        juce::Logger::writeToLog("  File does not exist");
        return;
    }
    
    playbackFile = audioFile;
    
    // Stop existing streaming
    if (diskStreamer)
    {
        diskStreamer->stopStreaming();
    }
    
    // Set up disk streamer with new file
    diskStreamer->setAudioFile(playbackFile, sampleRate);
    
    // Read actual file format to calculate length correctly
    // Use JUCE's AudioFormatReader to get exact format
    std::unique_ptr<juce::AudioFormatReader> reader(wavFormat->createReaderFor(
        new juce::FileInputStream(audioFile), true));
    
    if (reader)
    {
        // Get exact length from reader
        audioLengthInSamples = reader->lengthInSamples;
        
        // CRITICAL: Auto-detect stereo/mono from file and reconfigure ring buffer!
        bool fileIsStereo = (reader->numChannels == 2);
        if (fileIsStereo != stereoMode)
        {
            stereoMode = fileIsStereo;
            juce::Logger::writeToLog("  Auto-detected " + juce::String(reader->numChannels) + 
                                     " channel(s), set stereoMode=" + juce::String(stereoMode ? "true" : "false"));
            
            // Allocate stereo recording buffers if needed
            if (stereoMode)
            {
                recordingBufferRight.resize(recordingBufferSize, 0.0f);
                resampleSourceBufferRight.resize(16384);  // For stereo resampling
            }
        }
        
        // Recreate ring buffer with correct channel count (critical for proper playback!)
        int numChannels = stereoMode ? 2 : 1;
        playbackRingBuffer = std::make_unique<RingBuffer>(numChannels, 384000);
        diskStreamer->setRingBuffer(playbackRingBuffer.get());
        diskStreamer->setAudioFile(playbackFile, sampleRate);  // Reset with new ring buffer
        
        // Log file format details
        juce::Logger::writeToLog("  WAV format: " + juce::String(reader->bitsPerSample) + "-bit, " + 
                                juce::String(reader->numChannels) + " channel(s)");
        juce::Logger::writeToLog("  Sample rate: " + juce::String(reader->sampleRate) + " Hz");
        juce::Logger::writeToLog("  Length: " + juce::String(audioLengthInSamples) + " samples (" + 
                                juce::String(audioLengthInSamples / sampleRate, 2) + " seconds)");
    }
    else
    {
        // Fallback: calculate from file size (less accurate)
        juce::int64 fileSize = audioFile.getSize();
        juce::Logger::writeToLog("  WARNING: Could not read WAV header, estimating from file size");
        
        // Calculate based on configured bit depth
        int bytesPerSample = (bitDepth / 8);  // 16-bit = 2, 24-bit = 3, 32-bit = 4
        
        if (fileSize > 44)
        {
            audioLengthInSamples = (fileSize - 44) / bytesPerSample;
        }
        else
        {
            audioLengthInSamples = 0;
        }
        
        juce::Logger::writeToLog("  File size: " + juce::String(fileSize) + " bytes");
        juce::Logger::writeToLog("  Estimated length: " + juce::String(audioLengthInSamples) + " samples");
    }
    
    // DON'T start streaming yet - wait for playback to start
    // This prevents position mismatch when loading files while playhead is not at 0
    // diskStreamer->setPlaybackPosition(0);  // ← REMOVED
    // diskStreamer->startStreaming();         // ← REMOVED
    
    juce::Logger::writeToLog("  File loaded, streaming will start on playback");
    
    // Trigger async waveform generation (runs in background thread)
    invalidateWaveformCache();
}

void Track::clearTrack()
{
    juce::Logger::writeToLog("Track " + juce::String(trackNumber) + ": Clearing track");
    
    // Stop streaming
    if (diskStreamer)
    {
        diskStreamer->stopStreaming();
    }
    
    // Clear ring buffer
    if (playbackRingBuffer)
    {
        playbackRingBuffer->reset();
    }
    
    playbackFile = juce::File();
    audioLengthInSamples = 0;
    
    // Invalidate waveform cache
    invalidateWaveformCache();
}

void Track::updateStreamPosition(juce::int64 playbackPosition)
{
    // Update disk streaming thread with current playback position
    // This tells it where to read ahead from
    if (diskStreamer && hasAudio())
    {
        diskStreamer->setPlaybackPosition(playbackPosition);
    }
}

bool Track::hasAudio() const
{
    return playbackFile.existsAsFile() && audioLengthInSamples > 0;
}

void Track::setArmed(bool shouldBeArmed)
{
    armed = shouldBeArmed;
    juce::Logger::writeToLog("Track " + juce::String(trackNumber) + ": Armed state set to " + 
                            juce::String(armed ? "TRUE" : "FALSE"));
    
    // NO BUFFER PRE-ALLOCATION NEEDED!
    // Disk streaming handles everything async
}

void Track::setMonitoring(bool shouldMonitor)
{
    monitoring = shouldMonitor;
    juce::Logger::writeToLog("Track " + juce::String(trackNumber) + ": Monitoring state set to " + 
                            juce::String(monitoring ? "ON" : "OFF"));
}

void Track::setSampleRate(double newSampleRate)
{
    sampleRate = newSampleRate;
    juce::Logger::writeToLog("Track " + juce::String(trackNumber) + ": Sample rate set to " + 
                            juce::String(sampleRate));
}

void Track::setBitDepth(int depth)
{
    bitDepth = depth;
    juce::Logger::writeToLog("Track " + juce::String(trackNumber) + ": Bit depth set to " + 
                            juce::String(bitDepth));
}

std::vector<float> Track::getWaveformPeaks(int resolution)
{
    const juce::ScopedLock lock(waveformCacheLock);
    
    // Check if async generator has new peaks ready
    if (peakGenerator)
    {
        std::vector<float> asyncPeaks;
        int asyncResolution;
        
        if (peakGenerator->tryGetPeaks(asyncPeaks, asyncResolution))
        {
            // Got new peaks from background thread
            cachedWaveformPeaks = std::move(asyncPeaks);
            cachedPeaksResolution = asyncResolution;
            waveformCacheValid = true;
        }
    }
    
    // Return cached peaks if valid and resolution matches
    if (waveformCacheValid && cachedPeaksResolution == resolution)
    {
        return cachedWaveformPeaks;
    }
    
    // No valid cache - return empty (will be filled asynchronously)
    return std::vector<float>();
}

void Track::invalidateWaveformCache()
{
    const juce::ScopedLock lock(waveformCacheLock);
    waveformCacheValid = false;
    
    // Clear old peaks in generator
    if (peakGenerator)
        peakGenerator->clearPeaks();
    
    // Start async generation if we have audio
    if (hasAudio() && peakGenerator)
    {
        // Use 2000 points for high-resolution waveform display
        peakGenerator->generatePeaksAsync(playbackFile, 5000, audioLengthInSamples);
    }
}

std::vector<float> Track::getRecordingBuffer(int numSamples)
{
    const juce::ScopedLock lock(recordingBufferLock);
    
    if (numSamples > recordingBufferSize)
        numSamples = recordingBufferSize;
    
    std::vector<float> buffer(numSamples, 0.0f);
    
    int readPos = (recordingBufferWritePos.load() - numSamples + recordingBufferSize) 
                  % recordingBufferSize;
    
    for (int i = 0; i < numSamples; ++i)
    {
        buffer[i] = recordingBuffer[readPos];
        readPos = (readPos + 1) % recordingBufferSize;
    }
    
    return buffer;
}

juce::int64 Track::getTotalLengthInSamples() const
{
    return audioLengthInSamples;
}

void Track::setName(const juce::String& name)
{
    trackName = name;
}
