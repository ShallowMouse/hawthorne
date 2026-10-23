#include "TapeMachineUI.h"

// Vintage color scheme
namespace VintageColors
{
    const juce::Colour background(0xff2a2620);      // Dark brown
    const juce::Colour panel(0xff3a3630);           // Medium brown
    const juce::Colour accent(0xffc4b49a);          // Beige/tan
    const juce::Colour text(0xfff4f4e8);            // Off-white
    const juce::Colour vuGreen(0xff7fb069);         // Muted green
    const juce::Colour vuYellow(0xffd4a574);        // Muted yellow
    const juce::Colour vuRed(0xffb85c5c);           // Muted red
    const juce::Colour recordRed(0xffcc4444);       // Record indicator
    const juce::Colour armed(0xffffaa44);           // Armed indicator
}

//==============================================================================
// TransportPanel Implementation
//==============================================================================
TransportPanel::TransportPanel()
{
    // Setup buttons with vintage symbols
    rewindButton.setButtonText("⏮");
    playButton.setButtonText("▶");
    stopButton.setButtonText("⏹");
    recordButton.setButtonText("⏺");
    ffButton.setButtonText("⏭");
    
    // Style buttons
    for (auto* btn : { &rewindButton, &playButton, &stopButton, &recordButton, &ffButton })
    {
        addAndMakeVisible(btn);
        btn->setColour(juce::TextButton::buttonColourId, VintageColors::panel);
        btn->setColour(juce::TextButton::textColourOffId, VintageColors::text);
    }
    
    // Special styling for record button
    recordButton.setColour(juce::TextButton::buttonColourId, VintageColors::panel);
    recordButton.setColour(juce::TextButton::buttonOnColourId, VintageColors::recordRed);
    
    // Timecode display
    timecodeLabel.setText("00:00:00.000", juce::dontSendNotification);
    timecodeLabel.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 24.0f, juce::Font::bold));
    timecodeLabel.setJustificationType(juce::Justification::centred);
    timecodeLabel.setColour(juce::Label::textColourId, VintageColors::accent);
    timecodeLabel.setColour(juce::Label::backgroundColourId, VintageColors::background);
    addAndMakeVisible(timecodeLabel);
    
    // Button callbacks
    rewindButton.onClick = [this]() { if (onRewind) onRewind(); };
    playButton.onClick = [this]() { if (onPlay) onPlay(); };
    stopButton.onClick = [this]() { if (onStop) onStop(); };
    recordButton.onClick = [this]() { if (onRecord) onRecord(); };
    ffButton.onClick = [this]() { if (onFastForward) onFastForward(); };
}

void TransportPanel::paint(juce::Graphics& g)
{
    g.fillAll(VintageColors::panel);
    
    // Draw border
    g.setColour(VintageColors::accent.withAlpha(0.3f));
    g.drawRect(getLocalBounds(), 1);
}

void TransportPanel::resized()
{
    auto area = getLocalBounds().reduced(10);
    
    // Timecode at top
    timecodeLabel.setBounds(area.removeFromTop(40));
    
    area.removeFromTop(10);
    
    // Transport buttons in a row
    auto buttonArea = area.removeFromTop(50);
    int buttonWidth = 60;
    int spacing = 10;
    
    rewindButton.setBounds(buttonArea.removeFromLeft(buttonWidth));
    buttonArea.removeFromLeft(spacing);
    playButton.setBounds(buttonArea.removeFromLeft(buttonWidth));
    buttonArea.removeFromLeft(spacing);
    stopButton.setBounds(buttonArea.removeFromLeft(buttonWidth));
    buttonArea.removeFromLeft(spacing);
    recordButton.setBounds(buttonArea.removeFromLeft(buttonWidth));
    buttonArea.removeFromLeft(spacing);
    ffButton.setBounds(buttonArea.removeFromLeft(buttonWidth));
}

void TransportPanel::setTimecode(const juce::String& timecode)
{
    timecodeLabel.setText(timecode, juce::dontSendNotification);
}

void TransportPanel::setPlaying(bool playing)
{
    isPlaying = playing;
    playButton.setToggleState(playing, juce::dontSendNotification);
}

void TransportPanel::setRecording(bool recording)
{
    isRecording = recording;
    recordButton.setToggleState(recording, juce::dontSendNotification);
}

//==============================================================================
// TrackStrip Implementation
//==============================================================================
TrackStrip::TrackStrip(int trackNumber, bool isMixdownTrack)
    : trackNum(trackNumber), 
      trackName(isMixdownTrack ? "MIXDOWN (L+R)" : "Track " + juce::String(trackNumber)),
      isMixdown(isMixdownTrack)
{
    // Track name label
    nameLabel.setText(trackName, juce::dontSendNotification);
    nameLabel.setFont(juce::Font(isMixdown ? 13.0f : 12.0f, juce::Font::bold));
    nameLabel.setJustificationType(juce::Justification::centred);
    nameLabel.setColour(juce::Label::textColourId, 
                       isMixdown ? juce::Colour(VintageLookAndFeel::accentGreen) : VintageColors::accent);
    addAndMakeVisible(nameLabel);
    
    // Arm button - KEEP CONSISTENT WITH OTHER TRACKS
    armButton.setButtonText("A");  // Always "A" for Arm
    armButton.setClickingTogglesState(true);
    armButton.setColour(juce::TextButton::buttonColourId, VintageColors::panel);
    armButton.setColour(juce::TextButton::buttonOnColourId, VintageColors::armed);  // Same amber as other tracks
    armButton.setColour(juce::TextButton::textColourOffId, VintageColors::text);
    armButton.setColour(juce::TextButton::textColourOnId, juce::Colours::black);
    armButton.onClick = [this]() { 
        armed = armButton.getToggleState();
        if (onArmChanged) onArmChanged(armed);
    };
    addAndMakeVisible(armButton);
}

void TrackStrip::paint(juce::Graphics& g)
{
    auto area = getLocalBounds();
    
    // Background - different color for mixdown track
    if (isMixdown)
    {
        // Darker background for mixdown track
        g.setColour(VintageColors::background.darker(0.2f));
        g.fillRect(area);
        
        // Green accent border for mixdown track
        g.setColour(juce::Colour(VintageLookAndFeel::accentGreen));
        g.drawRect(area, 2);  // Thicker border
        
        // Inner border for extra distinction
        g.setColour(juce::Colour(VintageLookAndFeel::accentGreen).darker(0.3f));
        g.drawRect(area.reduced(2), 1);
    }
    else
    {
        // Normal track background
        g.setColour(VintageColors::background);
        g.fillRect(area);
        
        // Normal border
        g.setColour(VintageColors::panel);
        g.drawRect(area, 1);
    }
    
    // Waveform area
    auto waveformArea = area.reduced(5, 5).withTrimmedTop(60).withTrimmedBottom(40);
    drawWaveform(g, waveformArea);
    
    // VU meter area
    auto vuArea = area.removeFromBottom(35).reduced(5, 5);
    drawVUMeter(g, vuArea);
    
    // Recording indicator
    if (recording)
    {
        g.setColour(VintageColors::recordRed);
        g.fillEllipse(area.getX() + 5, area.getY() + 5, 10, 10);
    }
    
    // Stereo indicator for mixdown track
    if (isMixdown)
    {
        g.setColour(juce::Colour(VintageLookAndFeel::accentGreen));
        g.setFont(juce::Font(10.0f, juce::Font::bold));
        g.drawText("STEREO", area.getX() + area.getWidth() - 45, area.getY() + 5, 40, 12, 
                  juce::Justification::centred);
    }
}

void TrackStrip::resized()
{
    auto area = getLocalBounds().reduced(5);
    
    // Track name at top
    nameLabel.setBounds(area.removeFromTop(25));
    
    area.removeFromTop(5);
    
    // Arm button
    armButton.setBounds(area.removeFromTop(30).reduced(10, 0));
}

void TrackStrip::setTrackName(const juce::String& name)
{
    trackName = name;
    nameLabel.setText(name, juce::dontSendNotification);
}

void TrackStrip::setArmed(bool isArmed)
{
    armed = isArmed;
    armButton.setToggleState(armed, juce::dontSendNotification);
    repaint();
}

void TrackStrip::setRecording(bool isRecording)
{
    recording = isRecording;
    repaint();
}

void TrackStrip::setLevel(float level)
{
    currentLevel = juce::jlimit(0.0f, 1.0f, level);
    repaint();
}

void TrackStrip::setWaveform(const std::vector<float>& peaks)
{
    waveformPeaks = peaks;
    repaint();
}

void TrackStrip::drawVUMeter(juce::Graphics& g, juce::Rectangle<int> area)
{
    // Background
    g.setColour(VintageColors::panel.darker(0.3f));
    g.fillRect(area);
    
    // Level bar
    if (currentLevel > 0.0f)
    {
        int barWidth = (int)(area.getWidth() * currentLevel);
        auto barArea = area.withWidth(barWidth);
        
        // Color based on level
        juce::Colour barColor = VintageColors::vuGreen;
        if (currentLevel > 0.7f)
            barColor = VintageColors::vuYellow;
        if (currentLevel > 0.9f)
            barColor = VintageColors::vuRed;
        
        g.setColour(barColor);
        g.fillRect(barArea);
    }
    
    // Border
    g.setColour(VintageColors::panel);
    g.drawRect(area, 1);
}

void TrackStrip::drawWaveform(juce::Graphics& g, juce::Rectangle<int> area)
{
    // Background
    g.setColour(VintageColors::panel.darker(0.5f));
    g.fillRect(area);
    
    // Draw waveform if we have data
    if (!waveformPeaks.empty())
    {
        g.setColour(VintageColors::vuGreen);
        
        float width = (float)area.getWidth();
        float height = (float)area.getHeight();
        float centerY = area.getY() + height / 2.0f;
        
        juce::Path waveformPath;
        bool firstPoint = true;
        
        for (size_t i = 0; i < waveformPeaks.size(); ++i)
        {
            float x = area.getX() + (i / (float)waveformPeaks.size()) * width;
            float peak = waveformPeaks[i];
            float y = centerY - (peak * height * 0.4f);
            
            if (firstPoint)
            {
                waveformPath.startNewSubPath(x, centerY);
                firstPoint = false;
            }
            waveformPath.lineTo(x, y);
        }
        
        // Mirror for bottom half
        for (int i = (int)waveformPeaks.size() - 1; i >= 0; --i)
        {
            float x = area.getX() + (i / (float)waveformPeaks.size()) * width;
            float peak = waveformPeaks[i];
            float y = centerY + (peak * height * 0.4f);
            waveformPath.lineTo(x, y);
        }
        
        waveformPath.closeSubPath();
        g.fillPath(waveformPath);
    }
    
    // Border
    g.setColour(VintageColors::panel);
    g.drawRect(area, 1);
}

//==============================================================================
// TapeMachineUI Implementation
//==============================================================================
TapeMachineUI::TapeMachineUI(AudioEngine* engine)
    : audioEngine(engine)
{
    juce::Logger::writeToLog("Creating native TapeMachineUI...");
    
    // Title/Logo
    auto titleLabel = new juce::Label("title", "VTAPE//24");
    titleLabel->setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 28.0f, juce::Font::bold));
    titleLabel->setJustificationType(juce::Justification::centred);
    titleLabel->setColour(juce::Label::textColourId, VintageColors::accent);
    addAndMakeVisible(titleLabel);
    
    // Transport panel
    addAndMakeVisible(transportPanel);
    
    transportPanel.onPlay = [this]() {
        if (audioEngine) audioEngine->play();
    };
    
    transportPanel.onStop = [this]() {
        if (audioEngine) audioEngine->stop();
    };
    
    transportPanel.onRecord = [this]() {
        if (audioEngine) audioEngine->record();
    };
    
    transportPanel.onRewind = [this]() {
        if (audioEngine) audioEngine->rewind();
    };
    
    transportPanel.onFastForward = [this]() {
        if (audioEngine) audioEngine->fastForward();
    };
    
    // BPM changed callback
    transportPanel.onBPMChanged = [this](double newBPM) {
        if (auto* session = audioEngine->getCurrentSession())
        {
            session->setBPM(newBPM);
            juce::Logger::writeToLog("BPM changed to: " + juce::String(newBPM, 1));
        }
    };
    
    // Settings button
    settingsButton.setButtonText("⚙ Settings");
    settingsButton.setColour(juce::TextButton::buttonColourId, VintageColors::panel);
    settingsButton.setColour(juce::TextButton::textColourOffId, VintageColors::text);
    settingsButton.onClick = [this]() {
        if (onSettingsClicked) onSettingsClicked();
    };
    addAndMakeVisible(settingsButton);
    
    // Status bar
    statusLabel.setText("● READY", juce::dontSendNotification);
    statusLabel.setFont(juce::Font(12.0f));
    statusLabel.setColour(juce::Label::textColourId, VintageColors::vuGreen);
    statusLabel.setColour(juce::Label::backgroundColourId, VintageColors::panel);
    addAndMakeVisible(statusLabel);
    
    // Setup tracks
    setupTracks();
    
    // Tracks viewport
    addAndMakeVisible(tracksViewport);
    tracksViewport.setViewedComponent(&tracksContainer, false);
    tracksViewport.setScrollBarsShown(true, false);
    
    // Start update timer (30fps)
    startTimer(33);
    
    juce::Logger::writeToLog("TapeMachineUI created successfully");
}

TapeMachineUI::~TapeMachineUI()
{
    stopTimer();
}

void TapeMachineUI::paint(juce::Graphics& g)
{
    g.fillAll(VintageColors::background);
}

void TapeMachineUI::resized()
{
    auto area = getLocalBounds();
    
    // Title at very top
    // (We'll position dynamically if we add title label to members)
    
    // Transport controls at top
    transportPanel.setBounds(area.removeFromTop(120).reduced(10));
    
    // Settings button in top right
    settingsButton.setBounds(getWidth() - 120, 10, 110, 30);
    
    area.removeFromTop(10);
    
    // Status bar at bottom
    statusLabel.setBounds(area.removeFromBottom(25));
    
    // Tracks fill remaining space
    tracksViewport.setBounds(area);
    
    // Layout tracks horizontally in container
    int trackWidth = 100;
    int totalWidth = trackWidth * numTracks;
    tracksContainer.setBounds(0, 0, totalWidth, area.getHeight());
    
    for (int i = 0; i < trackStrips.size(); ++i)
    {
        trackStrips[i]->setBounds(i * trackWidth, 0, trackWidth, area.getHeight());
    }
}

void TapeMachineUI::timerCallback()
{
    if (audioEngine)
    {
        updateFromEngine();
    }
}

void TapeMachineUI::setupTracks()
{
    // Get actual track count from AudioEngine
    if (audioEngine)
        numTracks = audioEngine->getNumTracks();
    
    juce::Logger::writeToLog("Setting up " + juce::String(numTracks) + " tracks...");
    
    for (int i = 0; i < numTracks; ++i)
    {
        // Check if this is the stereo return/mixdown track
        bool isMixdown = false;
        if (audioEngine)
        {
            Track* track = audioEngine->getTrack(i);
            if (track)
            {
                juce::Logger::writeToLog("  Track " + juce::String(i + 1) + ": " + track->getName() + 
                                        " (stereo=" + juce::String(track->isStereo() ? "yes" : "no") +
                                        ", mixdown=" + juce::String(track->isMixdown() ? "yes" : "no") + ")");
                if (track->isMixdown())
                {
                    isMixdown = true;
                }
            }
            else
            {
                juce::Logger::writeToLog("  Track " + juce::String(i + 1) + ": NULL track!");
            }
        }
        
        auto* strip = new TrackStrip(i + 1, isMixdown);
        strip->onArmChanged = [this, i](bool armed) {
            if (audioEngine) audioEngine->setTrackArmed(i, armed);
        };
        trackStrips.add(strip);
        tracksContainer.addAndMakeVisible(strip);
    }
    
    juce::Logger::writeToLog("Tracks setup complete");
}

void TapeMachineUI::updateFromEngine()
{
    // Update transport state
    auto state = audioEngine->getTransportState();
    transportPanel.setPlaying(state.isPlaying);
    transportPanel.setRecording(state.isRecording);
    transportPanel.setTimecode(state.currentTime);
    transportPanel.setBarBeat(state.bar, state.beat, state.tick);
    
    // Update BPM display from session
    if (auto* session = audioEngine->getCurrentSession())
    {
        transportPanel.setBPM(session->getBPM(), 
                            session->getTimeSignatureNumerator(), 
                            session->getTimeSignatureDenominator());
    }
    
    // Update track waveforms
    for (int i = 0; i < trackStrips.size(); ++i)
    {
        Track* track = audioEngine->getTrack(i);
        if (track && track->hasAudio())
        {
            // Get waveform peaks for display (1000 points for smooth display)
            auto peaks = track->getWaveformPeaks(1000);
            trackStrips[i]->setWaveform(peaks);
        }
        
        // If recording, get real-time recording buffer for waveform display
        if (track && track->isRecording())
        {
            // Get the last 2 seconds of recording buffer (96000 samples at 48kHz)
            std::vector<float> recordingBuffer = track->getRecordingBuffer(96000);
            // Note: TrackStrip would need setRecordingWaveform() method if this UI is used
            // trackStrips[i]->setRecordingWaveform(recordingBuffer);
        }
    }
    
    // Update status
    if (state.isRecording)
        statusLabel.setText("● RECORDING", juce::dontSendNotification);
    else if (state.isPlaying)
        statusLabel.setText("● PLAYING", juce::dontSendNotification);
    else
        statusLabel.setText("● READY", juce::dontSendNotification);
}
