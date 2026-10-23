#include "AudioEngine.h"
#include "Track.h"
#include "OSCController.h"
#include "Session.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>

//==============================================================================
AudioEngine::AudioEngine(Settings* settingsPtr)
    : settings(settingsPtr)
{
    // Register audio formats
    formatManager.registerBasicFormats();
    
    // Zero out all peak levels
    for (int i = 0; i < MAX_TRACKS; ++i)
    {
        trackPeakLevels[i].store(0.0f);
        trackPeakLevelsRight[i].store(0.0f);
    }
    // LTC output is always active — no restore needed
}

AudioEngine::~AudioEngine()
{
    deviceManager.removeAudioCallback(this);
}

bool AudioEngine::initialize()
{
    // Try to use device from settings
    juce::String savedDevice = settings->getAudioDevice();
    
    if (savedDevice.isNotEmpty())
    {
        if (initializeWithDevice(savedDevice))
        {
            return true;
        }
    }
    
    // Fall back to default device
    juce::Logger::writeToLog("AudioEngine: Trying to initialize with default device...");
    juce::String error = deviceManager.initialiseWithDefaultDevices(32, 2);
    
    if (error.isNotEmpty())
    {
        juce::Logger::writeToLog("AudioEngine: Failed to initialize audio device: " + error);
        juce::Logger::writeToLog("AudioEngine: Continuing without audio - tracks already created");
        
        // Tracks already exist from constructor - don't recreate them
        return false;
    }
    
    // Save the device that was selected
    auto* device = deviceManager.getCurrentAudioDevice();
    if (device)
    {
        settings->setAudioDevice(device->getName());
        
        // CRITICAL: Set sample rate immediately for all tracks
        sampleRate = device->getCurrentSampleRate();
        juce::Logger::writeToLog("AudioEngine: Default device sample rate = " + juce::String(sampleRate));
        
        // DIAGNOSTIC: Log the actual channels
        int numInputs = device->getActiveInputChannels().countNumberOfSetBits();
        int numOutputs = device->getActiveOutputChannels().countNumberOfSetBits();
        juce::Logger::writeToLog("AudioEngine: Active input channels = " + juce::String(numInputs));
        juce::Logger::writeToLog("AudioEngine: Active output channels = " + juce::String(numOutputs));
        
        if (numInputs == 0)
        {
            juce::Logger::writeToLog("AudioEngine: WARNING - No input channels enabled! Recording will not work.");
        }
        
        // Update sample rate for existing tracks
        juce::Logger::writeToLog("AudioEngine: Updating sample rate for " + juce::String(tracks.size()) + " existing tracks");
        for (auto& track : tracks)
        {
            track->setSampleRate(sampleRate);
        }
    }
    
    // Set this as the audio callback
    deviceManager.addAudioCallback(this);
    
    // Tracks already exist from constructor - don't recreate them
    return true;
}

bool AudioEngine::initializeWithDevice(const juce::String& deviceName)
{
    juce::Logger::writeToLog("AudioEngine: Trying to initialize with device: " + deviceName);
    
    // Use saved driver type, falling back to any type that has the device
    juce::String savedDriverType = settings->getAudioDriverType();
    
    juce::AudioIODeviceType* targetType = nullptr;
    
    // First try the saved driver type
    if (savedDriverType.isNotEmpty())
    {
        for (auto& type : deviceManager.getAvailableDeviceTypes())
        {
            if (type->getTypeName() == savedDriverType)
            {
                type->scanForDevices();
                if (type->getDeviceNames().contains(deviceName))
                {
                    targetType = type;
                    break;
                }
            }
        }
    }
    
    // If not found, scan all types for the device
    if (!targetType)
    {
        for (auto& type : deviceManager.getAvailableDeviceTypes())
        {
            type->scanForDevices();
            if (type->getDeviceNames().contains(deviceName))
            {
                targetType = type;
                juce::Logger::writeToLog("AudioEngine: Found device under type: " + type->getTypeName());
                break;
            }
        }
    }
    
    if (!targetType)
    {
        juce::Logger::writeToLog("AudioEngine: Device '" + deviceName + "' not found under any driver type");
        return false;
    }
    
    juce::Logger::writeToLog("AudioEngine: Using driver type: " + targetType->getTypeName());
    deviceManager.setCurrentAudioDeviceType(targetType->getTypeName(), true);
    
    // Setup for specific device
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    setup.inputDeviceName = deviceName;
    setup.outputDeviceName = deviceName;
    setup.useDefaultInputChannels = false;
    setup.useDefaultOutputChannels = false;
    setup.inputChannels.setRange(0, 64, true);
    setup.outputChannels.setRange(0, 64, true);   // Open all available outputs
    setup.sampleRate = settings->getSampleRate();
    setup.bufferSize = settings->getBufferSize();
    
    juce::Logger::writeToLog("AudioEngine: Configuring device with sample rate=" + juce::String(setup.sampleRate) + 
                            ", buffer size=" + juce::String(setup.bufferSize));
    
    juce::String error = deviceManager.setAudioDeviceSetup(setup, true);
    
    if (error.isEmpty())
    {
        juce::Logger::writeToLog("AudioEngine: Device configured successfully!");
        
        auto* device = deviceManager.getCurrentAudioDevice();
        if (device)
        {
            int numInputs = device->getActiveInputChannels().countNumberOfSetBits();
            int numOutputs = device->getActiveOutputChannels().countNumberOfSetBits();
            juce::Logger::writeToLog("AudioEngine: Active input channels = " + juce::String(numInputs));
            juce::Logger::writeToLog("AudioEngine: Active output channels = " + juce::String(numOutputs));
            
            if (numInputs == 0)
                juce::Logger::writeToLog("AudioEngine: WARNING - No input channels enabled!");
        }
        
        deviceManager.addAudioCallback(this);
        settings->setAudioDevice(deviceName);
        settings->setAudioDriverType(targetType->getTypeName());
        
        if (auto* device2 = deviceManager.getCurrentAudioDevice())
        {
            sampleRate = device2->getCurrentSampleRate();
            juce::Logger::writeToLog("AudioEngine: Device sample rate = " + juce::String(sampleRate));
            for (auto& track : tracks)
                track->setSampleRate(sampleRate);
        }
        
        return true;
    }
    else
    {
        juce::Logger::writeToLog("AudioEngine: Failed to configure device: " + error);
        return false;
    }
}

juce::StringArray AudioEngine::getAvailableDevices()
{
    juce::StringArray devices;
    
    for (auto& type : deviceManager.getAvailableDeviceTypes())
    {
        type->scanForDevices();
        devices.addArray(type->getDeviceNames());
    }
    
    return devices;
}

juce::String AudioEngine::getCurrentDevice() const
{
    auto* device = deviceManager.getCurrentAudioDevice();
    return device ? device->getName() : juce::String();
}

bool AudioEngine::setAudioDevice(const juce::String& deviceName)
{
    deviceManager.removeAudioCallback(this);
    
    bool success = initializeWithDevice(deviceName);
    
    if (success)
    {
        refreshTracksFromSettings();
    }
    
    return success;
}

int AudioEngine::getNumInputChannels() const
{
    auto* device = deviceManager.getCurrentAudioDevice();
    return device ? device->getActiveInputChannels().countNumberOfSetBits() : 0;
}

int AudioEngine::getNumOutputChannels() const
{
    auto* device = deviceManager.getCurrentAudioDevice();
    return device ? device->getActiveOutputChannels().countNumberOfSetBits() : 0;
}

void AudioEngine::refreshTracksFromSettings()
{
    // Use pure user track count (stereo return handled separately at fixed index 2)
    int newTrackCount = (currentSession != nullptr)
                        ? currentSession->getTrackCount()
                        : settings->getTrackCount();
    
    juce::Logger::writeToLog("refreshTracksFromSettings: userTracks=" + juce::String(userTrackCount) 
                           + " requested=" + juce::String(newTrackCount));
    
    if (newTrackCount == userTrackCount && clickTrackIndex >= 0 && timecodeTrackIndex >= 0)
    {
        // Count unchanged — just update sample rates
        for (auto& t : tracks)
            t->setSampleRate(sampleRate);
        return;
    }
    
    createTracks(newTrackCount);
}

void AudioEngine::rebuildTracksFromSession(Session* session)
{
    if (session)
    {
        currentSession = session;
        // Pass pure user track count — createTracks handles stereo at index 2 separately
        int pureUserCount = session->getTrackCount();
        juce::Logger::writeToLog("rebuildTracksFromSession: forcing createTracks(" +
                                 juce::String(pureUserCount) + " user tracks)");
        createTracks(pureUserCount);
    }
    else
    {
        refreshTracksFromSettings();
    }
}

void AudioEngine::loadSessionRecordings()
{
    juce::File recordingsDir = settings->getSessionRecordingsDirectory();
    
    if (!recordingsDir.exists())
    {
        juce::Logger::writeToLog("No recordings directory found for session");
        return;
    }
    
    // Find all WAV files in recordings directory
    juce::Array<juce::File> wavFiles;
    recordingsDir.findChildFiles(wavFiles, juce::File::findFiles, false, "*.wav");
    
    juce::Logger::writeToLog("Loading " + juce::String(wavFiles.size()) + " recordings from session");
    
    // Load each recording into its corresponding track
    for (const auto& wavFile : wavFiles)
    {
        juce::String filename = wavFile.getFileNameWithoutExtension();
        
        // Parse Track_##_Name format
        if (filename.startsWith("Track_"))
        {
            juce::String trackNumStr = filename.substring(6, 8);
            int trackNum = trackNumStr.getIntValue();
            int trackIndex = trackNum - 1;  // Track numbers are 1-based
            
            if (trackIndex >= 0 && trackIndex < tracks.size())
            {
                // Load audio file into track for playback
                tracks[trackIndex]->loadAudioFile(wavFile);
                juce::Logger::writeToLog("Loaded " + wavFile.getFileName() + " into Track " + juce::String(trackNum));
            }
        }
    }
}

void AudioEngine::createTracks(int numUserTracks)
{
    tracks.clear();
    userTrackCount = numUserTracks;  // pure user track count, never includes stereo/click/ltc

    int sessionBitDepth = currentSession ? currentSession->getBitDepth() : 24;
    bool hasStereo = currentSession ? currentSession->getHasStereoReturn() : false;

    juce::Logger::writeToLog("createTracks: Click@0 LTC@1 Stereo@2 users@3+ (" +
                             juce::String(numUserTracks) + " user tracks" +
                             juce::String(hasStereo ? ", stereo return" : "") + ")");

    // ---- Index 0: Click track ----
    clickTrackIndex = kClickTrackIndex;
    auto clickTrack = std::make_unique<Track>(1);
    clickTrack->setSampleRate(sampleRate);
    clickTrack->setTrackType(TrackType::Click);
    clickTrack->setName("Click");
    tracks.push_back(std::move(clickTrack));

    // ---- Index 1: LTC timecode track ----
    timecodeTrackIndex = kLTCTrackIndex;
    auto tcTrack = std::make_unique<Track>(2);
    tcTrack->setSampleRate(sampleRate);
    tcTrack->setTrackType(TrackType::Timecode);
    tcTrack->setName("LTC");
    tracks.push_back(std::move(tcTrack));

    // ---- Index 2: Stereo return — always present so engine index is stable ----
    auto stereoTrack = std::make_unique<Track>(3);
    stereoTrack->setSampleRate(sampleRate);
    stereoTrack->setBitDepth(sessionBitDepth);
    if (hasStereo)
    {
        stereoTrack->setTrackType(TrackType::Mixdown);
        stereoTrack->setName("Stereo Return");
        // Load stereo audio if session has it
        if (currentSession)
        {
            const auto& trackInfos = currentSession->getTracks();
            // Stereo return is the last session track info when hasStereoReturn
            for (int ti = (int)trackInfos.size() - 1; ti >= 0; --ti)
            {
                if (trackInfos[ti].isStereoReturn && !trackInfos[ti].recordingFile.isEmpty())
                {
                    juce::File af = currentSession->getAudioFolder()
                                        .getChildFile(trackInfos[ti].recordingFile);
                    if (af.existsAsFile()) stereoTrack->loadAudioFile(af);
                    break;
                }
            }
        }
    }
    else
    {
        stereoTrack->setName("Stereo");  // placeholder — not shown in UI when no stereo return
    }
    tracks.push_back(std::move(stereoTrack));

    juce::Logger::writeToLog("  Click@" + juce::String(clickTrackIndex) +
                             "  LTC@" + juce::String(timecodeTrackIndex) +
                             "  Stereo@" + juce::String(kStereoReturnIndex) +
                             "  Users@" + juce::String(kFirstUserTrackIndex) +
                             ".." + juce::String(kFirstUserTrackIndex + numUserTracks - 1));

    // ---- Indices 3+: User tracks ----
    for (int i = 0; i < numUserTracks; ++i)
    {
        auto track = std::make_unique<Track>(i + 1);
        track->setSampleRate(sampleRate);
        track->setBitDepth(sessionBitDepth);

        if (currentSession)
        {
            const auto& trackInfos = currentSession->getTracks();
            if (i < (int)trackInfos.size() && !trackInfos[i].isStereoReturn)
            {
                if (trackInfos[i].name.isNotEmpty())
                    track->setName(trackInfos[i].name);
                if (!trackInfos[i].recordingFile.isEmpty())
                {
                    juce::File af = currentSession->getAudioFolder()
                                        .getChildFile(trackInfos[i].recordingFile);
                    if (af.existsAsFile())
                    {
                        track->loadAudioFile(af);
                        juce::Logger::writeToLog("  Track " + juce::String(i+1) +
                                                 ": restored " + af.getFileName());
                    }
                }
            }
        }
        tracks.push_back(std::move(track));
    }

    juce::Logger::writeToLog("createTracks complete: " + juce::String(tracks.size()) + " total tracks");
}

void AudioEngine::setBPM(double newBpm)
{
    bpm = juce::jmax(20.0, juce::jmin(400.0, newBpm));
    
    // Also update session BPM if we have a session
    if (auto* session = getCurrentSession())
        session->setBPM(bpm);
    
    if (oscController)
        oscController->sendBPM(bpm);
}

double AudioEngine::getBPM() const
{
    // Return session BPM if we have a session, otherwise return engine BPM
    if (auto* session = getCurrentSession())
        return session->getBPM();
    return bpm;
}

void AudioEngine::setPlaybackSpeed(double speed)
{
    // Clamp to reasonable range: 0.25x (2 octaves down) to 4.0x (2 octaves up)
    playbackSpeed.store(juce::jlimit(0.25, 4.0, speed));
    
    // Calculate semitone offset from 1.0x (12 * log2(speed))
    int semitones = static_cast<int>(std::round(12.0 * std::log2(playbackSpeed.load())));
    
    if (oscController)
        oscController->sendSpeed(playbackSpeed.load(), semitones);
}

void AudioEngine::adjustSpeedBySemitones(int semitones)
{
    // Each semitone is 2^(1/12)
    double currentSpeed = playbackSpeed.load();
    double newSpeed = currentSpeed * std::pow(2.0, semitones / 12.0);
    setPlaybackSpeed(newSpeed);
}

void AudioEngine::adjustSpeedByOctave(int octaves)
{
    // Octave is 12 semitones = 2^octaves
    double currentSpeed = playbackSpeed.load();
    double newSpeed = currentSpeed * std::pow(2.0, octaves);
    setPlaybackSpeed(newSpeed);
}

void AudioEngine::setClickEnabled(bool enabled)
{
    clickEnabled.store(enabled);
    if (oscController)
        oscController->sendClickEnabled(enabled);
    juce::Logger::writeToLog("AudioEngine: Click " + juce::String(enabled ? "ENABLED" : "DISABLED"));
}

void AudioEngine::setLTCInputEnabled(bool enabled, bool slaveTransport)
{
    ltcInputEnabled.store(enabled);
    ltcSlaveEnabled.store(slaveTransport);
    
    if (enabled)
    {
        // Reset decoder state
        ltcDec = LTCDecoderState{};
        juce::Logger::writeToLog("AudioEngine: LTC input enabled, slave=" + juce::String(slaveTransport ? "yes" : "no"));
    }
    else
    {
        juce::Logger::writeToLog("AudioEngine: LTC input disabled");
    }
}

// Decode a full 80-bit LTC frame.
// Returns true if the frame has a valid sync word and fills hh/mm/ss/ff.
bool AudioEngine::decodeLTCFrame(const uint8_t bits[80], int& hh, int& mm, int& ss, int& ff)
{
    // Validate sync word: bits[64..79] = 0011 1111 1111 1101
    const uint8_t syncWord[16] = {0,0,1,1,1,1,1,1,1,1,1,1,1,1,0,1};
    for (int i = 0; i < 16; ++i)
        if (bits[64 + i] != syncWord[i])
            return false;
    
    // Extract BCD fields (LSB first within each field)
    auto bcd4 = [&](int startBit) {
        return (bits[startBit] | (bits[startBit+1]<<1) | (bits[startBit+2]<<2) | (bits[startBit+3]<<3));
    };
    
    int frameUnits = bcd4(0);
    int frameTens  = bcd4(4) & 0x3;  // only 2 bits for frame tens
    int secUnits   = bcd4(8);
    int secTens    = bcd4(12) & 0x7;
    int minUnits   = bcd4(16);
    int minTens    = bcd4(20) & 0x7;
    int hrUnits    = bcd4(24);
    int hrTens     = bcd4(28) & 0x3;
    
    ff = frameTens * 10 + frameUnits;
    ss = secTens   * 10 + secUnits;
    mm = minTens   * 10 + minUnits;
    hh = hrTens    * 10 + hrUnits;
    
    // Sanity check
    if (hh > 23 || mm > 59 || ss > 59 || ff > 29)
        return false;
    
    return true;
}

// Process one buffer of input audio looking for LTC biphase mark code.
// Updates ltcDec state and (if slaveEnabled) sets currentSamplePosition.
void AudioEngine::processLTCInput(const float* samples, int numSamples, juce::int64 bufferStartSample)
{
    if (!ltcInputEnabled.load() || !samples)
        return;
    
    // Nominal samples per bit for 30fps at current sample rate
    // 30fps × 80 bits/frame = 2400 bits/second
    const double halfBitSamples = sampleRate / (30.0 * 80.0 * 2.0);   // samples per half-bit
    const double thresholdSamples = halfBitSamples * 1.5;               // long vs short threshold
    
    for (int s = 0; s < numSamples; ++s)
    {
        float samp = samples[s];
        juce::int64 absPos = bufferStartSample + s;
        
        // Zero-crossing detector (positive-going only to avoid double-triggers)
        bool isCrossing = (ltcDec.lastSample <= 0.0f && samp > 0.0f) ||
                          (ltcDec.lastSample >= 0.0f && samp < 0.0f);
        ltcDec.lastSample = samp;
        
        if (!isCrossing)
            continue;
        
        // Compute interval from last crossing
        juce::int64 interval = absPos - ltcDec.lastCrossingAbs;
        ltcDec.lastCrossingAbs = absPos;
        
        if (interval <= 0 || interval > (juce::int64)(thresholdSamples * 4))
        {
            // Too long or invalid — reset
            ltcDec.pendingCrossingAbs = -1;
            continue;
        }
        
        // BMC decoding:
        // Short interval (< threshold) = midpoint of 1-bit
        // Long interval (>= threshold) = boundary of 0-bit (or next bit start)
        bool isShort = (interval < (juce::int64)thresholdSamples);
        
        if (isShort)
        {
            if (ltcDec.pendingCrossingAbs >= 0)
            {
                // Two short intervals in a row = one 1-bit
                uint8_t bit = 1;
                ltcDec.bits[ltcDec.bitsCollected % 80] = bit;
                ltcDec.bitsCollected++;
                ltcDec.pendingCrossingAbs = -1;
            }
            else
            {
                // First short interval — remember it, wait for second
                ltcDec.pendingCrossingAbs = absPos;
            }
        }
        else
        {
            // Long interval
            if (ltcDec.pendingCrossingAbs >= 0)
            {
                // Had a pending short that didn't get paired — that was a 0-bit boundary
                uint8_t bit = 0;
                ltcDec.bits[ltcDec.bitsCollected % 80] = bit;
                ltcDec.bitsCollected++;
                ltcDec.pendingCrossingAbs = -1;
            }
            else
            {
                // Normal long = 0-bit
                uint8_t bit = 0;
                ltcDec.bits[ltcDec.bitsCollected % 80] = bit;
                ltcDec.bitsCollected++;
            }
        }
        
        // After collecting 80 bits, attempt frame decode
        if (ltcDec.bitsCollected > 0 && ltcDec.bitsCollected % 80 == 0)
        {
            int hh, mm, ss, ff;
            if (decodeLTCFrame(ltcDec.bits, hh, mm, ss, ff))
            {
                double timeSeconds = hh * 3600.0 + mm * 60.0 + ss + ff / 30.0;
                bool positionChanged = std::abs(timeSeconds - ltcDec.lastDecodedTimecode) > 0.001;
                ltcDec.lastDecodedTimecode = timeSeconds;
                ltcDec.consecutiveFailures = 0;
                ltcLastDecodedTime.store(timeSeconds);  // Make available to UI thread
                
                if (ltcSlaveEnabled.load())
                {
                    // Slave transport position to decoded timecode
                    juce::int64 newPos = (juce::int64)(timeSeconds * sampleRate);
                    currentSamplePosition.store((double)newPos);
                    
                    // Auto-start playback when receiving valid LTC
                    if (!isPlaying && !isRecording)
                        isPlaying = true;
                }
                
                // Send via OSC when position changes
                if (oscController && positionChanged)
                    oscController->sendTimecode(timeSeconds);
            }
            else
            {
                ltcDec.consecutiveFailures++;
                if (ltcDec.consecutiveFailures > 5)
                {
                    // Lost lock — stop playback if slaving
                    if (ltcSlaveEnabled.load())
                        isPlaying = false;
                }
            }
        }
    }
}

void AudioEngine::play()
{
    isPlaying = true;
    isPaused = false;
    isRecording = false;
    
    // Send OSC feedback if controller is connected
    if (oscController)
        oscController->sendAllTransportStates(true, false, false, false, false);
}

void AudioEngine::stop()
{
    isPlaying = false;
    isRecording = false;
    isPaused = false;
    isRewinding = false;
    isFastForwarding = false;
    // NOTE: do NOT reset LTC state here — the biphase clock runs continuously
    // through stop/start so the receiver sees uninterrupted signal.

    // Stop all recording tracks
    for (auto& track : tracks)
    {
        if (!track->isVirtualTrack())
            track->stopRecording();
    }
    
    // Send OSC feedback if controller is connected
    if (oscController)
        oscController->sendAllTransportStates(false, true, false, false, false);
}

void AudioEngine::pause()
{
    isPaused = true;
    isPlaying = false;
    // Keep isRecording state - can resume recording after pause
    
    // Send OSC feedback if controller is connected
    // Keep play button lit during pause
    if (oscController)
        oscController->sendAllTransportStates(true, false, isRecording, false, false, true);  // play=true, pause=true
}

void AudioEngine::record()
{
    juce::Logger::writeToLog("AudioEngine::record() called");
    
    isPlaying = true;
    isPaused = false;
    isRecording = true;
    
    // DON'T reset position - record from current tape position (punch-in recording)
    // Tracks will pad with silence from 0 to current position if needed
    double punchInPosition = currentSamplePosition.load();
    juce::Logger::writeToLog("  Punch-in recording at position: " + juce::String(punchInPosition / sampleRate) + " seconds");
    
    // Get recordings directory
    juce::File recordingsDir;
    
    if (currentSession)
    {
        // Use session's Audio folder
        recordingsDir = currentSession->getAudioFolder();
        juce::Logger::writeToLog("  Using session Audio folder: " + recordingsDir.getFullPathName());
    }
    else
    {
        // Fallback to old settings method
        recordingsDir = settings->getSessionRecordingsDirectory();
        juce::Logger::writeToLog("  Using settings recordings directory: " + recordingsDir.getFullPathName());
    }
    
    if (!recordingsDir.exists())
    {
        juce::Logger::writeToLog("  Creating recordings directory...");
        juce::Result result = recordingsDir.createDirectory();
        if (result.failed())
        {
            juce::Logger::writeToLog("  ERROR: Failed to create directory: " + result.getErrorMessage());
            return;
        }
    }
    
    juce::Logger::writeToLog("  Starting recording on armed tracks only...");
    juce::Logger::writeToLog("  Punch-in position (same for all tracks): " + juce::String(punchInPosition) + " samples");

    // Find out how many hardware input channels are available so we can warn/wrap
    int hwInputCount = 0;
    if (auto* dev = deviceManager.getCurrentAudioDevice())
        hwInputCount = dev->getActiveInputChannels().countNumberOfSetBits();
    juce::Logger::writeToLog("  Hardware input channels available: " + juce::String(hwInputCount));
    
    int armedCount = 0;
    for (auto& track : tracks)
    {
        if (!track->isArmed())
        {
            juce::Logger::writeToLog("  Track " + juce::String(track->getTrackNumber()) + " not armed - skipping");
            continue;
        }
        
        armedCount++;
        
        // Generate filename
        juce::String filename;
        if (currentSession)
        {
            filename = "Track" + juce::String(track->getTrackNumber()) + ".wav";
        }
        else
        {
            filename = "Track_" + juce::String(track->getTrackNumber()).paddedLeft('0', 2) + 
                      "_" + track->getName().replaceCharacters(" /\\:*?\"<>|", "__________") + 
                      ".wav";
        }
        
        juce::File trackFile = recordingsDir.getChildFile(filename);
        
        juce::Logger::writeToLog("  Track " + juce::String(track->getTrackNumber()) + " ARMED - recording to: " + trackFile.getFullPathName());
        
        // Log which physical input this track is mapped to
        // Engine index = kVirtualTrackCount + (userTrackNumber - 1)
        int engineTrackIdx = kVirtualTrackCount + (track->getTrackNumber() - 1);
        int mappedInput = settings->getTrackInputMapping(engineTrackIdx);
        juce::Logger::writeToLog("    -> input mapping: Input " + juce::String(mappedInput) +
                                 (hwInputCount > 0 && mappedInput >= hwInputCount
                                      ? " (WARNING: exceeds hardware count " + juce::String(hwInputCount) +
                                        " — track will record silence. Fix in Session > Input Routing)"
                                      : ""));
        
        // Pass current position so startRecording pads with silence from 0 to punch-in point
        // CRITICAL: All tracks get THE SAME startPos so they align on the timeline
        juce::int64 startPos = static_cast<juce::int64>(punchInPosition);
        juce::Logger::writeToLog("    -> startPos for this track: " + juce::String(startPos) + " samples (" + 
                                juce::String(startPos / sampleRate, 3) + " seconds of silence padding)");
        
        track->startRecording(trackFile, startPos);
        
        // Confirm the track actually entered recording state
        juce::Logger::writeToLog("    -> track->isRecording() = " + juce::String(track->isRecording() ? "YES" : "NO"));
    }
    
    juce::Logger::writeToLog("  record() complete: " + juce::String(armedCount) + " armed track(s) started");
    
    // Auto-save session after starting recording (persists track names + session XML)
    saveSessionToDisk();
    
    // Send OSC feedback if controller is connected
    if (oscController)
        oscController->sendAllTransportStates(false, false, true, false, false);
}

void AudioEngine::rewind()
{
    // Start continuous rewind (like holding button on tape machine)
    isRewinding.store(true);
    isFastForwarding.store(false);  // Can't do both
    
    // Stop playback/recording during rewind
    isPlaying.store(false);
    isRecording.store(false);
    
    juce::Logger::writeToLog("AudioEngine: Starting continuous rewind");
    
    // Send OSC feedback if controller is connected
    if (oscController)
        oscController->sendAllTransportStates(false, false, false, true, false);
}

void AudioEngine::stopRewind()
{
    // Stop continuous rewind (release button)
    isRewinding.store(false);
    juce::Logger::writeToLog("AudioEngine: Stopped continuous rewind");
}

void AudioEngine::fastForward()
{
    // Start continuous fast forward (like holding button on tape machine)
    isFastForwarding.store(true);
    isRewinding.store(false);  // Can't do both
    
    // Stop playback/recording during fast forward
    isPlaying.store(false);
    isRecording.store(false);
    
    juce::Logger::writeToLog("AudioEngine: Starting continuous fast forward");
    
    // Send OSC feedback if controller is connected
    if (oscController)
        oscController->sendAllTransportStates(false, false, false, false, true);
}

void AudioEngine::stopFastForward()
{
    // Stop continuous fast forward (release button)
    isFastForwarding.store(false);
    juce::Logger::writeToLog("AudioEngine: Stopped continuous fast forward");
}

// Reverse playback toggle
void AudioEngine::toggleReverse()
{
    bool newState = !isReversing.load();
    isReversing.store(newState);
    juce::Logger::writeToLog("AudioEngine: Reverse playback " + juce::String(newState ? "ENABLED" : "DISABLED"));
    
    // Send OSC feedback
    if (oscController)
        oscController->sendMessage("/transport/reverse", newState ? 1.0f : 0.0f);
}

// Section point management
void AudioEngine::setSectionStart(double timeInSeconds)
{
    sectionStart.store(timeInSeconds);
    juce::Logger::writeToLog("AudioEngine: Section start set to " + juce::String(timeInSeconds, 2) + "s");
    
    // Send OSC feedback
    if (oscController)
        oscController->sendMessage("/transport/section/start", static_cast<float>(timeInSeconds));
}

void AudioEngine::setSectionEnd(double timeInSeconds)
{
    sectionEnd.store(timeInSeconds);
    juce::Logger::writeToLog("AudioEngine: Section end set to " + juce::String(timeInSeconds, 2) + "s");
    
    // Send OSC feedback
    if (oscController)
        oscController->sendMessage("/transport/section/end", static_cast<float>(timeInSeconds));
}

void AudioEngine::clearSection()
{
    sectionStart.store(-1.0);
    sectionEnd.store(-1.0);
    sectionLoop.store(false);
    juce::Logger::writeToLog("AudioEngine: Section cleared");
    
    // Send OSC feedback
    if (oscController)
    {
        oscController->sendMessage("/transport/section/clear", 1.0f);
        oscController->sendMessage("/transport/section/loop", 0.0f);
    }
}

void AudioEngine::toggleSectionLoop()
{
    bool newState = !sectionLoop.load();
    sectionLoop.store(newState);
    juce::Logger::writeToLog("AudioEngine: Section loop " + juce::String(newState ? "ENABLED" : "DISABLED"));
    
    // Send OSC feedback
    if (oscController)
        oscController->sendMessage("/transport/section/loop", newState ? 1.0f : 0.0f);
}

void AudioEngine::setTrackArmed(int trackIndex, bool armed)
{
    juce::Logger::writeToLog("AudioEngine::setTrackArmed() - trackIndex=" + juce::String(trackIndex + 1) + 
                             ", armed=" + juce::String(armed ? "TRUE" : "FALSE"));
    
    if (trackIndex >= 0 && trackIndex < tracks.size())
    {
        tracks[trackIndex]->setArmed(armed);
        juce::Logger::writeToLog("  AudioEngine Track " + juce::String(trackIndex + 1) + " armed state set successfully");
        
        // Send OSC feedback if controller is connected
        if (oscController)
            oscController->sendTrackArmState(trackIndex, armed);
    }
    else
    {
        juce::Logger::writeToLog("  ERROR: Invalid track index " + juce::String(trackIndex));
    }
}

void AudioEngine::setTrackMonitoring(int trackIndex, bool monitoring)
{
    juce::Logger::writeToLog("AudioEngine::setTrackMonitoring() - trackIndex=" +
                            juce::String(trackIndex + 1) +
                            ", monitoring=" + juce::String(monitoring ? "TRUE" : "FALSE"));

    if (trackIndex >= 0 && trackIndex < tracks.size())
    {
        tracks[trackIndex]->setMonitoring(monitoring);

        if (oscController)
            oscController->sendTrackMonitorState(trackIndex, monitoring);
    }
    else
    {
        juce::Logger::writeToLog("  ERROR: Invalid track index " + juce::String(trackIndex));
    }
}

void AudioEngine::setTrackRecording(int trackIndex, bool recording)
{
    if (trackIndex >= 0 && trackIndex < tracks.size())
    {
        if (recording)
        {
            juce::File projectDir = settings->getProjectDirectory();
            projectDir.createDirectory();
            
            juce::File trackFile = projectDir.getChildFile(
                "Track_" + juce::String(trackIndex + 1).paddedLeft('0', 2) + 
                "_" + tracks[trackIndex]->getName().replaceCharacters(" /\\:*?\"<>|", "__________") + 
                ".wav");
            juce::int64 startPos = static_cast<juce::int64>(currentSamplePosition.load());
            tracks[trackIndex]->startRecording(trackFile, startPos);
            isRecording = true;
            isPlaying = true;
        }
        else
        {
            tracks[trackIndex]->stopRecording();
        }
    }
}

Track* AudioEngine::getTrack(int trackIndex)
{
    if (trackIndex >= 0 && trackIndex < tracks.size())
        return tracks[trackIndex].get();
    return nullptr;
}

void AudioEngine::setTrackName(int trackIndex, const juce::String& name)
{
    // Update in-memory Track object
    if (trackIndex >= 0 && trackIndex < (int)tracks.size())
        tracks[trackIndex]->setName(name);

    if (!currentSession) return;

    auto& trackInfos = currentSession->getTracks();

    if (trackIndex == kStereoReturnIndex)
    {
        // Stereo return: find its TrackInfo by isStereoReturn flag
        for (auto& ti : trackInfos)
            if (ti.isStereoReturn) { ti.name = name; break; }
    }
    else if (trackIndex >= kFirstUserTrackIndex)
    {
        // User track: session index is 0-based
        int sessionIdx = trackIndex - kFirstUserTrackIndex;
        if (sessionIdx < (int)trackInfos.size() && !trackInfos[sessionIdx].isStereoReturn)
            trackInfos[sessionIdx].name = name;
    }
    // Click (0) and LTC (1) tracks don't persist names
}

juce::String AudioEngine::getTrackName(int trackIndex) const
{
    if (trackIndex >= 0 && trackIndex < (int)tracks.size())
        return tracks[trackIndex]->getName();
    return {};
}

bool AudioEngine::saveSessionToDisk()
{
    bool ok = true;
    // Save session XML (track names, recording files, tempo)
    if (currentSession)
    {
        juce::File sessionFile = currentSession->getSessionFilePath();
        if (sessionFile != juce::File())
            ok &= currentSession->saveToFile(sessionFile);
    }
    // Save settings XML (routing, device, etc.)
    ok &= settings->saveSession();
    return ok;
}

bool AudioEngine::hasArmedTracks() const
{
    for (const auto& track : tracks)
    {
        if (track && track->isArmed())
            return true;
    }
    return false;
}

void AudioEngine::setNumTracks(int numTracks)
{
    createTracks(numTracks);
    settings->setNumTracks(numTracks);
}

void AudioEngine::setSampleRate(double newSampleRate)
{
    sampleRate = newSampleRate;
    for (auto& track : tracks)
    {
        track->setSampleRate(sampleRate);
    }
}

juce::File AudioEngine::getSessionDirectory() const
{
    return settings->getProjectDirectory();
}

TransportState AudioEngine::getTransportState() const
{
    TransportState state;
    state.isPlaying = isPlaying;
    state.isRecording = isRecording;
    state.isPaused = isPaused;
    state.currentTime = currentSamplePosition / sampleRate;
    state.currentSample = currentSamplePosition.load();
    state.sampleRate = sampleRate;
    
    // Calculate bar.beat from current position and session tempo
    if (currentSession)
    {
        state.bpm = currentSession->getBPM();
        int numerator = currentSession->getTimeSignatureNumerator();
        int denominator = currentSession->getTimeSignatureDenominator();
        
        // Calculate total beats from start
        double secondsPerBeat = 60.0 / state.bpm;
        double samplesPerBeat = secondsPerBeat * sampleRate;
        double totalBeats = state.currentSample / samplesPerBeat;
        
        // Calculate bar, beat, tick
        double beatsPerBar = numerator;  // 4 beats in 4/4, 3 beats in 3/4, etc.
        state.bar = (int)(totalBeats / beatsPerBar) + 1;  // Bars start at 1
        double beatInBar = fmod(totalBeats, beatsPerBar);
        state.beat = (int)beatInBar + 1;  // Beats start at 1
        
        // Ticks: 100 ticks per beat (like REAPER)
        double tickInBeat = (beatInBar - floor(beatInBar)) * 100.0;
        state.tick = (int)tickInBeat;
    }
    else
    {
        // No session - default values
        state.bpm = 120.0;
        state.bar = 1;
        state.beat = 1;
        state.tick = 0;
    }
    
    return state;
}

//==============================================================================
// Click Track Generator — generates metronome click audio for the click track
void AudioEngine::generateClickAudio(float* left, float* right, int numSamples, double startSamplePos)
{
    if (!clickEnabled.load())
    {
        std::fill(left, left + numSamples, 0.0f);
        std::fill(right, right + numSamples, 0.0f);
        return;  // No phase tracking needed — click derived from position, not freerunning
    }
    
    // Beat timing derived directly from project position so click is always on-grid
    // after seek, RTZ, or any transport change
    double samplesPerBeat = (60.0 * sampleRate) / bpm;
    double clickLen = sampleRate * 0.010;  // 10ms burst
    
    for (int s = 0; s < numSamples; ++s)
    {
        double pos      = startSamplePos + s;
        double beatPos  = pos / samplesPerBeat;           // fractional beat number
        double beatFrac = beatPos - std::floor(beatPos);  // 0..1 within current beat
        double beatPhase = beatFrac * samplesPerBeat;     // sample offset within beat
        
        float sample = 0.0f;
        if (beatPhase < clickLen)
        {
            juce::int64 beatNum = static_cast<juce::int64>(beatPos);
            bool isDownbeat = (beatNum % 4 == 0);
            float freq = isDownbeat ? 1000.0f : 660.0f;
            float env  = (float)(1.0 - beatPhase / clickLen) * 0.8f;
            sample = env * std::sin(2.0f * juce::MathConstants<float>::pi * freq
                                    * (float)beatPhase / (float)sampleRate);
        }
        
        left[s]  = sample;
        right[s] = sample;
    }
}

//==============================================================================
// LTC (SMPTE Linear Timecode) Generator — produces LTC audio for the timecode track
// Encoding: 30fps non-drop, biphase mark code, audio at ~0.5 amplitude
void AudioEngine::buildLTCFrame(int h, int m, int s, int f, uint8_t bits[80])
{
    // BCD encode helpers — LTC uses Binary-Coded Decimal, NOT raw binary
    // Each time field is split into units (val % 10) and tens (val / 10),
    // each stored LSB-first.  Using raw binary instead of BCD is the most
    // common reason LTC decoders reject frames (they simply misread the time).
    const int fU = f % 10, fT = f / 10;
    const int sU = s % 10, sT = s / 10;
    const int mU = m % 10, mT = m / 10;
    const int hU = h % 10, hT = h / 10;

    // Helper: push n bits of value v, LSB first — we use direct indexing below
    // (no lambda needed; raw index is clearer for the SMPTE table)

    // Build 80-bit LTC word (SMPTE 12M, 30fps non-drop, lsb-first within each field)
    int b = 0;

    // --- Word 1: Frames (bits 0–7) ---
    for (int i = 0; i < 4; ++i) bits[b++] = (fU >> i) & 1;   // bits 0-3: frame units BCD
    bits[b++] = 0;                                              // bit  4: user bits group 1
    bits[b++] = (fT >> 0) & 1;                                 // bit  5: frame tens bit 0
    bits[b++] = (fT >> 1) & 1;                                 // bit  6: frame tens bit 1
    bits[b++] = 0;                                              // bit  7: drop-frame flag (0 = NDF)

    // --- Word 2: Seconds (bits 8–15) ---
    for (int i = 0; i < 4; ++i) bits[b++] = (sU >> i) & 1;   // bits 8-11: seconds units BCD
    bits[b++] = 0;                                              // bit 12: user bits group 2
    bits[b++] = (sT >> 0) & 1;                                 // bit 13: seconds tens bit 0
    bits[b++] = (sT >> 1) & 1;                                 // bit 14: seconds tens bit 1
    bits[b++] = (sT >> 2) & 1;                                 // bit 15: seconds tens bit 2

    // --- Word 3: Minutes (bits 16–23) ---
    for (int i = 0; i < 4; ++i) bits[b++] = (mU >> i) & 1;   // bits 16-19: minutes units BCD
    bits[b++] = 0;                                              // bit 20: user bits group 3
    bits[b++] = (mT >> 0) & 1;                                 // bit 21: minutes tens bit 0
    bits[b++] = (mT >> 1) & 1;                                 // bit 22: minutes tens bit 1
    bits[b++] = (mT >> 2) & 1;                                 // bit 23: minutes tens bit 2

    // --- Word 4: Hours (bits 24–31) ---
    for (int i = 0; i < 4; ++i) bits[b++] = (hU >> i) & 1;   // bits 24-27: hours units BCD
    bits[b++] = 0;                                              // bit 28: user bits group 4
    bits[b++] = (hT >> 0) & 1;                                 // bit 29: hours tens bit 0
    bits[b++] = (hT >> 1) & 1;                                 // bit 30: hours tens bit 1
    bits[b++] = 0;                                              // bit 31: colour-frame flag

    // --- User bits groups 5–8 (bits 32–63) — all zero ---
    for (int i = 0; i < 32; ++i) bits[b++] = 0;

    // --- Sync word (bits 64–79): 0011 1111 1111 1101 ---
    bits[b++] = 0; bits[b++] = 0; bits[b++] = 1; bits[b++] = 1;
    bits[b++] = 1; bits[b++] = 1; bits[b++] = 1; bits[b++] = 1;
    bits[b++] = 1; bits[b++] = 1; bits[b++] = 1; bits[b++] = 1;
    bits[b++] = 1; bits[b++] = 1; bits[b++] = 0; bits[b++] = 1;

    jassert(b == 80);
}

void AudioEngine::generateLTCAudio(float* left, float* right, int numSamples, double startSamplePos)
{
    // Stateful biphase-mark bit clock.
    //
    // KEY DESIGN:
    //   The LTC bit clock runs unconditionally every sample (no transport gate).
    //   The timecode VALUE encoded at each frame boundary is derived from
    //   (startSamplePos + s) — the EXACT transport-relative sample of that boundary —
    //   not the coarse start-of-buffer value.  This is critical: with a 512-sample
    //   buffer and 1600-sample LTC frame, using buffer-start position causes the
    //   encoded frame numbers to drift and repeat/skip, which is exactly the
    //   "jumps between three positions" symptom.
    //
    // While stopped: startSamplePos is constant across callbacks, so the same
    //   timecode is encoded every frame (correct OTWP hold behaviour).
    // While playing: startSamplePos increments by numSamples per callback, and
    //   the exact boundary sample gives the right frame number.

    const int    fps          = 30;
    const double samplesPerBit = sampleRate / (fps * 80.0);
    const double halfBit       = samplesPerBit * 0.5;

    for (int s = 0; s < numSamples; ++s)
    {
        // ---- Load a new frame at each 80-bit boundary ----
        if (ltcBitInFrame >= 80)
        {
            ltcBitInFrame = 0;

            // Use the exact transport sample position of THIS boundary, not the
            // coarse buffer-start value.
            const double exactPos = startSamplePos + s;
            int tf  = (int)(exactPos * fps / sampleRate);  // total frames elapsed
            const int ff = tf % fps;   tf /= fps;
            const int ss = tf % 60;    tf /= 60;
            const int mm = tf % 60;    tf /= 60;
            const int hh = tf % 24;
            buildLTCFrame(hh, mm, ss, ff, ltcFrameBits);
        }

        // ---- Output current polarity ----
        left[s] = right[s] = ltcPolarity * 0.5f;

        // ---- Advance sub-bit phase by one sample ----
        ltcBitPhase += 1.0;

        // ---- Mid-bit: extra flip if data bit == 1 ----
        if ((ltcBitPhase - 1.0) < halfBit && ltcBitPhase >= halfBit)
        {
            if (ltcFrameBits[ltcBitInFrame])
                ltcPolarity = -ltcPolarity;
        }

        // ---- Bit boundary: always flip, advance to next bit ----
        if (ltcBitPhase >= samplesPerBit)
        {
            ltcBitPhase -= samplesPerBit;
            ltcPolarity  = -ltcPolarity;
            ++ltcBitInFrame;   // becomes 80 at end of frame; picked up next iteration
        }
    }
}


//==============================================================================
void AudioEngine::audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                                   int numInputChannels,
                                                   float* const* outputChannelData,
                                                   int numOutputChannels,
                                                   int numSamples,
                                                   const juce::AudioIODeviceCallbackContext& context)
{
    static int callbackCount = 0;
    if (callbackCount++ % 100 == 0)  // Log every 100th callback
    {
        juce::Logger::writeToLog("Audio callback: isPlaying=" + juce::String(isPlaying ? "YES" : "NO") +
                                 ", isRecording=" + juce::String(isRecording ? "YES" : "NO") +
                                 ", inputs=" + juce::String(numInputChannels) +
                                 ", outputs=" + juce::String(numOutputChannels));
    }
    
    // Clear output buffers
    for (int i = 0; i < numOutputChannels; ++i)
    {
        juce::FloatVectorOperations::clear(outputChannelData[i], numSamples);
    }
    
    // LTC Input: process BEFORE transport check so it can start/slave transport
    if (ltcInputEnabled.load())
    {
        int ltcCh = ltcInputChannel.load();
        if (ltcCh < 0 && settings)
            ltcCh = settings->getLTCInputChannel();
        if (ltcCh >= 0 && ltcCh < numInputChannels && inputChannelData && inputChannelData[ltcCh])
            processLTCInput(inputChannelData[ltcCh], numSamples, 
                           (juce::int64)currentSamplePosition.load());
    }
    
    // Output audio when transport is moving, or during scrub preview
    bool scrubbing = scrubPreviewSamplesRemaining.load() > 0;
    bool transportActive = isPlaying || isRewinding.load() || isFastForwarding.load() || (isPaused.load() && scrubbing);
    
    // ---- LTC (Timecode) output ----
    // LTC output always runs — no transport gate.
    // ---- LTC timecode output — always runs, no gate ----
    // LTC is always active; the user controls which hardware output it reaches via Output Routing.

    // Log LTC routing every ~4 seconds
    static int ltcLogCounter = 0;
    if (++ltcLogCounter >= 375)
    {
        ltcLogCounter = 0;
        juce::String routed;
        if (settings)
            for (int ch = 0; ch < numOutputChannels; ++ch)
                if (settings->getTrackOutputMapping(timecodeTrackIndex, ch))
                    routed += juce::String(ch) + " ";
        juce::Logger::writeToLog("LTC: always-on  trackIdx=" + juce::String(timecodeTrackIndex) +
                                 "  routed-to=" + (routed.isEmpty() ? "NONE (set in Output Routing!)" : routed.trim()) +
                                 "  hwOut=" + juce::String(numOutputChannels));
    }

    if (timecodeTrackIndex >= 0 && timecodeTrackIndex < (int)tracks.size()
        && numOutputChannels >= 1 && outputChannelData)
    {
        // Use pre-allocated buffers (no allocation on audio thread!)
        std::fill(ltcBufferL.begin(), ltcBufferL.begin() + numSamples, 0.0f);
        std::fill(ltcBufferR.begin(), ltcBufferR.begin() + numSamples, 0.0f);
        generateLTCAudio(ltcBufferL.data(), ltcBufferR.data(), numSamples, currentSamplePosition.load());

        float peak = 0.0f;
        for (int s = 0; s < numSamples; ++s)
            peak = juce::jmax(peak, std::abs(ltcBufferL[s]));
        trackPeakLevels[timecodeTrackIndex].store(juce::jmax(peak, trackPeakLevels[timecodeTrackIndex].load() * 0.80f));

        for (int outCh = 0; outCh < numOutputChannels; ++outCh)
            if (settings->getTrackOutputMapping(timecodeTrackIndex, outCh))
                juce::FloatVectorOperations::add(outputChannelData[outCh], ltcBufferL.data(), numSamples);
    }

    // ---- Click track output ----
    // Click is position-based so only runs when transport is active (needs a valid position)
    if (transportActive && clickTrackIndex >= 0 && clickTrackIndex < (int)tracks.size()
        && numOutputChannels >= 1 && outputChannelData)
    {
        // Use pre-allocated buffers (no allocation on audio thread!)
        std::fill(clickBufferL.begin(), clickBufferL.begin() + numSamples, 0.0f);
        std::fill(clickBufferR.begin(), clickBufferR.begin() + numSamples, 0.0f);
        generateClickAudio(clickBufferL.data(), clickBufferR.data(), numSamples, currentSamplePosition.load());

        float peak = 0.0f;
        for (int s = 0; s < numSamples; ++s)
            peak = juce::jmax(peak, std::abs(clickBufferL[s]));
        trackPeakLevels[clickTrackIndex].store(juce::jmax(peak, trackPeakLevels[clickTrackIndex].load() * 0.80f));

        bool routedToAny = false;
        for (int outCh = 0; outCh < numOutputChannels; ++outCh)
        {
            if (settings->getTrackOutputMapping(clickTrackIndex, outCh))
            {
                routedToAny = true;
                juce::FloatVectorOperations::add(outputChannelData[outCh],
                    (outCh % 2 == 0) ? clickBufferL.data() : clickBufferR.data(), numSamples);
            }
        }
        (void)routedToAny; // no fallback — if not routed, click is silent
    }
    // ---- End virtual track output ----
    
    if (!transportActive)
    {
        // Fast decay to zero when stopped - don't leave VU meters frozen
        for (int i = 0; i < MAX_TRACKS; ++i)
        {
            float v = trackPeakLevels[i].load();
            if (v > 0.0001f)
                trackPeakLevels[i].store(v * 0.70f);  // Fast decay per callback
            else
                trackPeakLevels[i].store(0.0f);

            float vr = trackPeakLevelsRight[i].load();
            if (vr > 0.0001f)
                trackPeakLevelsRight[i].store(vr * 0.70f);
            else
                trackPeakLevelsRight[i].store(0.0f);
        }
        return;
    }
    
    // Get current playback position
    juce::int64 currentPos = currentSamplePosition.load();
    
    // Update disk streaming threads with current position
    // This tells them where to read ahead from
    for (auto& track : tracks)
    {
        if (track)
            track->updateStreamPosition(currentPos);
    }
    
    // Process each track
    for (size_t trackIndex = 0; trackIndex < tracks.size(); ++trackIndex)
    {
        // Check track is valid
        if (!tracks[trackIndex])
            continue;
            
        auto& track = tracks[trackIndex];
        
        // Write audio for tracks that are actively recording (armed only)
        if (isRecording && track->isRecording())
        {
            // NO LOGGING ON AUDIO THREAD - causes jitter!
            
            // Check if track is stereo
            if (track->isStereo())
            {
                // STEREO TRACK — each channel routed independently in the matrix
                // L channel: normal trackIndex key; R channel: trackIndex + kStereoRightOffset
                int inputChannelLeft  = settings->getTrackInputMapping(static_cast<int>(trackIndex));
                int inputChannelRight = settings->getTrackInputMapping(static_cast<int>(trackIndex) + kStereoRightOffset);

                // Fallback: if right channel not mapped, use left+1 (legacy behaviour)
                if (inputChannelRight < 0 && inputChannelLeft >= 0)
                    inputChannelRight = inputChannelLeft + 1;
                
                // NO LOGGING - removed for real-time performance
                
                if (inputChannelLeft >= 0 && inputChannelLeft < numInputChannels &&
                    inputChannelRight >= 0 && inputChannelRight < numInputChannels)
                {
                    juce::int64 currentPos = static_cast<juce::int64>(currentSamplePosition.load());
                    track->writeAudioStereo(inputChannelData[inputChannelLeft], 
                                            inputChannelData[inputChannelRight], 
                                            numSamples, currentPos);
                }
                else
                {
                    // Armed but no valid input mapping - write silence
                    // Use pre-allocated buffer (no allocation on audio thread!)
                    juce::int64 currentPos = static_cast<juce::int64>(currentSamplePosition.load());
                    track->writeAudioStereo(silenceBuffer.data(), silenceBuffer.data(), numSamples, currentPos);
                    
                    // NO LOGGING - removed for real-time performance
                }
            }
            else
            {
                // MONO TRACK - Get single input channel
                int inputChannel = settings->getTrackInputMapping(static_cast<int>(trackIndex));

                // NO LOGGING - removed for real-time performance

                if (inputChannel >= 0 && inputChannel < numInputChannels)
                {
                    juce::int64 currentPos = static_cast<juce::int64>(currentSamplePosition.load());
                    track->writeAudio(inputChannelData[inputChannel], numSamples, currentPos);

                    // Measure input peak for VU meter
                    float peak = 0.0f;
                    for (int s = 0; s < numSamples; ++s)
                        peak = juce::jmax(peak, std::abs(inputChannelData[inputChannel][s]));
                    float oldPeak = trackPeakLevels[trackIndex].load();
                    trackPeakLevels[trackIndex].store(juce::jmax(peak, oldPeak * 0.80f));
                }
                else
                {
                    // No valid input for this track — write silence.
                    // Use pre-allocated buffer (no allocation on audio thread!)
                    // Correct behaviour: don't substitute audio from another input.
                    // Fix the mapping in Session → Input Routing if this is wrong.
                    juce::int64 currentPos = static_cast<juce::int64>(currentSamplePosition.load());
                    track->writeAudio(silenceBuffer.data(), numSamples, currentPos);

                    // NO LOGGING - removed for real-time performance
                }
            }
        }
        
        // Virtual tracks (click/timecode) are handled separately above — skip here
        if (track->isVirtualTrack())
            continue;
        // Regular track playback (when playing, or during scrub preview when paused)
        bool scrubbing = scrubPreviewSamplesRemaining.load() > 0;
        if (track->hasAudio() && (isPlaying.load() || (isPaused.load() && scrubbing)))
        {
            if (track->isStereo())
            {
                // STEREO PLAYBACK - use pre-allocated buffers (no allocation on audio thread!)
                
                // Use resampled playback when varispeed or reverse is active
                double speed = playbackSpeed.load();
                bool reverse = isReversing.load();
                
                if (std::abs(speed - 1.0) < 0.001 && !reverse)
                {
                    // Normal speed forward - use direct reading for optimal performance
                    track->readAudioStereo(playbackBufferL.data(), playbackBufferR.data(), numSamples, currentSamplePosition.load());
                }
                else
                {
                    // Varispeed or reverse active - use resampling with interpolation
                    // For reverse, negate the speed
                    double effectiveSpeed = reverse ? -speed : speed;
                    track->readAudioStereoResampled(playbackBufferL.data(), playbackBufferR.data(), numSamples, currentSamplePosition.load(), effectiveSpeed);
                }
                
                float peakL = 0.0f, peakR = 0.0f;
                for (int s = 0; s < numSamples; ++s)
                {
                    peakL = juce::jmax(peakL, std::abs(playbackBufferL[s]));
                    peakR = juce::jmax(peakR, std::abs(playbackBufferR[s]));
                }
                trackPeakLevels[trackIndex].store(
                    juce::jmax(peakL, trackPeakLevels[trackIndex].load() * 0.80f));
                trackPeakLevelsRight[trackIndex].store(
                    juce::jmax(peakR, trackPeakLevelsRight[trackIndex].load() * 0.80f));
                
                // Route L and R independently via matrix.
                // L uses trackIndex key, R uses trackIndex + kStereoRightOffset.
                // If neither has a mapping, fall back to physical outputs 0/1.
                bool anyRouted = false;
                for (int outCh = 0; outCh < numOutputChannels; ++outCh)
                {
                    if (settings->getTrackOutputMapping(static_cast<int>(trackIndex), outCh))
                    {
                        juce::FloatVectorOperations::add(outputChannelData[outCh], playbackBufferL.data(), numSamples);
                        anyRouted = true;
                    }
                    if (settings->getTrackOutputMapping(static_cast<int>(trackIndex) + kStereoRightOffset, outCh))
                    {
                        juce::FloatVectorOperations::add(outputChannelData[outCh], playbackBufferR.data(), numSamples);
                        anyRouted = true;
                    }
                }
                (void)anyRouted; // no fallback — matrix is authoritative
            }
            else
            {
                // MONO PLAYBACK - use pre-allocated buffer (no allocation on audio thread!)
                
                // Use resampled playback when varispeed or reverse is active
                double speed = playbackSpeed.load();
                bool reverse = isReversing.load();
                
                if (std::abs(speed - 1.0) < 0.001 && !reverse)
                {
                    // Normal speed forward - use direct reading for optimal performance
                    track->readAudio(playbackBufferL.data(), numSamples, currentSamplePosition.load());
                }
                else
                {
                    // Varispeed or reverse active - use resampling with interpolation
                    // For reverse, negate the speed
                    double effectiveSpeed = reverse ? -speed : speed;
                    track->readAudioResampled(playbackBufferL.data(), numSamples, currentSamplePosition.load(), effectiveSpeed);
                }
                
                float peak = 0.0f;
                for (int s = 0; s < numSamples; ++s)
                    peak = juce::jmax(peak, std::abs(playbackBufferL[s]));
                float oldPeak = trackPeakLevels[trackIndex].load();
                trackPeakLevels[trackIndex].store(juce::jmax(peak, oldPeak * 0.80f));
                
                // Route via matrix — no fallback
                for (int outCh = 0; outCh < numOutputChannels; ++outCh)
                {
                    if (settings->getTrackOutputMapping(static_cast<int>(trackIndex), outCh))
                        juce::FloatVectorOperations::add(outputChannelData[outCh], playbackBufferL.data(), numSamples);
                }
            }
        }
        else
        {
            // No audio — decay peak toward zero
            float oldPeak = trackPeakLevels[trackIndex].load();
            trackPeakLevels[trackIndex].store(oldPeak * 0.80f);
        }
    }  // end per-track for-loop
    
    // Update position - different speeds for different transport modes
    if (isPlaying)
    {
        // Normal playback - advance at varispeed rate (forward or reverse)
        double speed = playbackSpeed.load();
        bool reverse = isReversing.load();
        
        double positionDelta = numSamples * speed;
        if (reverse)
            positionDelta = -positionDelta;  // Go backwards
        
        double newPos = currentSamplePosition.load() + positionDelta;
        
        // Handle section looping if enabled
        if (sectionLoop.load() && hasSectionSet())
        {
            double secStart = sectionStart.load() * sampleRate;
            double secEnd = sectionEnd.load() * sampleRate;
            
            if (reverse)
            {
                // Reverse: loop back to end when hitting start
                if (newPos < secStart)
                    newPos = secEnd;
            }
            else
            {
                // Forward: loop back to start when hitting end
                if (newPos >= secEnd)
                    newPos = secStart;
            }
        }
        else
        {
            // No section loop - clamp to non-negative
            if (newPos < 0.0)
            {
                newPos = 0.0;
                // Auto-stop if reversing hits beginning
                if (reverse)
                    isPlaying.store(false);
            }
        }
        
        currentSamplePosition.store(newPos);
    }
    else if (isPaused.load())
    {
        // Decrement scrub preview counter
        int remaining = scrubPreviewSamplesRemaining.load();
        if (remaining > 0)
        {
            scrubPreviewSamplesRemaining.store(juce::jmax(0, remaining - numSamples));
        }
        // Position doesn't advance when paused (except via seek/scrubbing)
    }
    else if (isRewinding.load())
    {
        // Continuous rewind at 4x speed (like tape machine)
        double newPos = currentSamplePosition.load() - (numSamples * 4.0);
        if (newPos <= 0.0)
        {
            // Hit the start — stop rewinding automatically
            newPos = 0.0;
            isRewinding.store(false);
        }
        currentSamplePosition.store(newPos);
    }
    else if (isFastForwarding.load())
    {
        // Continuous fast forward at 4x speed (like tape machine)
        currentSamplePosition.store(currentSamplePosition.load() + (numSamples * 4.0));
    }
    // When paused, position only changes via seek() (scrubbing)
}



void AudioEngine::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    sampleRate = device->getCurrentSampleRate();
    currentSamplePosition = 0.0;
    
    // Pre-allocate ALL buffers for audio callback (avoid any allocation on audio thread!)
    // 8192 samples is enough for any typical buffer size (usually 64-2048)
    silenceBuffer.resize(8192, 0.0f);
    playbackBufferL.resize(8192, 0.0f);
    playbackBufferR.resize(8192, 0.0f);
    clickBufferL.resize(8192, 0.0f);
    clickBufferR.resize(8192, 0.0f);
    ltcBufferL.resize(8192, 0.0f);
    ltcBufferR.resize(8192, 0.0f);
    
    juce::Logger::writeToLog("AudioEngine: Pre-allocated audio buffers (8192 samples each)");
    
    // Reset LTC state machine fully on device (re)start
    ltcPolarity   = 1.0f;
    ltcBitPhase   = 0.0;
    ltcBitInFrame = 80;
    std::fill(std::begin(ltcFrameBits), std::end(ltcFrameBits), uint8_t(0));
    
    for (auto& track : tracks)
    {
        track->setSampleRate(sampleRate);
    }
}

void AudioEngine::audioDeviceStopped()
{
    // Cleanup
    for (auto& track : tracks)
    {
        track->stopRecording();
    }
}

void AudioEngine::seek(double timeInSeconds)
{
    juce::int64 targetSample = static_cast<juce::int64>(timeInSeconds * sampleRate);
    currentSamplePosition.store(targetSample);
    
    // Enable scrub preview - allow ~50ms of audio output for scrub feedback
    scrubPreviewSamplesRemaining.store(static_cast<int>(sampleRate * 0.05));  // 50ms at current sample rate
    
    // Reset the full LTC state machine — timecode value jumps on seek so we need a
    // clean frame boundary.  The receiver will re-sync within one frame (~33ms at 30fps).
    ltcPolarity   = 1.0f;
    ltcBitPhase   = 0.0;
    ltcBitInFrame = 80;   // force frame latch on the very next sample
    std::fill(std::begin(ltcFrameBits), std::end(ltcFrameBits), uint8_t(0));
    
    for (auto& track : tracks)
        track->updateStreamPosition(targetSample);
}

void AudioEngine::sendTimecode()
{
    if (!oscController) return;
    
    TransportState state = getTransportState();
    
    // Only send if time has changed by at least 10ms to avoid flooding OSC
    double timeDiff = std::abs(state.currentTime - lastSentTimecode);
    if (timeDiff >= 0.01)  // 10ms threshold
    {
        oscController->sendTimecode(state.currentTime);
        oscController->sendBeat(state.bar, state.beat, state.tick);
        lastSentTimecode = state.currentTime;
    }
    
    // Only send BPM if it has actually changed
    if (std::abs(state.bpm - lastSentBPM) > 0.01)
    {
        oscController->sendBPM(state.bpm);
        lastSentBPM = state.bpm;
    }
    
    // REAPER-STYLE MARKER REGIONS:
    // Display the marker for the current region (between this marker and the next one)
    // Find the most recent marker at or before current time
    Marker* currentMarker = nullptr;
    const auto& markers = markerManager.getMarkers();
    
    for (const auto& marker : markers)
    {
        // If marker is at or before current time, it's a candidate
        if (marker->getTimestamp() <= state.currentTime)
        {
            currentMarker = marker.get();
        }
        else
        {
            // Markers are sorted, so once we pass current time, we're done
            break;
        }
    }
    
    // Update marker display
    if (currentMarker)
    {
        int markerNum = currentMarker->getNumber();
        
        // Only send if marker changed (to avoid spam)
        if (markerNum != lastSentMarkerNumber)
        {
            oscController->sendMarkerInfo(markerNum, currentMarker->getTimestamp(), currentMarker->getLabel());
            lastSentMarkerNumber = markerNum;
            
            juce::Logger::writeToLog("Marker region: #" + juce::String(markerNum) + 
                                    " \"" + currentMarker->getLabel() + "\" at " + 
                                    juce::String(currentMarker->getTimestamp(), 2) + "s");
        }
    }
    else
    {
        // Before any markers - clear marker display
        if (lastSentMarkerNumber != -1)
        {
            oscController->sendMarkerInfo(0, 0.0, "");  // Send empty marker
            lastSentMarkerNumber = -1;
            juce::Logger::writeToLog("Marker region: (none - before first marker)");
        }
    }
}

//==============================================================================
// Marker Management

int AudioEngine::addMarker(double timestamp, const juce::String& label)
{
    int markerNumber = markerManager.addMarker(timestamp, label);
    
    juce::Logger::writeToLog("Marker added: #" + juce::String(markerNumber) + 
                            " at " + juce::String(timestamp, 2) + "s" +
                            (label.isNotEmpty() ? " (" + label + ")" : ""));
    
    // Send OSC feedback
    if (oscController)
    {
        oscController->sendMarkerAdded(markerNumber, timestamp, label);
    }
    
    return markerNumber;
}

bool AudioEngine::removeMarker(int markerNumber)
{
    bool success = markerManager.removeMarker(markerNumber);
    
    if (success)
    {
        juce::Logger::writeToLog("Marker removed: #" + juce::String(markerNumber));
        
        // Send OSC feedback
        if (oscController)
        {
            oscController->sendMarkerRemoved(markerNumber);
        }
    }
    
    return success;
}

void AudioEngine::goToMarker(int markerNumber)
{
    Marker* marker = markerManager.getMarkerByNumber(markerNumber);
    
    if (marker)
    {
        seek(marker->getTimestamp());
        juce::Logger::writeToLog("Jumped to marker #" + juce::String(markerNumber) + 
                                " (" + marker->getLabel() + ") at " + 
                                juce::String(marker->getTimestamp(), 2) + "s");
        
        // Send OSC feedback with marker number and name
        if (oscController)
        {
            oscController->sendMarkerInfo(markerNumber, marker->getTimestamp(), marker->getLabel());
        }
    }
    else
    {
        juce::Logger::writeToLog("Marker #" + juce::String(markerNumber) + " not found (total markers: " + 
                                juce::String(markerManager.getNumMarkers()) + ")");
    }
}

void AudioEngine::goToNextMarker()
{
    TransportState state = getTransportState();
    juce::Logger::writeToLog("goToNextMarker called - current time: " + juce::String(state.currentTime, 2) + 
                            "s, total markers: " + juce::String(markerManager.getNumMarkers()));
    
    Marker* nextMarker = markerManager.getNextMarker(state.currentTime);
    
    if (nextMarker)
    {
        seek(nextMarker->getTimestamp());
        juce::Logger::writeToLog("Jumped to next marker #" + juce::String(nextMarker->getNumber()) + 
                                " (" + nextMarker->getLabel() + ") at " + 
                                juce::String(nextMarker->getTimestamp(), 2) + "s");
        
        // Send OSC feedback with marker number and name
        if (oscController)
        {
            oscController->sendMarkerInfo(nextMarker->getNumber(), nextMarker->getTimestamp(), nextMarker->getLabel());
        }
    }
    else
    {
        juce::Logger::writeToLog("No next marker found after " + juce::String(state.currentTime, 2) + "s");
    }
}

void AudioEngine::goToPreviousMarker()
{
    TransportState state = getTransportState();
    juce::Logger::writeToLog("goToPreviousMarker called - current time: " + juce::String(state.currentTime, 2) + 
                            "s, total markers: " + juce::String(markerManager.getNumMarkers()));
    
    Marker* prevMarker = markerManager.getPreviousMarker(state.currentTime);
    
    if (prevMarker)
    {
        seek(prevMarker->getTimestamp());
        juce::Logger::writeToLog("Jumped to previous marker #" + juce::String(prevMarker->getNumber()) + 
                                " (" + prevMarker->getLabel() + ") at " + 
                                juce::String(prevMarker->getTimestamp(), 2) + "s");
        
        // Send OSC feedback with marker number and name
        if (oscController)
        {
            oscController->sendMarkerInfo(prevMarker->getNumber(), prevMarker->getTimestamp(), prevMarker->getLabel());
        }
    }
    else
    {
        juce::Logger::writeToLog("No previous marker found before " + juce::String(state.currentTime, 2) + "s");
    }
}

void AudioEngine::loadMarkersFromSession(Session* session)
{
    if (!session) return;
    
    markerManager.clearAllMarkers();
    
    const auto& markerInfos = session->getMarkers();
    juce::Logger::writeToLog("Loading " + juce::String(markerInfos.size()) + " markers from session");
    
    for (const auto& markerInfo : markerInfos)
    {
        auto markerNumber = markerManager.addMarker(markerInfo.timestamp, markerInfo.label);
        juce::Logger::writeToLog("  Loaded marker #" + juce::String(markerNumber) + 
                                " at " + juce::String(markerInfo.timestamp, 2) + "s" +
                                (markerInfo.label.isNotEmpty() ? " (" + markerInfo.label + ")" : ""));
    }
}

void AudioEngine::saveMarkersToSession(Session* session)
{
    if (!session) return;
    
    auto& markerInfos = session->getMarkers();
    markerInfos.clear();
    
    const auto& markers = markerManager.getMarkers();
    juce::Logger::writeToLog("Saving " + juce::String(markers.size()) + " markers to session");
    
    for (const auto& marker : markers)
    {
        Session::MarkerInfo markerInfo;
        markerInfo.timestamp = marker->getTimestamp();
        markerInfo.label = marker->getLabel();
        markerInfo.colour = marker->getColour().toString();
        markerInfos.push_back(markerInfo);
    }
}

void AudioEngine::skipToNextBeat()
{
    TransportState state = getTransportState();
    
    // Calculate beat duration in seconds
    double beatDuration = 60.0 / state.bpm;
    
    // Calculate current beat position (fractional)
    double currentBeat = state.currentTime / beatDuration;
    
    // Round UP to next whole beat
    int nextBeat = (int)std::ceil(currentBeat);
    
    // If we're already exactly on a beat, skip to the next one
    if (std::abs(currentBeat - nextBeat) < 0.001)
        nextBeat++;
    
    // Convert back to time
    double targetTime = nextBeat * beatDuration;
    
    // Calculate bar.beat.tick for display
    int beatsPerBar = (currentSession && currentSession->getTimeSignatureNumerator() > 0) 
                      ? currentSession->getTimeSignatureNumerator() : 4;
    int bar = (nextBeat / beatsPerBar) + 1;
    int beat = (nextBeat % beatsPerBar) + 1;
    
    juce::Logger::writeToLog("Skip to next beat: " + juce::String(bar) + "." + juce::String(beat) + ".00 (" + 
                            juce::String(targetTime, 2) + "s)");
    
    seek(targetTime);
}

void AudioEngine::skipToPreviousBeat()
{
    TransportState state = getTransportState();
    
    // Calculate beat duration in seconds
    double beatDuration = 60.0 / state.bpm;
    
    // Calculate current beat position (fractional)
    double currentBeat = state.currentTime / beatDuration;
    
    // Round DOWN to previous whole beat
    int prevBeat = (int)std::floor(currentBeat);
    
    // If we're already exactly on a beat, skip to the previous one
    if (std::abs(currentBeat - prevBeat) < 0.001)
        prevBeat--;
    
    // Don't go below beat 0
    if (prevBeat < 0)
        prevBeat = 0;
    
    // Convert back to time
    double targetTime = prevBeat * beatDuration;
    
    // Calculate bar.beat.tick for display
    int beatsPerBar = (currentSession && currentSession->getTimeSignatureNumerator() > 0) 
                      ? currentSession->getTimeSignatureNumerator() : 4;
    int bar = (prevBeat / beatsPerBar) + 1;
    int beat = (prevBeat % beatsPerBar) + 1;
    
    juce::Logger::writeToLog("Skip to previous beat: " + juce::String(bar) + "." + juce::String(beat) + ".00 (" + 
                            juce::String(targetTime, 2) + "s)");
    
    seek(targetTime);
}

void AudioEngine::skipToNextBar()
{
    TransportState state = getTransportState();
    
    // Get beats per bar from session
    int beatsPerBar = (currentSession && currentSession->getTimeSignatureNumerator() > 0) 
                      ? currentSession->getTimeSignatureNumerator() : 4;
    
    // Calculate beat and bar durations
    double beatDuration = 60.0 / state.bpm;
    double barDuration = beatDuration * beatsPerBar;
    
    // Calculate current bar position (fractional)
    double currentBar = state.currentTime / barDuration;
    
    // Round UP to next whole bar
    int nextBar = (int)std::ceil(currentBar);
    
    // If we're already exactly on a bar, skip to the next one
    if (std::abs(currentBar - nextBar) < 0.001)
        nextBar++;
    
    // Convert back to time (bar starts at beat 1)
    double targetTime = nextBar * barDuration;
    
    juce::Logger::writeToLog("Skip to next bar: " + juce::String(nextBar + 1) + ".1.00 (" + 
                            juce::String(targetTime, 2) + "s)");
    
    seek(targetTime);
}

void AudioEngine::skipToPreviousBar()
{
    TransportState state = getTransportState();
    
    // Get beats per bar from session
    int beatsPerBar = (currentSession && currentSession->getTimeSignatureNumerator() > 0) 
                      ? currentSession->getTimeSignatureNumerator() : 4;
    
    // Calculate beat and bar durations
    double beatDuration = 60.0 / state.bpm;
    double barDuration = beatDuration * beatsPerBar;
    
    // Calculate current bar position (fractional)
    double currentBar = state.currentTime / barDuration;
    
    // Round DOWN to previous whole bar
    int prevBar = (int)std::floor(currentBar);
    
    // If we're already exactly on a bar, skip to the previous one
    if (std::abs(currentBar - prevBar) < 0.001)
        prevBar--;
    
    // Don't go below bar 0
    if (prevBar < 0)
        prevBar = 0;
    
    // Convert back to time
    double targetTime = prevBar * barDuration;
    
    juce::Logger::writeToLog("Skip to previous bar: " + juce::String(prevBar + 1) + ".1.00 (" + 
                            juce::String(targetTime, 2) + "s)");
    
    seek(targetTime);
}
