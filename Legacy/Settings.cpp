#include "Settings.h"

//==============================================================================
Settings::Settings()
{
    try
    {
        // Settings file location
        settingsFile = juce::File::getSpecialLocation(
            juce::File::userApplicationDataDirectory)
            .getChildFile("VTape24")
            .getChildFile("settings.xml");
        
        juce::Logger::writeToLog("Settings file location: " + settingsFile.getFullPathName());
        
        // Load existing settings or use defaults
        load();
        juce::Logger::writeToLog("Settings loaded successfully");
    }
    catch (const std::exception& e)
    {
        juce::Logger::writeToLog("Settings constructor error: " + juce::String(e.what()));
        // Use defaults
        resetToDefaults();
    }
}

Settings::~Settings()
{
    try
    {
        save();
    }
    catch (...)
    {
        // Don't throw in destructor
    }
}

AudioSettings Settings::getAudioSettings() const
{
    return currentSettings;
}

void Settings::setAudioSettings(const AudioSettings& settings)
{
    currentSettings = settings;
    save();
}

void Settings::setAudioDevice(const juce::String& deviceName)
{
    currentSettings.audioDeviceName = deviceName;
    save();
}

void Settings::setAudioDriverType(const juce::String& driverType)
{
    currentSettings.audioDriverType = driverType;
    save();
}

juce::String Settings::getAudioDriverType() const
{
    return currentSettings.audioDriverType;
}

void Settings::setTrackInputMapping(int trackIndex, int inputChannel)
{
    currentSettings.trackInputMapping[trackIndex] = inputChannel;
    save();
}

void Settings::setTrackOutputMapping(int trackIndex, int outputChannel, bool enabled)
{
    auto key = std::make_pair(trackIndex, outputChannel);
    if (enabled)
        currentSettings.trackOutputMapping[key] = true;
    else
        currentSettings.trackOutputMapping.erase(key);
    save();
}

void Settings::clearTrackOutputMapping()
{
    currentSettings.trackOutputMapping.clear();
    // Note: deliberately no save() here.
    // This is always called as part of a load sequence (clear → populate → save).
    // Saving mid-sequence would persist an empty routing map to disk, which could
    // be reloaded by an async audio-device re-init and wipe valid routes.
}

void Settings::clearTrackInputMapping()
{
    currentSettings.trackInputMapping.clear();
    // Same reasoning — no mid-sequence save.
}

void Settings::setRecordingPath(const juce::String& path)
{
    currentSettings.recordingPath = path;
    save();
}

void Settings::setProjectName(const juce::String& name)
{
    // Sanitize project name (remove invalid characters)
    juce::String sanitized = name;
    sanitized = sanitized.replaceCharacters("/\\:*?\"<>|", "_________");
    currentSettings.projectName = sanitized.isEmpty() ? "Untitled" : sanitized;
    save();
}

void Settings::setNumTracks(int numTracks)
{
    currentSettings.numTracks = juce::jlimit(1, 64, numTracks);
    save();
}

void Settings::setTrackCount(int count)
{
    currentSettings.trackCount = juce::jlimit(8, 64, count);
    save();
}

void Settings::setMasterChannelEnabled(bool enabled)
{
    currentSettings.masterChannelEnabled = enabled;
    save();
}

void Settings::setSampleRate(int rate)
{
    currentSettings.sampleRate = rate;
    save();
}

void Settings::setBufferSize(int size)
{
    currentSettings.bufferSize = size;
    save();
}

juce::String Settings::getAudioDevice() const
{
    return currentSettings.audioDeviceName;
}

int Settings::getTrackInputMapping(int trackIndex) const
{
    auto it = currentSettings.trackInputMapping.find(trackIndex);
    if (it != currentSettings.trackInputMapping.end())
        return it->second;
    return trackIndex; // Default: track N maps to input N
}

bool Settings::getTrackOutputMapping(int trackIndex, int outputChannel) const
{
    auto key = std::make_pair(trackIndex, outputChannel);
    auto it = currentSettings.trackOutputMapping.find(key);
    return it != currentSettings.trackOutputMapping.end() && it->second;
}

juce::String Settings::getRecordingPath() const
{
    return currentSettings.recordingPath;
}

juce::String Settings::getProjectName() const
{
    return currentSettings.projectName;
}

int Settings::getNumTracks() const
{
    return currentSettings.numTracks;
}

int Settings::getTrackCount() const
{
    return currentSettings.trackCount;
}

bool Settings::getMasterChannelEnabled() const
{
    return currentSettings.masterChannelEnabled;
}

int Settings::getSampleRate() const
{
    return currentSettings.sampleRate;
}

int Settings::getBufferSize() const
{
    return currentSettings.bufferSize;
}

void Settings::setOSCEnabled(bool enabled)
{
    currentSettings.oscEnabled = enabled;
}

void Settings::setOSCPort(int port)
{
    currentSettings.oscPort = port;
}

bool Settings::getOSCEnabled() const
{
    return currentSettings.oscEnabled;
}

int Settings::getOSCPort() const
{
    return currentSettings.oscPort;
}

void Settings::setDefaultSavePath(const juce::String& path)
{
    currentSettings.defaultSavePath = path;
    save();
}

juce::String Settings::getDefaultSavePath() const
{
    if (currentSettings.defaultSavePath.isNotEmpty())
        return currentSettings.defaultSavePath;
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
               .getChildFile("VTape24/Sessions").getFullPathName();
}

void Settings::setLTCInputChannel(int ch)
{
    currentSettings.ltcInputChannel = ch;
    save();
}

int Settings::getLTCInputChannel() const
{
    return currentSettings.ltcInputChannel;
}

void Settings::setLTCSlaveMode(bool slave)
{
    currentSettings.ltcSlaveMode = slave;
    save();
}

bool Settings::getLTCSlaveMode() const
{
    return currentSettings.ltcSlaveMode;
}

void Settings::setLTCOutputEnabled(bool enabled)
{
    currentSettings.ltcOutputEnabled = enabled;
    save();
}

bool Settings::getLTCOutputEnabled() const
{
    return currentSettings.ltcOutputEnabled;
}

void Settings::setLTCSendWhileStopped(bool send)
{
    currentSettings.ltcSendWhileStopped = send;
    save();
}

bool Settings::getLTCSendWhileStopped() const
{
    return currentSettings.ltcSendWhileStopped;
}

void Settings::setInputChannelName(int ch, const juce::String& name)
{
    if (name.isEmpty())
        currentSettings.inputChannelNames.erase(ch);
    else
        currentSettings.inputChannelNames[ch] = name;
    save();
}

juce::String Settings::getInputChannelName(int ch) const
{
    auto it = currentSettings.inputChannelNames.find(ch);
    if (it != currentSettings.inputChannelNames.end() && it->second.isNotEmpty())
        return it->second;
    return "IN " + juce::String(ch + 1);
}

void Settings::setOutputChannelName(int ch, const juce::String& name)
{
    if (name.isEmpty())
        currentSettings.outputChannelNames.erase(ch);
    else
        currentSettings.outputChannelNames[ch] = name;
    save();
}

juce::String Settings::getOutputChannelName(int ch) const
{
    auto it = currentSettings.outputChannelNames.find(ch);
    if (it != currentSettings.outputChannelNames.end() && it->second.isNotEmpty())
        return it->second;
    return "OUT " + juce::String(ch + 1);
}

void Settings::setDefaultBPM(double bpm)
{
    currentSettings.defaultBPM = bpm;
    save();
}

double Settings::getDefaultBPM() const
{
    return currentSettings.defaultBPM;
}

void Settings::setWaveformScrollDown(bool scrollDown)
{
    currentSettings.waveformScrollDown = scrollDown;
    save();
}

bool Settings::getWaveformScrollDown() const
{
    return currentSettings.waveformScrollDown;
}

void Settings::setWaveformZoom(double zoom)
{
    // Clamp zoom between 0.1x and 10x
    currentSettings.waveformZoom = juce::jlimit(0.1, 10.0, zoom);
    save();
}

double Settings::getWaveformZoom() const
{
    return currentSettings.waveformZoom;
}

void Settings::setAmplitudeZoom(double zoom)
{
    // Clamp amplitude zoom between 0.1x and 10x
    currentSettings.amplitudeZoom = juce::jlimit(0.1, 10.0, zoom);
    save();
}

double Settings::getAmplitudeZoom() const
{
    return currentSettings.amplitudeZoom;
}

void Settings::setGridEnabled(bool enabled)
{
    currentSettings.gridEnabled = enabled;
    save();
}

bool Settings::getGridEnabled() const
{
    return currentSettings.gridEnabled;
}

void Settings::setGridResolution(int resolution)
{
    // Clamp between 0 (bars) and 4 (1/32)
    currentSettings.gridResolution = juce::jlimit(0, 4, resolution);
    save();
}

int Settings::getGridResolution() const
{
    return currentSettings.gridResolution;
}

juce::File Settings::getProjectDirectory() const
{
    juce::File baseDir(currentSettings.recordingPath);
    if (!baseDir.exists() || currentSettings.recordingPath.isEmpty())
    {
        baseDir = juce::File::getSpecialLocation(
            juce::File::userDocumentsDirectory).getChildFile("VTape24/Sessions");
    }
    
    return baseDir.getChildFile(currentSettings.projectName);
}

void Settings::save()
{
    std::unique_ptr<juce::XmlElement> xml(new juce::XmlElement("VTape24Settings"));
    
    // Audio device
    xml->setAttribute("audioDevice", currentSettings.audioDeviceName);
    xml->setAttribute("audioDriverType", currentSettings.audioDriverType);
    
    // Recording path
    xml->setAttribute("recordingPath", currentSettings.recordingPath);
    
    // Project name
    xml->setAttribute("projectName", currentSettings.projectName);
    
    // Track count
    xml->setAttribute("numTracks", currentSettings.numTracks);
    
    // Master channel
    xml->setAttribute("masterChannel", currentSettings.masterChannelEnabled);
    
    // Sample rate
    xml->setAttribute("sampleRate", currentSettings.sampleRate);
    
    // Buffer size
    xml->setAttribute("bufferSize", currentSettings.bufferSize);
    xml->setAttribute("oscEnabled", currentSettings.oscEnabled);
    xml->setAttribute("oscPort", currentSettings.oscPort);
    xml->setAttribute("defaultSavePath", currentSettings.defaultSavePath);
    xml->setAttribute("ltcInputChannel", currentSettings.ltcInputChannel);
    xml->setAttribute("ltcSlaveMode", currentSettings.ltcSlaveMode);
    xml->setAttribute("ltcOutputEnabled", currentSettings.ltcOutputEnabled);
    xml->setAttribute("ltcSendWhileStopped", currentSettings.ltcSendWhileStopped);
    xml->setAttribute("defaultBPM", currentSettings.defaultBPM);
    xml->setAttribute("waveformScrollDown", currentSettings.waveformScrollDown);
    xml->setAttribute("waveformZoom", currentSettings.waveformZoom);
    xml->setAttribute("amplitudeZoom", currentSettings.amplitudeZoom);
    xml->setAttribute("gridEnabled", currentSettings.gridEnabled);
    xml->setAttribute("gridResolution", currentSettings.gridResolution);
    
    // Track count (user-selectable)
    xml->setAttribute("trackCount", currentSettings.trackCount);
    
    // Track input mappings (one per track)
    auto* mappingsXml = xml->createNewChildElement("TrackInputMappings");
    for (const auto& mapping : currentSettings.trackInputMapping)
    {
        auto* mappingXml = mappingsXml->createNewChildElement("Mapping");
        mappingXml->setAttribute("track", mapping.first);
        mappingXml->setAttribute("input", mapping.second);
    }
    
    // Track output mappings (multiple outputs per track)
    auto* outputMappingsXml = xml->createNewChildElement("TrackOutputMappings");
    for (const auto& mapping : currentSettings.trackOutputMapping)
    {
        if (mapping.second)  // Only save enabled outputs
        {
            auto* mappingXml = outputMappingsXml->createNewChildElement("Mapping");
            mappingXml->setAttribute("track", mapping.first.first);
            mappingXml->setAttribute("output", mapping.first.second);
        }
    }
    
    // Custom channel names
    auto* inNamesXml = xml->createNewChildElement("InputChannelNames");
    for (const auto& kv : currentSettings.inputChannelNames)
        inNamesXml->createNewChildElement("Ch")->setAttribute(juce::String(kv.first), kv.second);
    
    auto* outNamesXml = xml->createNewChildElement("OutputChannelNames");
    for (const auto& kv : currentSettings.outputChannelNames)
        outNamesXml->createNewChildElement("Ch")->setAttribute(juce::String(kv.first), kv.second);
    
    // Ensure directory exists
    settingsFile.getParentDirectory().createDirectory();
    
    // Save to file
    xml->writeTo(settingsFile);
}

void Settings::load()
{
    if (!settingsFile.existsAsFile())
    {
        resetToDefaults();
        return;
    }
    
    std::unique_ptr<juce::XmlElement> xml(juce::XmlDocument::parse(settingsFile));
    
    if (xml == nullptr || !xml->hasTagName("VTape24Settings"))
    {
        resetToDefaults();
        return;
    }
    
    // Load audio device
    currentSettings.audioDeviceName = xml->getStringAttribute("audioDevice", "");
    currentSettings.audioDriverType = xml->getStringAttribute("audioDriverType", "");
    
    // Load recording path
    currentSettings.recordingPath = xml->getStringAttribute("recordingPath", 
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile("VTape24/Sessions").getFullPathName());
    
    // Load project name
    currentSettings.projectName = xml->getStringAttribute("projectName", "Untitled");
    
    // Load track count
    currentSettings.numTracks = xml->getIntAttribute("numTracks", 24);
    
    // Load master channel
    currentSettings.masterChannelEnabled = xml->getBoolAttribute("masterChannel", false);
    
    // Load sample rate
    currentSettings.sampleRate = xml->getIntAttribute("sampleRate", 48000);
    
    // Load buffer size
    currentSettings.bufferSize = xml->getIntAttribute("bufferSize", 512);
    currentSettings.oscEnabled = xml->getBoolAttribute("oscEnabled", true);
    currentSettings.oscPort = xml->getIntAttribute("oscPort", 8000);
    currentSettings.defaultSavePath = xml->getStringAttribute("defaultSavePath", "");
    currentSettings.ltcInputChannel = xml->getIntAttribute("ltcInputChannel", -1);
    currentSettings.ltcSlaveMode     = xml->getBoolAttribute("ltcSlaveMode", false);
    currentSettings.ltcOutputEnabled = xml->getBoolAttribute("ltcOutputEnabled", false);
    currentSettings.ltcSendWhileStopped = xml->getBoolAttribute("ltcSendWhileStopped", false);
    currentSettings.defaultBPM = xml->getDoubleAttribute("defaultBPM", 120.0);
    currentSettings.waveformScrollDown = xml->getBoolAttribute("waveformScrollDown", true);
    currentSettings.waveformZoom = xml->getDoubleAttribute("waveformZoom", 1.0);
    currentSettings.amplitudeZoom = xml->getDoubleAttribute("amplitudeZoom", 1.0);
    currentSettings.gridEnabled = xml->getBoolAttribute("gridEnabled", false);
    currentSettings.gridResolution = xml->getIntAttribute("gridResolution", 1);
    
    // Load track count (user-selectable)
    currentSettings.trackCount = xml->getIntAttribute("trackCount", 8);
    
    // Load custom channel names
    currentSettings.inputChannelNames.clear();
    if (auto* inNamesXml = xml->getChildByName("InputChannelNames"))
        for (auto* ch : inNamesXml->getChildIterator())
            for (int attr = 0; attr < ch->getNumAttributes(); ++attr)
            {
                int idx = ch->getAttributeName(attr).getIntValue();
                juce::String name = ch->getAttributeValue(attr);
                if (name.isNotEmpty()) currentSettings.inputChannelNames[idx] = name;
            }
    
    currentSettings.outputChannelNames.clear();
    if (auto* outNamesXml = xml->getChildByName("OutputChannelNames"))
        for (auto* ch : outNamesXml->getChildIterator())
            for (int attr = 0; attr < ch->getNumAttributes(); ++attr)
            {
                int idx = ch->getAttributeName(attr).getIntValue();
                juce::String name = ch->getAttributeValue(attr);
                if (name.isNotEmpty()) currentSettings.outputChannelNames[idx] = name;
            }
    
    // Load track input mappings (one per track)
    currentSettings.trackInputMapping.clear();
    auto* mappingsXml = xml->getChildByName("TrackInputMappings");
    if (mappingsXml)
    {
        for (auto* mappingXml : mappingsXml->getChildIterator())
        {
            if (mappingXml->hasTagName("Mapping"))
            {
                int track = mappingXml->getIntAttribute("track", -1);
                int input = mappingXml->getIntAttribute("input", -1);
                if (track >= 0 && input >= 0)
                {
                    currentSettings.trackInputMapping[track] = input;
                }
            }
        }
    }
    
    // Load track output mappings (multiple outputs per track)
    currentSettings.trackOutputMapping.clear();
    auto* outputMappingsXml = xml->getChildByName("TrackOutputMappings");
    if (outputMappingsXml)
    {
        for (auto* mappingXml : outputMappingsXml->getChildIterator())
        {
            if (mappingXml->hasTagName("Mapping"))
            {
                int track = mappingXml->getIntAttribute("track", -1);
                int output = mappingXml->getIntAttribute("output", -1);
                if (track >= 0 && output >= 0)
                {
                    auto key = std::make_pair(track, output);
                    currentSettings.trackOutputMapping[key] = true;
                }
            }
        }
    }
    
    ensureDefaultMappings();
}

void Settings::resetToDefaults()
{
    currentSettings.audioDeviceName = "";
    currentSettings.trackInputMapping.clear();
    currentSettings.recordingPath = juce::File::getSpecialLocation(
        juce::File::userDocumentsDirectory).getChildFile("VTape24/Sessions").getFullPathName();
    currentSettings.projectName = "Untitled";
    currentSettings.trackCount = 8;  // Use trackCount, not numTracks!
    currentSettings.masterChannelEnabled = false;
    currentSettings.waveformScrollDown = true;
    currentSettings.waveformZoom = 1.0;
    currentSettings.amplitudeZoom = 1.0;
    currentSettings.defaultBPM = 120.0;
    currentSettings.gridEnabled = false;
    currentSettings.gridResolution = 1;  // Default to beats
    
    ensureDefaultMappings();
    save();
}

void Settings::ensureDefaultMappings()
{
    // Ensure all tracks have a default mapping
    for (int i = 0; i < currentSettings.numTracks; ++i)
    {
        if (currentSettings.trackInputMapping.find(i) == currentSettings.trackInputMapping.end())
        {
            currentSettings.trackInputMapping[i] = i; // Track N -> Input N
        }
    }
}

//==============================================================================
// Session Management
//==============================================================================

void Settings::setSessionPath(const juce::String& path)
{
    currentSettings.sessionPath = path;
    save();
}

void Settings::setSessionName(const juce::String& name)
{
    currentSettings.sessionName = name.replaceCharacters("/\\:*?\"<>|", "_________");
    save();
}

void Settings::setSessionNotes(const juce::String& notes)
{
    currentSettings.sessionNotes = notes;
    save();
}

juce::String Settings::getSessionPath() const
{
    if (currentSettings.sessionPath.isEmpty())
    {
        return juce::File::getSpecialLocation(
            juce::File::userDocumentsDirectory).getChildFile("VTape24/Sessions").getFullPathName();
    }
    return currentSettings.sessionPath;
}

juce::String Settings::getSessionName() const
{
    return currentSettings.sessionName;
}

juce::String Settings::getSessionNotes() const
{
    return currentSettings.sessionNotes;
}

juce::File Settings::getCurrentSessionDirectory() const
{
    juce::File sessionsDir(getSessionPath());
    return sessionsDir.getChildFile(currentSettings.sessionName);
}

juce::File Settings::getSessionRecordingsDirectory() const
{
    return getCurrentSessionDirectory().getChildFile("Recordings");
}

juce::File Settings::getSessionFile() const
{
    return getCurrentSessionDirectory().getChildFile("session.vtape");
}

bool Settings::saveSession()
{
    juce::File sessionDir = getCurrentSessionDirectory();
    sessionDir.createDirectory();
    
    juce::File recordingsDir = getSessionRecordingsDirectory();
    recordingsDir.createDirectory();
    
    juce::File sessionFile = getSessionFile();
    
    // Create XML for session
    juce::XmlElement session("VTape24Session");
    session.setAttribute("version", "1.0");
    session.setAttribute("name", currentSettings.sessionName);
    session.setAttribute("created", juce::Time::getCurrentTime().toString(true, true));
    
    // Session info
    auto* info = session.createNewChildElement("SessionInfo");
    info->setAttribute("notes", currentSettings.sessionNotes);
    info->setAttribute("sampleRate", currentSettings.sampleRate);
    info->setAttribute("bufferSize", currentSettings.bufferSize);
    info->setAttribute("trackCount", currentSettings.trackCount);
    
    // Audio device
    auto* audio = session.createNewChildElement("AudioDevice");
    audio->setAttribute("name", currentSettings.audioDeviceName);
    
    // Track input routing
    auto* inputRouting = session.createNewChildElement("InputRouting");
    for (const auto& mapping : currentSettings.trackInputMapping)
    {
        auto* track = inputRouting->createNewChildElement("Track");
        track->setAttribute("index", mapping.first);
        track->setAttribute("input", mapping.second);
    }
    
    // Track output routing
    auto* outputRouting = session.createNewChildElement("OutputRouting");
    for (const auto& mapping : currentSettings.trackOutputMapping)
    {
        if (mapping.second)  // Only save enabled outputs
        {
            auto* route = outputRouting->createNewChildElement("Route");
            route->setAttribute("track", mapping.first.first);
            route->setAttribute("output", mapping.first.second);
        }
    }
    
    // Recordings list
    auto* recordings = session.createNewChildElement("Recordings");
    juce::Array<juce::File> wavFiles;
    recordingsDir.findChildFiles(wavFiles, juce::File::findFiles, false, "*.wav");
    
    for (const auto& wavFile : wavFiles)
    {
        auto* recording = recordings->createNewChildElement("Recording");
        recording->setAttribute("filename", wavFile.getFileName());
        recording->setAttribute("path", wavFile.getRelativePathFrom(sessionDir));
    }
    
    // Save to file atomically (write to temp file first, then rename)
    juce::File tempFile = sessionFile.getSiblingFile(sessionFile.getFileName() + ".tmp");
    
    // Delete any existing temp file
    if (tempFile.existsAsFile())
        tempFile.deleteFile();
    
    // Write to temp file
    if (!session.writeTo(tempFile))
    {
        juce::Logger::writeToLog("Failed to write temp session file: " + tempFile.getFullPathName());
        tempFile.deleteFile();  // Clean up failed temp file
        return false;
    }
    
    // Verify temp file was written correctly
    if (!tempFile.existsAsFile() || tempFile.getSize() == 0)
    {
        juce::Logger::writeToLog("Temp session file is empty or missing: " + tempFile.getFullPathName());
        tempFile.deleteFile();
        return false;
    }
    
    // Delete old session file if it exists
    if (sessionFile.existsAsFile())
    {
        if (!sessionFile.deleteFile())
        {
            juce::Logger::writeToLog("Failed to delete old session file: " + sessionFile.getFullPathName());
            tempFile.deleteFile();
            return false;
        }
    }
    
    // Rename temp file to actual session file (atomic operation)
    if (!tempFile.moveFileTo(sessionFile))
    {
        juce::Logger::writeToLog("Failed to rename temp session file to: " + sessionFile.getFullPathName());
        return false;
    }
    
    // Verify final file
    if (!sessionFile.existsAsFile() || sessionFile.getSize() == 0)
    {
        juce::Logger::writeToLog("Final session file is empty or missing: " + sessionFile.getFullPathName());
        return false;
    }
    
    juce::Logger::writeToLog("Session saved successfully: " + sessionFile.getFullPathName() + 
                             " (size: " + juce::String(sessionFile.getSize()) + " bytes)");
    return true;
}

bool Settings::loadSession(const juce::File& sessionFile)
{
    if (!sessionFile.existsAsFile())
    {
        juce::Logger::writeToLog("Session file not found: " + sessionFile.getFullPathName());
        return false;
    }
    
    // Check file size to avoid parsing empty/corrupt files
    if (sessionFile.getSize() == 0)
    {
        juce::Logger::writeToLog("Session file is empty: " + sessionFile.getFullPathName());
        return false;
    }
    
    // Parse XML with error reporting
    juce::String parseError;
    std::unique_ptr<juce::XmlElement> session(juce::XmlDocument::parse(sessionFile));
    
    if (!session)
    {
        juce::Logger::writeToLog("Failed to parse session file: " + sessionFile.getFullPathName());
        juce::Logger::writeToLog("XML parse error: " + parseError);
        return false;
    }
    
    if (session->getTagName() != "VTape24Session")
    {
        juce::Logger::writeToLog("Invalid session file (wrong root tag): " + sessionFile.getFullPathName());
        return false;
    }
    
    // Validate version (for future compatibility)
    juce::String version = session->getStringAttribute("version", "unknown");
    juce::Logger::writeToLog("Loading session version: " + version);
    
    // Load session info
    currentSettings.sessionName = session->getStringAttribute("name", "Untitled Session");
    
    if (auto* info = session->getChildByName("SessionInfo"))
    {
        currentSettings.sessionNotes = info->getStringAttribute("notes", "");
        
        // Validate sample rate
        int loadedSampleRate = info->getIntAttribute("sampleRate", 48000);
        if (loadedSampleRate >= 44100 && loadedSampleRate <= 192000)
            currentSettings.sampleRate = loadedSampleRate;
        else
        {
            juce::Logger::writeToLog("Warning: Invalid sample rate " + juce::String(loadedSampleRate) + 
                                     ", using default 48000");
            currentSettings.sampleRate = 48000;
        }
        
        // Validate buffer size
        int loadedBufferSize = info->getIntAttribute("bufferSize", 512);
        if (loadedBufferSize >= 64 && loadedBufferSize <= 4096)
            currentSettings.bufferSize = loadedBufferSize;
        else
        {
            juce::Logger::writeToLog("Warning: Invalid buffer size " + juce::String(loadedBufferSize) + 
                                     ", using default 512");
            currentSettings.bufferSize = 512;
        }
        
        // Validate track count
        int loadedTrackCount = info->getIntAttribute("trackCount", 8);
        if (loadedTrackCount >= 1 && loadedTrackCount <= 32)
            currentSettings.trackCount = loadedTrackCount;
        else
        {
            juce::Logger::writeToLog("Warning: Invalid track count " + juce::String(loadedTrackCount) + 
                                     ", using default 8");
            currentSettings.trackCount = 8;
        }
    }
    
    // Load audio device
    if (auto* audio = session->getChildByName("AudioDevice"))
    {
        currentSettings.audioDeviceName = audio->getStringAttribute("name", "");
    }
    
    // Load input routing
    currentSettings.trackInputMapping.clear();
    if (auto* inputRouting = session->getChildByName("InputRouting"))
    {
        for (auto* track : inputRouting->getChildIterator())
        {
            int trackIndex = track->getIntAttribute("index", -1);
            int inputChannel = track->getIntAttribute("input", -1);
            
            if (trackIndex >= 0)
            {
                currentSettings.trackInputMapping[trackIndex] = inputChannel;
            }
        }
    }
    
    // Load output routing
    currentSettings.trackOutputMapping.clear();
    if (auto* outputRouting = session->getChildByName("OutputRouting"))
    {
        for (auto* route : outputRouting->getChildIterator())
        {
            int trackIndex = route->getIntAttribute("track", -1);
            int outputChannel = route->getIntAttribute("output", -1);
            
            if (trackIndex >= 0 && outputChannel >= 0)
            {
                auto key = std::make_pair(trackIndex, outputChannel);
                currentSettings.trackOutputMapping[key] = true;
            }
        }
    }
    
    // Set session directory
    currentSettings.sessionPath = sessionFile.getParentDirectory().getParentDirectory().getFullPathName();
    
    save();
    
    juce::Logger::writeToLog("=== Session loaded successfully ===");
    juce::Logger::writeToLog("File: " + sessionFile.getFullPathName());
    juce::Logger::writeToLog("Name: " + currentSettings.sessionName);
    juce::Logger::writeToLog("Sample Rate: " + juce::String(currentSettings.sampleRate));
    juce::Logger::writeToLog("Buffer Size: " + juce::String(currentSettings.bufferSize));
    juce::Logger::writeToLog("Track Count: " + juce::String(currentSettings.trackCount));
    juce::Logger::writeToLog("Input Mappings: " + juce::String(currentSettings.trackInputMapping.size()));
    juce::Logger::writeToLog("Output Mappings: " + juce::String(currentSettings.trackOutputMapping.size()));
    juce::Logger::writeToLog("===================================");
    
    return true;
}

juce::Array<juce::File> Settings::getAvailableSessions() const
{
    juce::Array<juce::File> sessions;
    juce::File sessionsDir(getSessionPath());
    
    if (sessionsDir.exists())
    {
        juce::Array<juce::File> subdirs;
        sessionsDir.findChildFiles(subdirs, juce::File::findDirectories, false);
        
        for (const auto& dir : subdirs)
        {
            juce::File sessionFile = dir.getChildFile("session.vtape");
            if (sessionFile.existsAsFile())
            {
                sessions.add(sessionFile);
            }
        }
    }
    
    return sessions;
}

