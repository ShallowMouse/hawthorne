#pragma once

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

//==============================================================================
// Session - Represents a recording session with all its settings
//==============================================================================
class Session
{
public:
    Session()
    {
        // Default settings
        sessionName = "Untitled Session";
        sampleRate = 48000;
        bitDepth = 24;
        trackCount = 16;
        bufferSize = 512;
        createdDate = juce::Time::getCurrentTime();
        lastModified = createdDate;
    }
    
    // Load from XML file
    bool loadFromFile(const juce::File& sessionFile)
    {
        if (!sessionFile.existsAsFile())
            return false;
        
        juce::XmlDocument doc(sessionFile);
        std::unique_ptr<juce::XmlElement> xml(doc.getDocumentElement());
        
        if (xml == nullptr || xml->getTagName() != "VTape24Session")
            return false;
        
        // Load basic info
        sessionName = xml->getStringAttribute("name", "Untitled Session");
        notes = xml->getStringAttribute("notes", "");
        
        // Parse dates from ISO8601 strings
        juce::String createdStr = xml->getStringAttribute("created", "");
        if (createdStr.isNotEmpty())
            createdDate = juce::Time::fromISO8601(createdStr);
        
        juce::String modifiedStr = xml->getStringAttribute("modified", "");
        if (modifiedStr.isNotEmpty())
            lastModified = juce::Time::fromISO8601(modifiedStr);
        
        // Load audio settings
        if (auto* audioSettings = xml->getChildByName("AudioSettings"))
        {
            sampleRate = audioSettings->getIntAttribute("sampleRate", 48000);
            bitDepth = audioSettings->getIntAttribute("bitDepth", 24);
            bufferSize = audioSettings->getIntAttribute("bufferSize", 512);
            trackCount = audioSettings->getIntAttribute("trackCount", 16);
            hasStereoReturn = audioSettings->getBoolAttribute("hasStereoReturn", false);
            audioDevice = audioSettings->getStringAttribute("audioDevice", "");
            bpm = audioSettings->getDoubleAttribute("bpm", 120.0);
            timeSignatureNumerator = audioSettings->getIntAttribute("timeSignatureNumerator", 4);
            timeSignatureDenominator = audioSettings->getIntAttribute("timeSignatureDenominator", 4);
            ltcOutputEnabled = audioSettings->getBoolAttribute("ltcOutputEnabled", false);
            
            // Load input routing
            if (auto* inputRoutingElement = audioSettings->getChildByName("InputRouting"))
            {
                inputRouting.clear();
                for (auto* routeElement : inputRoutingElement->getChildIterator())
                {
                    int track = routeElement->getIntAttribute("track", -1);
                    int input = routeElement->getIntAttribute("input", -1);
                    if (track >= 0 && input >= 0)
                        inputRouting[track] = input;
                }
            }
            
            // Load output routing.
            // Supports both multi-output format (track + output per Route, all enabled)
            // and the legacy single-output format (identical XML, treated as enabled=true).
            if (auto* outputRoutingElement = audioSettings->getChildByName("OutputRouting"))
            {
                outputRouting.clear();
                for (auto* routeElement : outputRoutingElement->getChildIterator())
                {
                    int track  = routeElement->getIntAttribute("track",  -1);
                    int output = routeElement->getIntAttribute("output", -1);
                    if (track >= 0 && output >= 0)
                        outputRouting[{track, output}] = true;
                }
            }
        }
        
        // Load track info
        if (auto* tracksElement = xml->getChildByName("Tracks"))
        {
            for (auto* trackElement : tracksElement->getChildIterator())
            {
                TrackInfo track;
                track.number = trackElement->getIntAttribute("number", 0);
                track.name = trackElement->getStringAttribute("name", "");
                track.armed = trackElement->getBoolAttribute("armed", false);
                track.muted = trackElement->getBoolAttribute("muted", false);
                track.volume = (float)trackElement->getDoubleAttribute("volume", 0.8);
                track.pan = (float)trackElement->getDoubleAttribute("pan", 0.5);
                track.recordingFile = trackElement->getStringAttribute("recordingFile", "");
                track.isStereoReturn = trackElement->getBoolAttribute("isStereoReturn", false);
                
                tracks.push_back(track);
            }
        }
        
        // Load transport state
        if (auto* transport = xml->getChildByName("Transport"))
        {
            playbackPosition = transport->getDoubleAttribute("position", 0.0);
        }
        
        // Load markers
        markers.clear();
        if (auto* markersElement = xml->getChildByName("Markers"))
        {
            for (auto* markerElement : markersElement->getChildIterator())
            {
                if (markerElement->hasTagName("Marker"))
                {
                    MarkerInfo marker;
                    marker.timestamp = markerElement->getDoubleAttribute("timestamp", 0.0);
                    marker.label = markerElement->getStringAttribute("label", "");
                    marker.colour = markerElement->getStringAttribute("colour", "");
                    markers.push_back(marker);
                }
            }
            juce::Logger::writeToLog("  Loaded " + juce::String(markers.size()) + " markers");
        }
        
        // Derive folder paths from the session file location
        sessionFilePath = sessionFile;
        sessionFolder = sessionFile.getParentDirectory();
        audioFolder = sessionFolder.getChildFile("Audio");
        
        juce::Logger::writeToLog("  Session folder: " + sessionFolder.getFullPathName());
        juce::Logger::writeToLog("  Audio folder: " + audioFolder.getFullPathName());
        juce::Logger::writeToLog("  Audio folder exists: " + juce::String(audioFolder.exists() ? "YES" : "NO"));
        
        return true;
    }
    
    // Save to XML file
    bool saveToFile(const juce::File& sessionFile)
    {
        juce::Logger::writeToLog("Saving session to: " + sessionFile.getFullPathName());
        
        juce::XmlElement root("VTape24Session");
        
        // Save basic info
        lastModified = juce::Time::getCurrentTime();
        root.setAttribute("name", sessionName);
        root.setAttribute("notes", notes);
        root.setAttribute("created", createdDate.toISO8601(true));
        root.setAttribute("modified", lastModified.toISO8601(true));
        root.setAttribute("version", "0.1.0");
        
        juce::Logger::writeToLog("  Session name: " + sessionName);
        juce::Logger::writeToLog("  Track count: " + juce::String(trackCount));
        
        // Save audio settings
        auto* audioSettings = root.createNewChildElement("AudioSettings");
        audioSettings->setAttribute("sampleRate", sampleRate);
        audioSettings->setAttribute("bitDepth", bitDepth);
        audioSettings->setAttribute("bufferSize", bufferSize);
        audioSettings->setAttribute("trackCount", trackCount);
        audioSettings->setAttribute("hasStereoReturn", hasStereoReturn);
        audioSettings->setAttribute("audioDevice", audioDevice);
        audioSettings->setAttribute("bpm", bpm);
        audioSettings->setAttribute("timeSignatureNumerator", timeSignatureNumerator);
        audioSettings->setAttribute("timeSignatureDenominator", timeSignatureDenominator);
        audioSettings->setAttribute("ltcOutputEnabled", ltcOutputEnabled);
        
        // Save input routing
        auto* inputRoutingElement = audioSettings->createNewChildElement("InputRouting");
        for (const auto& [track, input] : inputRouting)
        {
            auto* routeElement = inputRoutingElement->createNewChildElement("Route");
            routeElement->setAttribute("track", track);
            routeElement->setAttribute("input", input);
        }
        
        // Save output routing — one <Route track="T" output="O"/> per enabled (track,output) pair
        auto* outputRoutingElement = audioSettings->createNewChildElement("OutputRouting");
        for (const auto& [key, enabled] : outputRouting)
        {
            if (!enabled) continue;
            auto* routeElement = outputRoutingElement->createNewChildElement("Route");
            routeElement->setAttribute("track",  key.first);
            routeElement->setAttribute("output", key.second);
        }
        
        // Save track info
        auto* tracksElement = root.createNewChildElement("Tracks");
        for (const auto& track : tracks)
        {
            auto* trackElement = tracksElement->createNewChildElement("Track");
            trackElement->setAttribute("number", track.number);
            trackElement->setAttribute("name", track.name);
            trackElement->setAttribute("armed", track.armed);
            trackElement->setAttribute("muted", track.muted);
            trackElement->setAttribute("volume", track.volume);
            trackElement->setAttribute("pan", track.pan);
            trackElement->setAttribute("recordingFile", track.recordingFile);
            trackElement->setAttribute("isStereoReturn", track.isStereoReturn);
        }
        
        // Save transport state
        auto* transport = root.createNewChildElement("Transport");
        transport->setAttribute("position", playbackPosition);
        
        // Save markers
        auto* markersElement = root.createNewChildElement("Markers");
        for (const auto& marker : markers)
        {
            auto* markerElement = markersElement->createNewChildElement("Marker");
            markerElement->setAttribute("timestamp", marker.timestamp);
            markerElement->setAttribute("label", marker.label);
            if (marker.colour.isNotEmpty())
                markerElement->setAttribute("colour", marker.colour);
        }
        
        juce::Logger::writeToLog("  Saved " + juce::String(markers.size()) + " markers");
        
        // Write to file
        if (root.writeTo(sessionFile))
        {
            sessionFilePath = sessionFile;
            juce::Logger::writeToLog("Session saved successfully!");
            return true;
        }
        
        juce::Logger::writeToLog("ERROR: Failed to write session file!");
        return false;
    }
    
    // Create session folder structure
    bool createSessionStructure(const juce::File& projectFolder, const juce::String& name)
    {
        sessionName = name;
        
        juce::Logger::writeToLog("Creating session structure for: " + name);
        juce::Logger::writeToLog("  Project folder: " + projectFolder.getFullPathName());
        
        // Create session folder: ProjectFolder/SessionName/
        sessionFolder = projectFolder.getChildFile(name);
        juce::Logger::writeToLog("  Session folder: " + sessionFolder.getFullPathName());
        
        if (!sessionFolder.createDirectory())
        {
            juce::Logger::writeToLog("  ERROR: Failed to create session folder!");
            return false;
        }
        juce::Logger::writeToLog("  Session folder created");
        
        // Create Audio subfolder
        audioFolder = sessionFolder.getChildFile("Audio");
        juce::Logger::writeToLog("  Audio folder: " + audioFolder.getFullPathName());
        
        if (!audioFolder.createDirectory())
        {
            juce::Logger::writeToLog("  ERROR: Failed to create Audio folder!");
            return false;
        }
        juce::Logger::writeToLog("  Audio folder created");
        
        // Create session.xml file
        sessionFilePath = sessionFolder.getChildFile("session.xml");
        juce::Logger::writeToLog("  Session file: " + sessionFilePath.getFullPathName());
        
        // Initialize track info
        tracks.clear();
        for (int i = 0; i < trackCount; ++i)
        {
            TrackInfo track;
            track.number = i + 1;
            track.name = "Track " + juce::String(i + 1);
            track.armed = false;
            track.muted = false;
            track.volume = 0.8f;
            track.pan = 0.5f;
            track.isStereoReturn = false;
            tracks.push_back(track);
        }
        
        // Add stereo return track if enabled
        if (hasStereoReturn)
        {
            TrackInfo stereoReturn;
            stereoReturn.number = trackCount + 1;  // Last track
            stereoReturn.name = "Stereo Return";
            stereoReturn.armed = false;
            stereoReturn.muted = false;
            stereoReturn.volume = 0.8f;
            stereoReturn.pan = 0.5f;
            stereoReturn.isStereoReturn = true;
            tracks.push_back(stereoReturn);
            
            juce::Logger::writeToLog("  Added stereo return track: " + juce::String(stereoReturn.number));
        }
        
        return saveToFile(sessionFilePath);
    }
    
    // Get audio file path for a track
    juce::File getAudioFileForTrack(int trackNumber) const
    {
        return audioFolder.getChildFile("Track" + juce::String(trackNumber) + ".wav");
    }
    
    // Getters
    juce::String getSessionName() const { return sessionName; }
    juce::String getNotes() const { return notes; }
    int getSampleRate() const { return sampleRate; }
    int getBitDepth() const { return bitDepth; }
    int getBufferSize() const { return bufferSize; }
    int getTrackCount() const { return trackCount; }
    bool getHasStereoReturn() const { return hasStereoReturn; }
    bool getLTCOutputEnabled() const { return ltcOutputEnabled; }
    void setLTCOutputEnabled(bool v) { ltcOutputEnabled = v; }
    int getTotalTrackCount() const { return trackCount + (hasStereoReturn ? 1 : 0); }  // Total including stereo return
    double getBPM() const { return bpm; }
    int getTimeSignatureNumerator() const { return timeSignatureNumerator; }
    int getTimeSignatureDenominator() const { return timeSignatureDenominator; }
    juce::File getSessionFolder() const { return sessionFolder; }
    juce::File getAudioFolder() const { return audioFolder; }
    juce::File getSessionFilePath() const { return sessionFilePath; }
    juce::Time getCreatedDate() const { return createdDate; }
    juce::Time getLastModified() const { return lastModified; }
    double getPlaybackPosition() const { return playbackPosition; }
    juce::String getAudioDevice() const { return audioDevice; }
    const std::map<int, int>& getInputRouting() const { return inputRouting; }
    const std::map<std::pair<int,int>, bool>& getOutputRouting() const { return outputRouting; }
    
    // Setters
    void setSessionName(const juce::String& name) { sessionName = name; }
    void setNotes(const juce::String& n) { notes = n; }
    void setSampleRate(int rate) { sampleRate = rate; }
    void setBitDepth(int depth) { bitDepth = depth; }
    void setBufferSize(int size) { bufferSize = size; }
    void setTrackCount(int count) { trackCount = count; }
    void setHasStereoReturn(bool enabled) { hasStereoReturn = enabled; }
    void setBPM(double tempo) { bpm = juce::jlimit(20.0, 999.0, tempo); }  // Limit 20-999 BPM
    void setTimeSignature(int numerator, int denominator) 
    { 
        timeSignatureNumerator = numerator; 
        timeSignatureDenominator = denominator; 
    }
    void setPlaybackPosition(double pos) { playbackPosition = pos; }
    void setAudioDevice(const juce::String& device) { audioDevice = device; }
    void setInputRouting(const std::map<int, int>& routing) { inputRouting = routing; }
    void setOutputRouting(const std::map<std::pair<int,int>, bool>& routing) { outputRouting = routing; }
    
    // Track info structure
    struct TrackInfo
    {
        int number = 0;
        juce::String name;
        bool armed = false;
        bool muted = false;
        float volume = 0.8f;
        float pan = 0.5f;
        juce::String recordingFile;
        bool isStereoReturn = false;  // True if this is the stereo mixdown track
    };
    
    // Marker info structure
    struct MarkerInfo
    {
        double timestamp = 0.0;
        juce::String label;
        juce::String colour;
    };
    
    std::vector<TrackInfo>& getTracks() { return tracks; }
    const std::vector<TrackInfo>& getTracks() const { return tracks; }
    
    std::vector<MarkerInfo>& getMarkers() { return markers; }
    const std::vector<MarkerInfo>& getMarkers() const { return markers; }

private:
    // Session identification
    juce::String sessionName;
    juce::String notes;
    juce::Time createdDate;
    juce::Time lastModified;
    
    // Audio settings
    int sampleRate = 48000;
    int bitDepth = 24;
    int bufferSize = 512;
    int trackCount = 16;
    bool hasStereoReturn = false;  // Add stereo mixdown track
    bool ltcOutputEnabled = false; // LTC output generator enabled for this session
    
    // Tempo settings
    double bpm = 120.0;  // Beats per minute
    int timeSignatureNumerator = 4;  // Top number (beats per bar)
    int timeSignatureDenominator = 4;  // Bottom number (note value)
    
    juce::String audioDevice;  // Audio device name
    std::map<int, int> inputRouting;                    // Track number -> Input channel
    std::map<std::pair<int,int>, bool> outputRouting;  // (Track, Output) -> enabled
    
    // Track information
    std::vector<TrackInfo> tracks;
    
    // Marker information
    std::vector<MarkerInfo> markers;
    
    // Transport state
    double playbackPosition = 0.0;
    
    // File paths
    juce::File sessionFolder;
    juce::File audioFolder;
    juce::File sessionFilePath;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Session)
};
