#include "OSCController.h"
#include "AudioEngine.h"
#include "Track.h"
#include "Session.h"

//==============================================================================
OSCController::OSCController(AudioEngine* engine)
    : audioEngine(engine)
{
    sender = std::make_unique<juce::OSCSender>();
}

OSCController::~OSCController()
{
    disconnect();
}

bool OSCController::connect(int port)
{
    juce::Logger::writeToLog("========================================");
    juce::Logger::writeToLog("OSC: Attempting to connect...");
    juce::Logger::writeToLog("  Receive port: " + juce::String(port));
    juce::Logger::writeToLog("  Feedback port: " + juce::String(port + 1));
    juce::Logger::writeToLog("========================================");
    
    // Connect receiver
    if (!OSCReceiver::connect(port))
    {
        juce::Logger::writeToLog("OSC: FAILED to connect receiver on port " + juce::String(port));
        return false;
    }
    
    juce::Logger::writeToLog("OSC: ✓ Receiver connected on port " + juce::String(port));
    
    // Add listener
    addListener(this);
    juce::Logger::writeToLog("OSC: ✓ Listener registered (this = " + juce::String::toHexString((juce::pointer_sized_int)this) + ")");
    juce::Logger::writeToLog("OSC: Listener type: MessageLoopCallback");
    
    // Verify listener was added
    juce::Logger::writeToLog("OSC: Testing listener registration...");
    
    // Connect sender (for feedback)
    bool senderOk = sender->connect("127.0.0.1", port + 1);
    
    if (senderOk)
    {
        juce::Logger::writeToLog("OSC: ✓ Sender connected to 127.0.0.1:" + juce::String(port + 1));
    }
    else
    {
        juce::Logger::writeToLog("OSC: WARNING - Sender failed to connect (feedback won't work)");
    }
    
    juce::Logger::writeToLog("========================================");
    juce::Logger::writeToLog("OSC: READY - Listening for messages on port " + juce::String(port));
    juce::Logger::writeToLog("  Send commands TO:   127.0.0.1:" + juce::String(port));
    juce::Logger::writeToLog("  Receive feedback FROM: 127.0.0.1:" + juce::String(port + 1));
    juce::Logger::writeToLog("========================================");
    
    // Send a test message to ourselves to verify receive path works
    juce::Logger::writeToLog("OSC: Sending self-test /ping message...");
    juce::OSCSender testSender;
    if (testSender.connect("127.0.0.1", port))
    {
        testSender.send("/ping");
        juce::Logger::writeToLog("OSC: Self-test /ping sent - if you see '!!! OSC MESSAGE RECEIVED !!!' next, receiving works!");
    }
    else
    {
        juce::Logger::writeToLog("OSC: WARNING - Could not send self-test message");
    }
    juce::Logger::writeToLog("========================================");
    
    return true;
}

void OSCController::disconnect()
{
    removeListener(this);
    OSCReceiver::disconnect();
    sender->disconnect();
}

void OSCController::oscMessageReceived(const juce::OSCMessage& message)
{
    juce::String address = message.getAddressPattern().toString();
    
    // CRITICAL DEBUG: Log EVERY message received
    juce::Logger::writeToLog("========================================");
    juce::Logger::writeToLog("!!! OSC MESSAGE RECEIVED !!!");
    juce::Logger::writeToLog("========================================");
    juce::Logger::writeToLog("OSC Message Received:");
    juce::Logger::writeToLog("  Address: " + address);
    juce::Logger::writeToLog("  Arguments: " + juce::String(message.size()));
    for (int i = 0; i < message.size(); ++i)
    {
        if (message[i].isFloat32())
            juce::Logger::writeToLog("    [" + juce::String(i) + "] float: " + juce::String(message[i].getFloat32()));
        else if (message[i].isInt32())
            juce::Logger::writeToLog("    [" + juce::String(i) + "] int: " + juce::String(message[i].getInt32()));
        else if (message[i].isString())
            juce::Logger::writeToLog("    [" + juce::String(i) + "] string: " + message[i].getString());
    }
    juce::Logger::writeToLog("========================================");
    
    // Route to appropriate handler
    if (address.startsWith("/transport"))
    {
        juce::Logger::writeToLog("OSC: Routing to transport handler");
        handleTransportMessage(message);
    }
    else if (address.startsWith("/track"))
    {
        juce::Logger::writeToLog("OSC: Routing to track handler");
        handleTrackMessage(message);
    }
    else if (address.startsWith("/marker"))
    {
        juce::Logger::writeToLog("OSC: Routing to marker handler");
        handleMarkerMessage(message);
    }
    else if (address.startsWith("/click"))
    {
        juce::Logger::writeToLog("OSC: Routing to click handler");
        handleClickMessage(message);
    }
    else if (address == "/ping")
    {
        // Test command - respond to verify receiving works
        juce::Logger::writeToLog("OSC: PING received - sending PONG");
        sender->send("/pong", 1.0f);
    }
    else
    {
        juce::Logger::writeToLog("OSC: Unknown message type - not /transport, /track, or /marker");
    }
    
    juce::Logger::writeToLog("OSC: Message processing complete");
    juce::Logger::writeToLog("========================================");
}

void OSCController::handleTransportMessage(const juce::OSCMessage& message)
{
    juce::String address = message.getAddressPattern().toString();
    
    juce::Logger::writeToLog("OSC: Received transport message: " + address);
    
    if (address == "/transport/play")
    {
        audioEngine->play();
        sendAllTransportStates(true, false, false, false, false, false);
    }
    else if (address == "/transport/stop")
    {
        audioEngine->stop();
        sendAllTransportStates(false, true, false, false, false, false);
    }
    else if (address == "/transport/record")
    {
        audioEngine->record();
        sendAllTransportStates(false, false, true, false, false, false);
    }
    else if (address == "/transport/pause")
    {
        audioEngine->pause();
        // Send feedback with pause state - keep play lit during pause
        auto state = audioEngine->getTransportState();
        sendAllTransportStates(true, false, state.isRecording, false, false, true);  // play=true, pause=true
    }
    else if (address == "/transport/rewind")
    {
        // Check for button value - press starts rewind, release stops
        if (message.size() > 0)
        {
            float value = 0.0f;
            if (message[0].isFloat32())
                value = message[0].getFloat32();
            else if (message[0].isInt32())
                value = (float)message[0].getInt32();
            
            if (value > 0.5f)
            {
                // Button press - start rewinding
                juce::Logger::writeToLog("OSC: Rewind button pressed - starting rewind");
                audioEngine->rewind();
                sendAllTransportStates(false, false, false, true, false, false);
            }
            else
            {
                // Button release - stop rewinding
                juce::Logger::writeToLog("OSC: Rewind button released - stopping rewind");
                audioEngine->stopRewind();
                sendAllTransportStates(false, true, false, false, false, false);
            }
        }
        else
        {
            // No value - toggle behavior
            audioEngine->rewind();
            sendAllTransportStates(false, false, false, true, false, false);
        }
    }
    else if (address == "/transport/ffwd")
    {
        // Check for button value - press starts ffwd, release stops
        if (message.size() > 0)
        {
            float value = 0.0f;
            if (message[0].isFloat32())
                value = message[0].getFloat32();
            else if (message[0].isInt32())
                value = (float)message[0].getInt32();
            
            if (value > 0.5f)
            {
                // Button press - start fast forward
                juce::Logger::writeToLog("OSC: FFwd button pressed - starting fast forward");
                audioEngine->fastForward();
                sendAllTransportStates(false, false, false, false, true, false);
            }
            else
            {
                // Button release - stop fast forward
                juce::Logger::writeToLog("OSC: FFwd button released - stopping fast forward");
                audioEngine->stopFastForward();
                sendAllTransportStates(false, true, false, false, false, false);
            }
        }
        else
        {
            // No value - toggle behavior
            audioEngine->fastForward();
            sendAllTransportStates(false, false, false, false, true, false);
        }
    }
    else if (address == "/transport/rtz")
    {
        // Return to Zero - seek to beginning
        // Check for button value (ignore button release)
        bool shouldExecute = true;
        if (message.size() > 0)
        {
            if (message[0].isFloat32())
            {
                float value = message[0].getFloat32();
                if (value <= 0.5f)
                    shouldExecute = false;  // Ignore button release
            }
            else if (message[0].isInt32())
            {
                int value = message[0].getInt32();
                if (value == 0)
                    shouldExecute = false;
            }
        }
        
        if (shouldExecute)
        {
            juce::Logger::writeToLog("OSC: RTZ - Return to Zero");
            audioEngine->seek(0.0);
        }
        else
        {
            juce::Logger::writeToLog("OSC: RTZ - button release, ignoring");
        }
    }
    else if (address == "/transport/bpm/increase")
    {
        // BPM +amount (float argument)
        if (auto* session = audioEngine->getCurrentSession())
        {
            float amount = 1.0f;  // Default +1
            if (message.size() > 0 && message[0].isFloat32())
                amount = message[0].getFloat32();
            
            double currentBPM = session->getBPM();
            session->setBPM(currentBPM + amount);
            juce::Logger::writeToLog("OSC: BPM +" + juce::String(amount, 1) + " = " + juce::String(session->getBPM(), 1));
            sendBPM(session->getBPM());
        }
    }
    else if (address == "/transport/bpm/decrease")
    {
        // BPM -amount (float argument)
        if (auto* session = audioEngine->getCurrentSession())
        {
            float amount = 1.0f;  // Default -1
            if (message.size() > 0 && message[0].isFloat32())
                amount = message[0].getFloat32();
            
            double currentBPM = session->getBPM();
            session->setBPM(currentBPM - amount);
            juce::Logger::writeToLog("OSC: BPM -" + juce::String(amount, 1) + " = " + juce::String(session->getBPM(), 1));
            sendBPM(session->getBPM());
        }
    }
    // Varispeed controls
    else if (address == "/transport/speed/set")
    {
        // Set absolute speed (e.g., 1.0 = normal, 2.0 = double speed/octave up)
        if (audioEngine && message.size() > 0 && message[0].isFloat32())
        {
            float speed = message[0].getFloat32();
            audioEngine->setPlaybackSpeed(speed);
            juce::Logger::writeToLog("OSC: Speed set to " + juce::String(speed, 2) + "x");
        }
    }
    else if (address == "/transport/speed/semitone")
    {
        // Adjust speed by semitones (e.g., +1, -1, +12, -12)
        // Accept both float and int arguments
        if (audioEngine && message.size() > 0)
        {
            int semitones = 0;
            if (message[0].isFloat32())
                semitones = static_cast<int>(message[0].getFloat32());
            else if (message[0].isInt32())
                semitones = message[0].getInt32();
            
            if (semitones != 0)  // Only adjust if non-zero
            {
                audioEngine->adjustSpeedBySemitones(semitones);
                juce::Logger::writeToLog("OSC: Speed adjusted by " + juce::String(semitones) + " semitones");
            }
        }
    }
    else if (address == "/transport/speed/octave")
    {
        // Adjust speed by octaves (e.g., +1 = double speed, -1 = half speed)
        // Accept both float and int arguments
        if (audioEngine && message.size() > 0)
        {
            int octaves = 0;
            if (message[0].isFloat32())
                octaves = static_cast<int>(message[0].getFloat32());
            else if (message[0].isInt32())
                octaves = message[0].getInt32();
            
            if (octaves != 0)  // Only adjust if non-zero
            {
                audioEngine->adjustSpeedByOctave(octaves);
                juce::Logger::writeToLog("OSC: Speed adjusted by " + juce::String(octaves) + " octaves");
            }
        }
    }
    else if (address == "/transport/speed/reset")
    {
        // Reset to normal speed (1.0x)
        if (audioEngine)
        {
            audioEngine->setPlaybackSpeed(1.0);
            juce::Logger::writeToLog("OSC: Speed reset to 1.0x");
        }
    }
    // Reverse playback toggle
    else if (address == "/transport/reverse")
    {
        if (audioEngine)
        {
            audioEngine->toggleReverse();
            juce::Logger::writeToLog("OSC: Reverse toggle");
        }
    }
    // Section/loop controls
    else if (address == "/transport/section/set_start")
    {
        // Set section start to current playhead position
        if (audioEngine)
        {
            double currentTime = audioEngine->getCurrentTime();
            audioEngine->setSectionStart(currentTime);
            juce::Logger::writeToLog("OSC: Section start set to " + juce::String(currentTime, 2) + "s");
        }
    }
    else if (address == "/transport/section/set_end")
    {
        // Set section end to current playhead position
        if (audioEngine)
        {
            double currentTime = audioEngine->getCurrentTime();
            audioEngine->setSectionEnd(currentTime);
            juce::Logger::writeToLog("OSC: Section end set to " + juce::String(currentTime, 2) + "s");
        }
    }
    else if (address == "/transport/section/clear")
    {
        if (audioEngine)
        {
            audioEngine->clearSection();
            juce::Logger::writeToLog("OSC: Section cleared");
        }
    }
    else if (address == "/transport/section/loop")
    {
        // Handle momentary button - only trigger on press
        bool shouldExecute = true;
        if (message.size() > 0)
        {
            if (message[0].isFloat32())
            {
                float value = message[0].getFloat32();
                if (value <= 0.5f)
                    shouldExecute = false;  // Ignore button release
            }
            else if (message[0].isInt32())
            {
                int value = message[0].getInt32();
                if (value == 0)
                    shouldExecute = false;
            }
        }
        
        if (shouldExecute && audioEngine)
        {
            audioEngine->toggleSectionLoop();
            juce::Logger::writeToLog("OSC: Section loop toggle");
        }
    }
    // Beat/Bar navigation
    else if (address == "/transport/skip/beat/next")
    {
        // Check for button value (ignore button release)
        bool shouldExecute = true;
        if (message.size() > 0)
        {
            if (message[0].isFloat32())
            {
                float value = message[0].getFloat32();
                if (value <= 0.5f)
                    shouldExecute = false;  // Ignore button release
            }
            else if (message[0].isInt32())
            {
                int value = message[0].getInt32();
                if (value == 0)
                    shouldExecute = false;
            }
        }
        
        if (shouldExecute && audioEngine)
        {
            juce::Logger::writeToLog("OSC: Skip to next beat");
            audioEngine->skipToNextBeat();
        }
    }
    else if (address == "/transport/skip/beat/prev")
    {
        // Check for button value (ignore button release)
        bool shouldExecute = true;
        if (message.size() > 0)
        {
            if (message[0].isFloat32())
            {
                float value = message[0].getFloat32();
                if (value <= 0.5f)
                    shouldExecute = false;
            }
            else if (message[0].isInt32())
            {
                int value = message[0].getInt32();
                if (value == 0)
                    shouldExecute = false;
            }
        }
        
        if (shouldExecute && audioEngine)
        {
            juce::Logger::writeToLog("OSC: Skip to previous beat");
            audioEngine->skipToPreviousBeat();
        }
    }
    else if (address == "/transport/skip/bar/next")
    {
        // Check for button value (ignore button release)
        bool shouldExecute = true;
        if (message.size() > 0)
        {
            if (message[0].isFloat32())
            {
                float value = message[0].getFloat32();
                if (value <= 0.5f)
                    shouldExecute = false;
            }
            else if (message[0].isInt32())
            {
                int value = message[0].getInt32();
                if (value == 0)
                    shouldExecute = false;
            }
        }
        
        if (shouldExecute && audioEngine)
        {
            juce::Logger::writeToLog("OSC: Skip to next bar");
            audioEngine->skipToNextBar();
        }
    }
    else if (address == "/transport/skip/bar/prev")
    {
        // Check for button value (ignore button release)
        bool shouldExecute = true;
        if (message.size() > 0)
        {
            if (message[0].isFloat32())
            {
                float value = message[0].getFloat32();
                if (value <= 0.5f)
                    shouldExecute = false;
            }
            else if (message[0].isInt32())
            {
                int value = message[0].getInt32();
                if (value == 0)
                    shouldExecute = false;
            }
        }
        
        if (shouldExecute && audioEngine)
        {
            juce::Logger::writeToLog("OSC: Skip to previous bar");
            audioEngine->skipToPreviousBar();
        }
    }
}

void OSCController::handleTrackMessage(const juce::OSCMessage& message)
{
    juce::String address = message.getAddressPattern().toString();
    
    juce::Logger::writeToLog("========================================");
    juce::Logger::writeToLog("OSC TRACK MESSAGE HANDLER");
    juce::Logger::writeToLog("  Raw address: '" + address + "'");
    juce::Logger::writeToLog("  Address length: " + juce::String(address.length()) + " characters");
    juce::Logger::writeToLog("  Message arguments: " + juce::String(message.size()));
    juce::Logger::writeToLog("========================================");
    
    if (!audioEngine)
    {
        juce::Logger::writeToLog("OSC: ERROR - audioEngine is NULL!");
        return;
    }
    
    juce::Logger::writeToLog("OSC: AudioEngine is available");
    
    // Parse track number from address
    // Format: /track/N/command
    // When split by "/", the leading / creates an empty first element
    juce::StringArray parts = juce::StringArray::fromTokens(address, "/", "");
    
    juce::Logger::writeToLog("OSC: Parsed " + juce::String(parts.size()) + " parts:");
    for (int i = 0; i < parts.size(); ++i)
    {
        juce::Logger::writeToLog("  Part[" + juce::String(i) + "] = '" + parts[i] + "'");
    }
    
    // Parts array: ['', 'track', '1', 'arm'] for /track/1/arm
    // So we check parts[1] == "track", not parts[0]
    if (parts.size() >= 4 && parts[1] == "track")
    {
        int trackNumber = parts[2].getIntValue();  // 1-based from OSC (user track 1 = OSC track 1)
        // User tracks start at engine index kVirtualTrackCount (2)
        int trackIndex = AudioEngine::kVirtualTrackCount + trackNumber - 1;
        juce::String command = parts[3];
        
        juce::Logger::writeToLog("========================================");
        juce::Logger::writeToLog("OSC: PARSED TRACK COMMAND");
        juce::Logger::writeToLog("  Track number (OSC): " + juce::String(trackNumber));
        juce::Logger::writeToLog("  Engine track index: " + juce::String(trackIndex));
        juce::Logger::writeToLog("  Command: '" + command + "'");
        juce::Logger::writeToLog("========================================");
        
        // Validate track index (must be a valid user track)
        int firstUserIdx = audioEngine->getFirstUserTrackIndex();
        int userCount    = audioEngine->getUserTrackCount();
        if (trackIndex < firstUserIdx || trackIndex >= firstUserIdx + userCount)
        {
            juce::Logger::writeToLog("OSC: ERROR - Invalid track number " + juce::String(trackNumber) + 
                                    " (valid: 1 to " + juce::String(userCount) + ")");
            return;
        }
        
        juce::Logger::writeToLog("OSC: Track index is valid");
        
        if (command == "arm")
        {
            bool armed;
            if (message.size() >= 1)
            {
                // Accept int or float (0 or 1)
                float value = message[0].isFloat32() ? message[0].getFloat32() : (float)message[0].getInt32();
                armed = value > 0.5f;
                
                juce::Logger::writeToLog("========================================");
                juce::Logger::writeToLog("OSC ARM COMMAND");
                juce::Logger::writeToLog("  Track: " + juce::String(trackNumber));
                juce::Logger::writeToLog("  Value received: " + juce::String(value));
                juce::Logger::writeToLog("  Setting armed state to: " + juce::String(armed ? "TRUE" : "FALSE"));
                juce::Logger::writeToLog("========================================");
            }
            else
            {
                // No argument - toggle current state
                Track* track = audioEngine->getTrack(trackIndex);
                armed = track ? !track->isArmed() : true;
                juce::Logger::writeToLog("OSC: ARM toggle (no value) -> " + juce::String(armed ? "TRUE" : "FALSE"));
            }
            
            juce::Logger::writeToLog("OSC: *** CALLING audioEngine->setTrackArmed(" + 
                                    juce::String(trackIndex) + ", " + 
                                    juce::String(armed ? "true" : "false") + ") ***");
            
            audioEngine->setTrackArmed(trackIndex, armed);
            
            juce::Logger::writeToLog("OSC: *** RETURNED from setTrackArmed ***");
            
            // Verify it was set
            Track* track = audioEngine->getTrack(trackIndex);
            if (track)
            {
                bool actualState = track->isArmed();
                juce::Logger::writeToLog("OSC: VERIFICATION - Track " + juce::String(trackNumber) + 
                                        " armed state is now: " + juce::String(actualState ? "TRUE" : "FALSE"));
                
                if (actualState != armed)
                {
                    juce::Logger::writeToLog("OSC: *** ERROR - State mismatch! Wanted " + 
                                            juce::String(armed ? "armed" : "disarmed") + 
                                            " but got " + juce::String(actualState ? "armed" : "disarmed") + " ***");
                }
            }
            
            // Send feedback (use trackNumber, not engine index)
            sender->send("/track/" + juce::String(trackNumber) + "/arm", armed ? 1.0f : 0.0f);
            juce::Logger::writeToLog("OSC: Sent feedback message /track/" + juce::String(trackNumber) + "/arm " + 
                                    juce::String(armed ? "1.0" : "0.0"));
            juce::Logger::writeToLog("========================================");
        }
        else if (command == "monitor")
        {
            bool monitoring;
            if (message.size() >= 1)
            {
                float value = message[0].isFloat32() ? message[0].getFloat32() : (float)message[0].getInt32();
                monitoring = value > 0.5f;
                
                juce::Logger::writeToLog("========================================");
                juce::Logger::writeToLog("OSC MONITOR COMMAND");
                juce::Logger::writeToLog("  Track: " + juce::String(trackNumber));
                juce::Logger::writeToLog("  Value received: " + juce::String(value));
                juce::Logger::writeToLog("  Setting monitoring state to: " + juce::String(monitoring ? "ON" : "OFF"));
                juce::Logger::writeToLog("========================================");
            }
            else
            {
                // Toggle current state
                Track* track = audioEngine->getTrack(trackIndex);
                monitoring = track ? !track->isMonitoring() : true;
                juce::Logger::writeToLog("OSC: MONITOR toggle (no value) -> " + juce::String(monitoring ? "ON" : "OFF"));
            }
            
            juce::Logger::writeToLog("OSC: *** CALLING audioEngine->setTrackMonitoring(" + 
                                    juce::String(trackIndex) + ", " + 
                                    juce::String(monitoring ? "true" : "false") + ") ***");
            
            audioEngine->setTrackMonitoring(trackIndex, monitoring);
            
            juce::Logger::writeToLog("OSC: *** RETURNED from setTrackMonitoring ***");
            
            // Verify it was set
            Track* track = audioEngine->getTrack(trackIndex);
            if (track)
            {
                bool actualState = track->isMonitoring();
                juce::Logger::writeToLog("OSC: VERIFICATION - Track " + juce::String(trackNumber) + 
                                        " monitoring state is now: " + juce::String(actualState ? "ON" : "OFF"));
                
                if (actualState != monitoring)
                {
                    juce::Logger::writeToLog("OSC: *** ERROR - State mismatch! Wanted " + 
                                            juce::String(monitoring ? "ON" : "OFF") + 
                                            " but got " + juce::String(actualState ? "ON" : "OFF") + " ***");
                }
            }
            
            // Send feedback (use trackNumber, not engine index)
            sender->send("/track/" + juce::String(trackNumber) + "/monitor", monitoring ? 1.0f : 0.0f);
            juce::Logger::writeToLog("OSC: Sent feedback message /track/" + juce::String(trackNumber) + "/monitor " + 
                                    juce::String(monitoring ? "1.0" : "0.0"));
            juce::Logger::writeToLog("========================================");
        }
    }
    else
    {
        juce::Logger::writeToLog("OSC: ERROR - Track message parsing failed!");
        juce::Logger::writeToLog("  parts.size()=" + juce::String(parts.size()));
        if (parts.size() > 0)
            juce::Logger::writeToLog("  parts[0]='" + parts[0] + "'");
    }
}


void OSCController::sendTrackArmState(int trackIndex, bool armed)
{
    sender->send("/track/" + juce::String(trackIndex + 1) + "/arm", armed ? 1.0f : 0.0f);
}

void OSCController::sendTrackMonitorState(int trackIndex, bool monitoring)
{
    sender->send("/track/" + juce::String(trackIndex + 1) + "/monitor", monitoring ? 1.0f : 0.0f);
}



void OSCController::sendAllTransportStates(bool play, bool stop, bool record, bool rewind, bool ffwd, bool pause)
{
    // Send individual button states as floats
    sender->send("/transport/play", play ? 1.0f : 0.0f);
    sender->send("/transport/stop", stop ? 1.0f : 0.0f);
    sender->send("/transport/record", record ? 1.0f : 0.0f);
    sender->send("/transport/rewind", rewind ? 1.0f : 0.0f);
    sender->send("/transport/ffwd", ffwd ? 1.0f : 0.0f);
    sender->send("/transport/pause", pause ? 1.0f : 0.0f);
}

void OSCController::sendTimecode(double timeInSeconds)
{
    int totalSeconds = (int)timeInSeconds;
    int minutes = totalSeconds / 60;
    int seconds = totalSeconds % 60;
    int milliseconds = (int)((timeInSeconds - totalSeconds) * 1000.0);
    
    juce::String timecode = juce::String(minutes) + ":" + 
                           juce::String(seconds).paddedLeft('0', 2) + "." + 
                           juce::String(milliseconds).paddedLeft('0', 3);
    
    sender->send("/transport/time", timecode);
}

void OSCController::sendBPM(double bpm)
{
    sender->send("/transport/bpm", (float)bpm);
}

void OSCController::sendClickEnabled(bool enabled)
{
    if (!sender) return;
    sender->send("/click/enable", enabled ? 1.0f : 0.0f);
    juce::Logger::writeToLog("OSC: Sent /click/enable = " + juce::String(enabled ? 1 : 0));
}

void OSCController::sendSpeed(double speed, int semitones)
{
    if (!sender) return;
    
    // Calculate percentage (100% = 1.0x speed)
    double percent = speed * 100.0;
    
    // Send multiple messages for different controller needs
    sender->send("/transport/speed", (float)speed);           // Raw multiplier (1.0 = normal)
    sender->send("/transport/speed/percent", (float)percent); // Percentage (100 = normal)
    sender->send("/transport/speed/semitones", (int)semitones); // Semitone offset from 1.0x
}

void OSCController::sendMessage(const juce::String& address, float value)
{
    if (!sender) return;
    sender->send(address, value);
}

void OSCController::handleClickMessage(const juce::OSCMessage& message)
{
    juce::String address = message.getAddressPattern().toString();
    juce::Logger::writeToLog("OSC handleClickMessage: " + address);
    
    if (!audioEngine) return;
    
    if (address == "/click/enable")
    {
        // Value: 0 = off, 1 = on, no args = toggle
        bool enabled;
        if (message.size() > 0 && message[0].isFloat32())
            enabled = message[0].getFloat32() > 0.5f;
        else if (message.size() > 0 && message[0].isInt32())
            enabled = message[0].getInt32() != 0;
        else
            enabled = !audioEngine->getClickEnabled();  // Toggle if no argument
        
        audioEngine->setClickEnabled(enabled);
        sendClickEnabled(enabled);  // Send feedback
        juce::Logger::writeToLog("OSC: Click " + juce::String(enabled ? "ENABLED" : "DISABLED") + " via OSC");
    }
    else
    {
        // Note: BPM is project-wide. Use /transport/bpm/set|increase|decrease to change it.
        juce::Logger::writeToLog("OSC: Unknown click address: " + address + 
                                " (use /transport/bpm/* to change tempo)");
    }
}

void OSCController::sendBeat(int bar, int beat, int tick)
{
    // Send as bar.beat.tick string (e.g., "1.1.00")
    juce::String beatString = juce::String(bar) + "." + 
                             juce::String(beat) + "." + 
                             juce::String(tick).paddedLeft('0', 2);
    sender->send("/transport/beat", beatString);
}

void OSCController::sendTrackVU(int trackIndex, bool led1, bool led2, bool led3)
{
    if (!sender) return;
    
    int trackNumber = trackIndex + 1;  // Convert to 1-based
    
    // Send individual LED states
    sender->send("/track/" + juce::String(trackNumber) + "/vu/led1", led1 ? 1.0f : 0.0f);
    sender->send("/track/" + juce::String(trackNumber) + "/vu/led2", led2 ? 1.0f : 0.0f);
    sender->send("/track/" + juce::String(trackNumber) + "/vu/led3", led3 ? 1.0f : 0.0f);
    
    // Also send combined message for convenience
    sender->send("/track/" + juce::String(trackNumber) + "/vu", 
                led1 ? 1.0f : 0.0f, 
                led2 ? 1.0f : 0.0f, 
                led3 ? 1.0f : 0.0f);
}

//==============================================================================
// Marker OSC Messages

void OSCController::handleMarkerMessage(const juce::OSCMessage& message)
{
    juce::String address = message.getAddressPattern().toString();
    
    juce::Logger::writeToLog("=== handleMarkerMessage called ===");
    juce::Logger::writeToLog("  Address: " + address);
    juce::Logger::writeToLog("  Arguments: " + juce::String(message.size()));
    
    // /marker/add [timestamp] [label]
    if (address == "/marker/add")
    {
        // Check if this is a button press/release
        bool isButtonPress = false;
        if (message.size() > 0 && message[0].isFloat32())
        {
            float firstValue = message[0].getFloat32();
            // If first arg is 0 or 1, it's likely a button value
            if (firstValue == 0.0f || firstValue == 1.0f)
            {
                if (firstValue < 0.5f)
                    return;  // Ignore button release
                isButtonPress = true;
            }
        }
        
        double timestamp = 0.0;
        juce::String label = "";
        
        if (!isButtonPress && message.size() > 0 && message[0].isFloat32())
        {
            // First arg is timestamp
            timestamp = message[0].getFloat32();
            
            if (message.size() > 1 && message[1].isString())
                label = message[1].getString();
        }
        else if (audioEngine)
        {
            // Use current time
            timestamp = audioEngine->getTransportState().currentTime;
        }
        
        if (audioEngine)
        {
            juce::Logger::writeToLog("OSC: Adding marker at " + juce::String(timestamp, 2) + "s");
            audioEngine->addMarker(timestamp, label);
        }
    }
    // /marker/remove [number]
    else if (address == "/marker/remove")
    {
        if (message.size() > 0 && message[0].isInt32())
        {
            int markerNumber = message[0].getInt32();
            if (audioEngine)
            {
                juce::Logger::writeToLog("OSC: Removing marker #" + juce::String(markerNumber));
                audioEngine->removeMarker(markerNumber);
            }
        }
    }
    // /marker/next
    else if (address == "/marker/next")
    {
        juce::Logger::writeToLog("OSC: handleMarkerMessage - /marker/next");
        juce::Logger::writeToLog("  Message size: " + juce::String(message.size()));
        
        if (message.size() > 0)
        {
            juce::Logger::writeToLog("  First arg is float32? " + juce::String(message[0].isFloat32() ? "YES" : "NO"));
            juce::Logger::writeToLog("  First arg is int32? " + juce::String(message[0].isInt32() ? "YES" : "NO"));
        }
        
        // Check for button value (float or int)
        float buttonValue = 0.0f;
        bool hasValue = false;
        
        if (message.size() > 0)
        {
            if (message[0].isFloat32())
            {
                buttonValue = message[0].getFloat32();
                hasValue = true;
                juce::Logger::writeToLog("  Got float value: " + juce::String(buttonValue));
            }
            else if (message[0].isInt32())
            {
                buttonValue = (float)message[0].getInt32();
                hasValue = true;
                juce::Logger::writeToLog("  Got int value (converted to float): " + juce::String(buttonValue));
            }
        }
        
        // Only respond to button press (value > 0.5), ignore release (value = 0)
        if (hasValue)
        {
            juce::Logger::writeToLog("  Value > 0.5? " + juce::String(buttonValue > 0.5f ? "YES" : "NO"));
            juce::Logger::writeToLog("  AudioEngine exists? " + juce::String(audioEngine ? "YES" : "NO"));
            
            if (buttonValue > 0.5f && audioEngine)
            {
                juce::Logger::writeToLog("OSC: /marker/next pressed - calling goToNextMarker()");
                audioEngine->goToNextMarker();
            }
            else if (buttonValue <= 0.5f)
            {
                juce::Logger::writeToLog("OSC: /marker/next - button release (value=" + juce::String(buttonValue) + "), ignoring");
            }
            else
            {
                juce::Logger::writeToLog("OSC: /marker/next - audioEngine is NULL!");
            }
        }
        else if (audioEngine)
        {
            juce::Logger::writeToLog("OSC: /marker/next - no value argument, calling directly");
            audioEngine->goToNextMarker();
        }
        else
        {
            juce::Logger::writeToLog("OSC: /marker/next - no valid arguments and audioEngine is NULL");
        }
    }
    // /marker/prev or /marker/previous
    else if (address == "/marker/prev" || address == "/marker/previous")
    {
        juce::Logger::writeToLog("OSC: handleMarkerMessage - /marker/prev");
        juce::Logger::writeToLog("  Message size: " + juce::String(message.size()));
        
        if (message.size() > 0)
        {
            juce::Logger::writeToLog("  First arg is float32? " + juce::String(message[0].isFloat32() ? "YES" : "NO"));
            juce::Logger::writeToLog("  First arg is int32? " + juce::String(message[0].isInt32() ? "YES" : "NO"));
        }
        
        // Check for button value (float or int)
        float buttonValue = 0.0f;
        bool hasValue = false;
        
        if (message.size() > 0)
        {
            if (message[0].isFloat32())
            {
                buttonValue = message[0].getFloat32();
                hasValue = true;
                juce::Logger::writeToLog("  Got float value: " + juce::String(buttonValue));
            }
            else if (message[0].isInt32())
            {
                buttonValue = (float)message[0].getInt32();
                hasValue = true;
                juce::Logger::writeToLog("  Got int value (converted to float): " + juce::String(buttonValue));
            }
        }
        
        // Only respond to button press (value > 0.5), ignore release (value = 0)
        if (hasValue)
        {
            juce::Logger::writeToLog("  Value > 0.5? " + juce::String(buttonValue > 0.5f ? "YES" : "NO"));
            juce::Logger::writeToLog("  AudioEngine exists? " + juce::String(audioEngine ? "YES" : "NO"));
            
            if (buttonValue > 0.5f && audioEngine)
            {
                juce::Logger::writeToLog("OSC: /marker/prev pressed - calling goToPreviousMarker()");
                audioEngine->goToPreviousMarker();
            }
            else if (buttonValue <= 0.5f)
            {
                juce::Logger::writeToLog("OSC: /marker/prev - button release (value=" + juce::String(buttonValue) + "), ignoring");
            }
            else
            {
                juce::Logger::writeToLog("OSC: /marker/prev - audioEngine is NULL!");
            }
        }
        else if (audioEngine)
        {
            juce::Logger::writeToLog("OSC: /marker/prev - no value argument, calling directly");
            audioEngine->goToPreviousMarker();
        }
        else
        {
            juce::Logger::writeToLog("OSC: /marker/prev - no valid arguments and audioEngine is NULL");
        }
    }
    // /marker/goto [number] or /marker/1, /marker/2, etc.
    // NOTE: This must come AFTER /marker/next and /marker/prev checks
    // because the startsWith("/marker/") would match those too!
    else if (address.startsWith("/marker/goto") || address.startsWith("/marker/"))
    {
        // Check for button value (ignore button release)
        if (message.size() > 0 && message[0].isFloat32())
        {
            float value = message[0].getFloat32();
            if (value < 0.5f)
                return;  // Ignore button release
        }
        
        int markerNumber = 0;
        
        // Check if it's /marker/goto [number]
        if (address == "/marker/goto" && message.size() > 0)
        {
            if (message[0].isInt32())
                markerNumber = message[0].getInt32();
            else if (message[0].isFloat32())
            {
                // Could be button value or marker number
                float val = message[0].getFloat32();
                if (val > 1.5f)  // Likely a marker number
                    markerNumber = (int)val;
                // else it's a button press, get number from second arg
                else if (message.size() > 1 && message[1].isInt32())
                    markerNumber = message[1].getInt32();
            }
        }
        // Or /marker/1, /marker/2, etc.
        else
        {
            juce::String numStr = address.fromLastOccurrenceOf("/", false, false);
            if (numStr == "goto" || numStr == "add" || numStr == "remove" || 
                numStr == "next" || numStr == "prev" || numStr == "previous")
                return;  // Not a numbered marker
            markerNumber = numStr.getIntValue();
        }
        
        if (audioEngine && markerNumber > 0)
        {
            juce::Logger::writeToLog("OSC: Jumping to marker #" + juce::String(markerNumber));
            audioEngine->goToMarker(markerNumber);
        }
    }
}

void OSCController::sendMarkerAdded(int markerNumber, double timestamp, const juce::String& label)
{
    if (!sender) return;
    
    sender->send("/marker/added", markerNumber, (float)timestamp, label);
    
    // Also send marker info
    sendMarkerInfo(markerNumber, timestamp, label);
}

void OSCController::sendMarkerRemoved(int markerNumber)
{
    if (!sender) return;
    
    sender->send("/marker/removed", markerNumber);
}

void OSCController::sendMarkerInfo(int markerNumber, double timestamp, const juce::String& label)
{
    if (!sender) return;
    
    sender->send("/marker/info", markerNumber, (float)timestamp, label);
    
    // Send separate outputs for number and name
    sender->send("/marker/number", markerNumber);
    sender->send("/marker/name", label);
}
