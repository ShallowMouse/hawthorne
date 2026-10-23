#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "VintageLookAndFeel.h"

class TransportButton : public juce::Component
{
public:
    enum IconType { Rewind, FastForward, Stop, Play, Pause, Record, Settings, Save, Session };
    
    TransportButton(IconType type) : iconType(type) {}
    
    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        bool isDown = isMouseButtonDown();
        bool isOver = isMouseOver();
        
        // Button gradient matching CSS
        juce::ColourGradient gradient;
        if (iconType == Record)
        {
            if (isRecording || isDown)
                gradient = juce::ColourGradient(
                    juce::Colour(0xffa85050), 0.0f, 0.0f,
                    juce::Colour(VintageLookAndFeel::accentRed), 0.0f, bounds.getHeight(), false);
            else if (isOver)
                gradient = juce::ColourGradient(
                    juce::Colour(0xffb86060), 0.0f, 0.0f,
                    juce::Colour(0xff9b4848), 0.0f, bounds.getHeight(), false);
            else
                gradient = juce::ColourGradient(
                    juce::Colour(0xffa85050), 0.0f, 0.0f,
                    juce::Colour(VintageLookAndFeel::accentRed), 0.0f, bounds.getHeight(), false);
        }

        else if (iconType == Pause && isPaused)
        {
            // Blue for pause when paused
            gradient = juce::ColourGradient(
                juce::Colour(0xff6a8aaa), 0.0f, 0.0f,
                juce::Colour(0xff4a6a8a), 0.0f, bounds.getHeight(), false);
        }
        else if (iconType == Stop)
        {
            // Yellow for stop button - bright when stopped (nothing playing)
            bool isStopped = !isPlaying && !isRecording && !isPaused;
            
            if (isStopped)
                gradient = juce::ColourGradient(
                    juce::Colour(0xffc8c870), 0.0f, 0.0f,
                    juce::Colour(0xffb8b860), 0.0f, bounds.getHeight(), false);
            else if (isDown)
                gradient = juce::ColourGradient(
                    juce::Colour(0xff8a8a50), 0.0f, 0.0f,
                    juce::Colour(0xffb8b860), 0.0f, bounds.getHeight(), false);
            else if (isOver)
                gradient = juce::ColourGradient(
                    juce::Colour(0xffa8a860), 0.0f, 0.0f,
                    juce::Colour(0xff9a9a50), 0.0f, bounds.getHeight(), false);
            else
                gradient = juce::ColourGradient(
                    juce::Colour(0xff7a7a40), 0.0f, 0.0f,
                    juce::Colour(0xff6a6a30), 0.0f, bounds.getHeight(), false);
        }
        else if (iconType == Play && isPlaying)
        {
            // GREEN for play button when playing
            if (isDown)
                gradient = juce::ColourGradient(
                    juce::Colour(0xff5aaa5a), 0.0f, 0.0f,
                    juce::Colour(0xff3a7a3a), 0.0f, bounds.getHeight(), false);
            else if (isOver)
                gradient = juce::ColourGradient(
                    juce::Colour(0xff6abb6a), 0.0f, 0.0f,
                    juce::Colour(0xff4a9a4a), 0.0f, bounds.getHeight(), false);
            else
                gradient = juce::ColourGradient(
                    juce::Colour(0xff5aaa5a), 0.0f, 0.0f,
                    juce::Colour(0xff4a9a4a), 0.0f, bounds.getHeight(), false);
        }
        else
        {
            if (isDown)
                gradient = juce::ColourGradient(
                    juce::Colour(0xff5a5a4a), 0.0f, 0.0f,
                    juce::Colour(VintageLookAndFeel::metalLight), 0.0f, bounds.getHeight(), false);
            else if (isOver)
                gradient = juce::ColourGradient(
                    juce::Colour(0xff9a9a8a), 0.0f, 0.0f,
                    juce::Colour(0xff6a6a5a), 0.0f, bounds.getHeight(), false);
            else
                gradient = juce::ColourGradient(
                    juce::Colour(VintageLookAndFeel::metalLight), 0.0f, 0.0f,
                    juce::Colour(0xff5a5a4a), 0.0f, bounds.getHeight(), false);
        }
        
        g.setGradientFill(gradient);
        g.fillRoundedRectangle(bounds, 6.0f);
        
        // Border
        g.setColour(juce::Colour(VintageLookAndFeel::metalDark));
        g.drawRoundedRectangle(bounds, 6.0f, 2.0f);
        
        // 3D highlight
        if (!isDown)
        {
            g.setColour(juce::Colours::white.withAlpha(0.2f));
            g.drawRoundedRectangle(bounds.reduced(2), 4.0f, 1.0f);
        }
        
        // Draw icon
        g.setColour(juce::Colour(VintageLookAndFeel::bgPanel));
        auto iconBounds = bounds.reduced(12);
        
        switch (iconType)
        {
            case Rewind:
                drawRewindIcon(g, iconBounds);
                break;
            case Stop:
                drawStopIcon(g, iconBounds);
                break;
            case Play:
                drawPlayIcon(g, iconBounds);
                break;
            case Pause:
                drawPauseIcon(g, iconBounds);
                break;
            case Record:
                drawRecordIcon(g, iconBounds);
                break;
            case FastForward:
                drawFastForwardIcon(g, iconBounds);
                break;
            case Settings:
                drawSettingsIcon(g, iconBounds);
                break;
            case Save:
                drawSaveIcon(g, iconBounds);
                break;
            case Session:
                drawSessionIcon(g, iconBounds);
                break;
        }
    }
    
    void mouseDown(const juce::MouseEvent&) override 
    { 
        repaint(); 
        if (onMouseDown) onMouseDown();
    }
    void mouseUp(const juce::MouseEvent&) override { 
        repaint();
        if (onMouseUp) onMouseUp();
        if (onClick) onClick();
    }
    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override 
    { 
        repaint(); 
        // If mouse leaves while held, fire mouseUp to avoid stuck state
        if (isMouseButtonDown())
        {
            if (onMouseUp) onMouseUp();
        }
    }
    
    void setPlaying(bool playing) { isPlaying = playing; repaint(); }
    void setRecording(bool recording) { isRecording = recording; repaint(); }
    void setPaused(bool paused) { isPaused = paused; repaint(); }
    void setTransportState(bool playing, bool recording, bool paused)
    {
        isPlaying = playing;
        isRecording = recording;
        isPaused = paused;
        repaint();
    }
    
    std::function<void()> onClick;
    std::function<void()> onMouseDown;   // Fires on button press (hold-to-activate)
    std::function<void()> onMouseUp;     // Fires on button release

private:
    void drawRewindIcon(juce::Graphics& g, juce::Rectangle<float> bounds)
    {
        // Double triangle pointing left
        juce::Path path;
        auto w = bounds.getWidth();
        auto h = bounds.getHeight();
        auto cx = bounds.getCentreX();
        auto cy = bounds.getCentreY();
        
        // Left triangle
        path.addTriangle(cx - w * 0.4f, cy,
                        cx - w * 0.1f, cy - h * 0.35f,
                        cx - w * 0.1f, cy + h * 0.35f);
        
        // Right triangle
        path.addTriangle(cx + w * 0.1f, cy,
                        cx + w * 0.4f, cy - h * 0.35f,
                        cx + w * 0.4f, cy + h * 0.35f);
        
        g.fillPath(path);
    }
    
    void drawPlayIcon(juce::Graphics& g, juce::Rectangle<float> bounds)
    {
        // Triangle pointing right
        juce::Path path;
        auto w = bounds.getWidth();
        auto h = bounds.getHeight();
        auto cx = bounds.getCentreX();
        auto cy = bounds.getCentreY();
        
        path.addTriangle(cx - w * 0.25f, cy - h * 0.4f,
                        cx - w * 0.25f, cy + h * 0.4f,
                        cx + w * 0.35f, cy);
        
        g.fillPath(path);
    }
    
    void drawStopIcon(juce::Graphics& g, juce::Rectangle<float> bounds)
    {
        // Square
        g.fillRoundedRectangle(bounds.reduced(4), 2.0f);
    }
    
    void drawPauseIcon(juce::Graphics& g, juce::Rectangle<float> bounds)
    {
        // Two vertical bars
        auto w = bounds.getWidth();
        auto h = bounds.getHeight();
        auto cx = bounds.getCentreX();
        auto cy = bounds.getCentreY();
        auto barWidth = w * 0.25f;
        auto barHeight = h * 0.7f;
        auto spacing = w * 0.15f;
        
        // Left bar
        g.fillRoundedRectangle(cx - spacing - barWidth, cy - barHeight / 2, 
                              barWidth, barHeight, 2.0f);
        
        // Right bar
        g.fillRoundedRectangle(cx + spacing, cy - barHeight / 2,
                              barWidth, barHeight, 2.0f);
    }
    
    void drawRecordIcon(juce::Graphics& g, juce::Rectangle<float> bounds)
    {
        // Circle
        g.fillEllipse(bounds.reduced(2));
    }
    
    void drawFastForwardIcon(juce::Graphics& g, juce::Rectangle<float> bounds)
    {
        // Double triangle pointing right
        juce::Path path;
        auto w = bounds.getWidth();
        auto h = bounds.getHeight();
        auto cx = bounds.getCentreX();
        auto cy = bounds.getCentreY();
        
        // Left triangle
        path.addTriangle(cx - w * 0.4f, cy,
                        cx - w * 0.4f, cy - h * 0.35f,
                        cx - w * 0.1f, cy);
        path.addTriangle(cx - w * 0.4f, cy,
                        cx - w * 0.4f, cy + h * 0.35f,
                        cx - w * 0.1f, cy);
        
        // Right triangle
        path.addTriangle(cx + w * 0.1f, cy,
                        cx + w * 0.1f, cy - h * 0.35f,
                        cx + w * 0.4f, cy);
        path.addTriangle(cx + w * 0.1f, cy,
                        cx + w * 0.1f, cy + h * 0.35f,
                        cx + w * 0.4f, cy);
        
        g.fillPath(path);
    }
    
    void drawSettingsIcon(juce::Graphics& g, juce::Rectangle<float> bounds)
    {
        // Gear icon
        juce::Path path;
        auto cx = bounds.getCentreX();
        auto cy = bounds.getCentreY();
        auto r = bounds.getWidth() * 0.4f;
        auto innerR = r * 0.5f;
        
        // Draw 6 teeth
        for (int i = 0; i < 6; ++i)
        {
            float angle1 = i * juce::MathConstants<float>::pi / 3.0f;
            float angle2 = angle1 + juce::MathConstants<float>::pi / 6.0f;
            float angle3 = angle1 + juce::MathConstants<float>::pi / 3.0f;
            
            float x1 = cx + std::cos(angle1) * r;
            float y1 = cy + std::sin(angle1) * r;
            float x2 = cx + std::cos(angle2) * (r * 1.2f);
            float y2 = cy + std::sin(angle2) * (r * 1.2f);
            float x3 = cx + std::cos(angle3) * r;
            float y3 = cy + std::sin(angle3) * r;
            
            if (i == 0)
                path.startNewSubPath(x1, y1);
            else
                path.lineTo(x1, y1);
            
            path.lineTo(x2, y2);
            path.lineTo(x3, y3);
        }
        
        path.closeSubPath();
        
        // Cut out center circle
        juce::Path center;
        center.addEllipse(cx - innerR, cy - innerR, innerR * 2, innerR * 2);
        path.addPath(center);
        path.setUsingNonZeroWinding(false);
        
        g.fillPath(path);
    }
    
    void drawSaveIcon(juce::Graphics& g, juce::Rectangle<float> bounds)
    {
        // Floppy disk icon
        auto w = bounds.getWidth();
        auto h = bounds.getHeight();
        auto cx = bounds.getCentreX();
        auto cy = bounds.getCentreY();
        
        // Outer rectangle (floppy body)
        juce::Rectangle<float> body(cx - w * 0.35f, cy - h * 0.35f, w * 0.7f, h * 0.7f);
        g.fillRoundedRectangle(body, 2.0f);
        
        // Top label area (darker)
        juce::Rectangle<float> label(body.getX(), body.getY(), body.getWidth(), body.getHeight() * 0.3f);
        g.setColour(juce::Colours::black.withAlpha(0.3f));
        g.fillRect(label);
        
        // Metal shutter at bottom
        juce::Rectangle<float> shutter(body.getX() + body.getWidth() * 0.15f, 
                                       body.getBottom() - body.getHeight() * 0.35f,
                                       body.getWidth() * 0.7f, 
                                       body.getHeight() * 0.25f);
        g.setColour(juce::Colours::black.withAlpha(0.5f));
        g.fillRoundedRectangle(shutter, 1.0f);
        
        // Small notch in top right corner
        juce::Path notch;
        notch.addTriangle(body.getRight() - w * 0.15f, body.getY(),
                         body.getRight(), body.getY(),
                         body.getRight(), body.getY() + h * 0.15f);
        g.setColour(juce::Colours::white);
        g.fillPath(notch);
    }

    void drawSessionIcon(juce::Graphics& g, juce::Rectangle<float> bounds)
    {
        // Document page with three ruled lines — "session notes" feel
        auto cx = bounds.getCentreX();
        auto cy = bounds.getCentreY();
        auto w  = bounds.getWidth()  * 0.60f;
        auto h  = bounds.getHeight() * 0.72f;

        // Page body
        juce::Rectangle<float> page(cx - w * 0.5f, cy - h * 0.5f, w, h);
        g.fillRoundedRectangle(page, 2.0f);

        // Folded corner (top-right)
        float foldSize = w * 0.28f;
        juce::Path fold;
        fold.addTriangle(page.getRight() - foldSize, page.getY(),
                         page.getRight(),             page.getY(),
                         page.getRight(),             page.getY() + foldSize);
        g.setColour(juce::Colours::black.withAlpha(0.35f));
        g.fillPath(fold);

        // Three ruled lines on the page
        g.setColour(juce::Colours::black.withAlpha(0.45f));
        float lineLeft  = page.getX()   + w * 0.15f;
        float lineRight = page.getRight() - foldSize * 0.6f;
        float lineSpacing = h * 0.20f;
        float startY = page.getY() + h * 0.30f;
        for (int i = 0; i < 3; ++i)
        {
            float y = startY + i * lineSpacing;
            g.drawLine(lineLeft, y, (i == 0 ? lineRight : page.getRight() - w * 0.12f), y, 1.2f);
        }
    }
    
    IconType iconType;
    bool isPlaying = false;
    bool isRecording = false;
    bool isPaused = false;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TransportButton)
};

class TransportComponent : public juce::Component
{
public:
    TransportComponent()
        : rewindButton(TransportButton::Rewind),
          ffButton(TransportButton::FastForward),
          stopButton(TransportButton::Stop),
          playButton(TransportButton::Play),
          pauseButton(TransportButton::Pause),
          recordButton(TransportButton::Record),
          settingsButton(TransportButton::Settings),
          saveButton(TransportButton::Save),
          sessionButton(TransportButton::Session)
    {
        // Logo label
        logoLabel.setText("VTAPE//24", juce::dontSendNotification);
        logoLabel.setJustificationType(juce::Justification::centredLeft);
        logoLabel.setFont(juce::Font(28.0f, juce::Font::bold).withExtraKerningFactor(0.15f));
        logoLabel.setColour(juce::Label::textColourId, juce::Colour(VintageLookAndFeel::metalDark));
        addAndMakeVisible(logoLabel);
        
        // Timecode display
        timecodeLabel.setText("00:00:00.000", juce::dontSendNotification);
        timecodeLabel.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 32.0f, juce::Font::bold));
        timecodeLabel.setJustificationType(juce::Justification::centred);
        timecodeLabel.setColour(juce::Label::textColourId, juce::Colour(VintageLookAndFeel::accentAmber));
        timecodeLabel.setColour(juce::Label::backgroundColourId, juce::Colour(0xff0a0a08));
        addAndMakeVisible(timecodeLabel);
        
        // BPM display
        bpmLabel.setText("120 BPM | 4/4", juce::dontSendNotification);
        bpmLabel.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 20.0f, juce::Font::bold));
        bpmLabel.setJustificationType(juce::Justification::centred);
        bpmLabel.setColour(juce::Label::textColourId, juce::Colour(VintageLookAndFeel::accentGreen));
        bpmLabel.setColour(juce::Label::backgroundColourId, juce::Colour(0xff0a0a08));
        bpmLabel.setEditable(true, true, false);  // Allow editing on double-click
        bpmLabel.onTextChange = [this] {
            // Parse BPM from text when edited
            juce::String text = bpmLabel.getText();
            // Extract first number as BPM
            int bpmValue = text.upToFirstOccurrenceOf(" ", false, false).getIntValue();
            if (bpmValue >= 20 && bpmValue <= 999)
            {
                if (onBPMChanged)
                    onBPMChanged(bpmValue);
            }
        };
        addAndMakeVisible(bpmLabel);
        
        // Bar.Beat display
        barBeatLabel.setText("1.1.01", juce::dontSendNotification);
        barBeatLabel.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 18.0f, juce::Font::bold));
        barBeatLabel.setJustificationType(juce::Justification::centred);
        barBeatLabel.setColour(juce::Label::textColourId, juce::Colour(VintageLookAndFeel::accentAmber).darker(0.3f));
        barBeatLabel.setColour(juce::Label::backgroundColourId, juce::Colour(0xff0a0a08));
        addAndMakeVisible(barBeatLabel);
        
        // Button callbacks
        // Rewind: hold to rewind, release to stop
        rewindButton.onMouseDown = [this] { if (onRewindStart) onRewindStart(); };
        rewindButton.onMouseUp   = [this] { if (onRewindStop)  onRewindStop(); };
        rewindButton.onClick = nullptr;  // handled via mouseDown/Up
        playButton.onClick = [this] { 
            isPlaying = !isPlaying;
            playButton.setPlaying(isPlaying);
            if (onPlay) onPlay(isPlaying); 
        };
        stopButton.onClick = [this] { 
            isPlaying = false;
            isRecording = false;
            isPaused = false;
            playButton.setPlaying(false);
            recordButton.setRecording(false);
            pauseButton.setPaused(false);
            if (onStop) onStop(); 
        };
        pauseButton.onClick = [this] {
            isPaused = !isPaused;
            pauseButton.setPaused(isPaused);
            if (isPaused) {
                isPlaying = false;
                playButton.setPlaying(false);
            }
            if (onPause) onPause(isPaused);
        };
        recordButton.onClick = [this] { 
            isRecording = !isRecording;
            if (isRecording) {
                isPlaying = true;
                isPaused = false;
                playButton.setPlaying(true);
                pauseButton.setPaused(false);
            }
            recordButton.setRecording(isRecording);
            if (onRecord) onRecord(isRecording); 
        };
        ffButton.onMouseDown = [this] { if (onFFStart) onFFStart(); };
        ffButton.onMouseUp   = [this] { if (onFFStop)  onFFStop();  };
        ffButton.onClick = nullptr;  // handled via mouseDown/Up
        settingsButton.onClick = [this] { if (onSettings) onSettings(); };
        saveButton.onClick     = [this] { if (onSave)     onSave();     };
        sessionButton.onClick  = [this] { if (onSession)  onSession();  };
        
        // CLICK metronome button
        clickButton.setButtonText("CLICK");
        clickButton.setClickingTogglesState(true);
        clickButton.setToggleState(false, juce::dontSendNotification);
        updateClickButtonColour();
        clickButton.onClick = [this] {
            clickActive = clickButton.getToggleState();
            updateClickButtonColour();
            if (onClickToggle) onClickToggle(clickActive);
        };
        addAndMakeVisible(clickButton);

        // LTC output button
        ltcButton.setButtonText("LTC IN");
        ltcButton.setClickingTogglesState(true);
        ltcButton.setToggleState(false, juce::dontSendNotification);
        updateLTCButtonColour();
        ltcButton.onClick = [this] {
            ltcActive = ltcButton.getToggleState();
            updateLTCButtonColour();
            if (onLTCToggle) onLTCToggle(ltcActive);
        };
        addAndMakeVisible(ltcButton);
        
        // Zoom controls
        zoomOutButton.setButtonText("-");
        zoomOutButton.onClick = [this] {
            if (onZoomChanged) onZoomChanged(-0.1);  // Decrease zoom by 0.1
        };
        addAndMakeVisible(zoomOutButton);
        
        zoomLabel.setText("1.0x", juce::dontSendNotification);
        zoomLabel.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::bold));
        zoomLabel.setJustificationType(juce::Justification::centred);
        zoomLabel.setColour(juce::Label::textColourId, juce::Colour(VintageLookAndFeel::accentAmber));
        zoomLabel.setColour(juce::Label::backgroundColourId, juce::Colour(0xff0a0a08));
        addAndMakeVisible(zoomLabel);
        
        zoomInButton.setButtonText("+");
        zoomInButton.onClick = [this] {
            if (onZoomChanged) onZoomChanged(+0.1);  // Increase zoom by 0.1
        };
        addAndMakeVisible(zoomInButton);
        
        // Amplitude zoom controls (vertical waveform size)
        ampZoomOutButton.setButtonText("▼");  // Down arrow for smaller waves
        ampZoomOutButton.onClick = [this] {
            if (onAmplitudeZoomChanged) onAmplitudeZoomChanged(-0.1);  // Decrease amplitude by 0.1
        };
        addAndMakeVisible(ampZoomOutButton);
        
        ampZoomLabel.setText("1.0x", juce::dontSendNotification);
        ampZoomLabel.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::bold));
        ampZoomLabel.setJustificationType(juce::Justification::centred);
        ampZoomLabel.setColour(juce::Label::textColourId, juce::Colour(VintageLookAndFeel::accentGreen));
        ampZoomLabel.setColour(juce::Label::backgroundColourId, juce::Colour(0xff0a0a08));
        addAndMakeVisible(ampZoomLabel);
        
        ampZoomInButton.setButtonText("▲");  // Up arrow for larger waves
        ampZoomInButton.onClick = [this] {
            if (onAmplitudeZoomChanged) onAmplitudeZoomChanged(+0.1);  // Increase amplitude by 0.1
        };
        addAndMakeVisible(ampZoomInButton);
        
        // Grid controls
        gridButton.setButtonText("GRID");
        gridButton.setClickingTogglesState(true);
        gridButton.setToggleState(false, juce::dontSendNotification);
        updateGridButtonColour();
        gridButton.onClick = [this] {
            gridActive = gridButton.getToggleState();
            updateGridButtonColour();
            if (onGridToggle) onGridToggle(gridActive);
        };
        addAndMakeVisible(gridButton);
        
        gridResButton.setButtonText("1/4");  // Default to beats
        gridResButton.onClick = [this] {
            if (onGridResolutionCycle) onGridResolutionCycle();
        };
        addAndMakeVisible(gridResButton);
        
        // Varispeed controls
        speedDownOctButton.setButtonText("-12");  // Down octave
        speedDownOctButton.onClick = [this] {
            if (onSpeedChangeSemitones) onSpeedChangeSemitones(-12);
        };
        addAndMakeVisible(speedDownOctButton);
        
        speedDownSemiButton.setButtonText("-1");  // Down semitone
        speedDownSemiButton.onClick = [this] {
            if (onSpeedChangeSemitones) onSpeedChangeSemitones(-1);
        };
        addAndMakeVisible(speedDownSemiButton);
        
        speedLabel.setText("1.0x", juce::dontSendNotification);
        speedLabel.setJustificationType(juce::Justification::centred);
        speedLabel.setColour(juce::Label::textColourId, juce::Colour(VintageLookAndFeel::accentAmber));
        addAndMakeVisible(speedLabel);
        
        speedUpSemiButton.setButtonText("+1");  // Up semitone
        speedUpSemiButton.onClick = [this] {
            if (onSpeedChangeSemitones) onSpeedChangeSemitones(+1);
        };
        addAndMakeVisible(speedUpSemiButton);
        
        speedUpOctButton.setButtonText("+12");  // Up octave
        speedUpOctButton.onClick = [this] {
            if (onSpeedChangeSemitones) onSpeedChangeSemitones(+12);
        };
        addAndMakeVisible(speedUpOctButton);
        
        speedResetButton.setButtonText("1x");  // Reset to normal
        speedResetButton.onClick = [this] {
            if (onSpeedReset) onSpeedReset();
        };
        speedResetButton.setTooltip("Reset playback speed to normal (1.0x)");
        addAndMakeVisible(speedResetButton);
        
        // Reverse playback button
        reverseButton.setButtonText("◄►");  // Reverse arrows symbol
        reverseButton.onClick = [this] {
            if (onReverseToggle) onReverseToggle();
        };
        reverseButton.setTooltip("Toggle reverse playback (plays tape backwards)");
        addAndMakeVisible(reverseButton);
        
        // Section control buttons
        sectionStartButton.setButtonText("◄ IN");  // Left marker
        sectionStartButton.onClick = [this] {
            if (onSectionSetStart) onSectionSetStart();
        };
        sectionStartButton.setTooltip("Mark section IN point at playhead");
        addAndMakeVisible(sectionStartButton);
        
        sectionEndButton.setButtonText("OUT ►");  // Right marker
        sectionEndButton.onClick = [this] {
            if (onSectionSetEnd) onSectionSetEnd();
        };
        sectionEndButton.setTooltip("Mark section OUT point at playhead");
        addAndMakeVisible(sectionEndButton);
        
        sectionLoopButton.setButtonText("↻ LOOP");  // Loop symbol
        sectionLoopButton.onClick = [this] {
            if (onSectionLoopToggle) onSectionLoopToggle();
        };
        sectionLoopButton.setTooltip("Loop between IN and OUT points");
        addAndMakeVisible(sectionLoopButton);
        
        sectionClearButton.setButtonText("✕ CLR");  // X symbol
        sectionClearButton.onClick = [this] {
            if (onSectionClear) onSectionClear();
        };
        sectionClearButton.setTooltip("Clear IN/OUT points");
        addAndMakeVisible(sectionClearButton);
        
        // Section position labels - make more prominent
        sectionStartLabel.setText("--:--", juce::dontSendNotification);
        sectionStartLabel.setJustificationType(juce::Justification::centred);
        sectionStartLabel.setColour(juce::Label::textColourId, juce::Colour(VintageLookAndFeel::accentAmber));
        sectionStartLabel.setColour(juce::Label::backgroundColourId, juce::Colour(0xff1a1512));
        sectionStartLabel.setFont(juce::Font(14.0f, juce::Font::bold));
        sectionStartLabel.setTooltip("Section IN point (bar:beat)");
        addAndMakeVisible(sectionStartLabel);
        
        sectionEndLabel.setText("--:--", juce::dontSendNotification);
        sectionEndLabel.setJustificationType(juce::Justification::centred);
        sectionEndLabel.setColour(juce::Label::textColourId, juce::Colour(VintageLookAndFeel::accentAmber));
        sectionEndLabel.setColour(juce::Label::backgroundColourId, juce::Colour(0xff1a1512));
        sectionEndLabel.setFont(juce::Font(14.0f, juce::Font::bold));
        sectionEndLabel.setTooltip("Section OUT point (bar:beat)");
        addAndMakeVisible(sectionEndLabel);
        
        // Group labels for better visual organization
        sectionGroupLabel.setText("SECTION", juce::dontSendNotification);
        sectionGroupLabel.setJustificationType(juce::Justification::centred);
        sectionGroupLabel.setColour(juce::Label::textColourId, juce::Colour(VintageLookAndFeel::metalLight));
        sectionGroupLabel.setFont(juce::Font(9.0f, juce::Font::bold));
        addAndMakeVisible(sectionGroupLabel);
        
        reverseGroupLabel.setText("REVERSE", juce::dontSendNotification);
        reverseGroupLabel.setJustificationType(juce::Justification::centred);
        reverseGroupLabel.setColour(juce::Label::textColourId, juce::Colour(VintageLookAndFeel::metalLight));
        reverseGroupLabel.setFont(juce::Font(9.0f, juce::Font::bold));
        addAndMakeVisible(reverseGroupLabel);
        
        // Add buttons in order: Rewind, FF, Stop, Play, Pause, Record
        addAndMakeVisible(rewindButton);
        addAndMakeVisible(ffButton);
        addAndMakeVisible(stopButton);
        addAndMakeVisible(playButton);
        addAndMakeVisible(pauseButton);
        addAndMakeVisible(recordButton);
        addAndMakeVisible(settingsButton);
        addAndMakeVisible(saveButton);
        addAndMakeVisible(sessionButton);
    }
    
    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds();
        
        // Top bar gradient
        juce::ColourGradient topGradient(
            juce::Colour(VintageLookAndFeel::panelCream), 0.0f, 0.0f,
            juce::Colour(VintageLookAndFeel::tapeBeige), 0.0f, (float)bounds.getHeight(), false);
        g.setGradientFill(topGradient);
        g.fillAll();
        
        // Bottom border
        g.setColour(juce::Colour(VintageLookAndFeel::metalDark));
        g.drawLine(0.0f, (float)bounds.getBottom() - 3.0f, 
                   (float)bounds.getRight(), (float)bounds.getBottom() - 3.0f, 3.0f);
        
        // Top highlight
        g.setColour(juce::Colours::white.withAlpha(0.3f));
        g.drawLine(0.0f, 1.0f, (float)bounds.getRight(), 1.0f, 1.0f);
        
        // Timecode background
        auto tcBounds = timecodeLabel.getBounds().toFloat().expanded(4, 2);
        g.setColour(juce::Colour(0xff0a0a08));
        g.fillRoundedRectangle(tcBounds, 4.0f);
        g.setColour(juce::Colour(VintageLookAndFeel::metalDark));
        g.drawRoundedRectangle(tcBounds, 4.0f, 2.0f);
        
        // Draw vertical separators between button groups for better visual organization
        auto separatorColor = juce::Colour(VintageLookAndFeel::metalDark).withAlpha(0.7f);
        auto separatorY1 = bounds.getY() + 12.0f;
        auto separatorY2 = bounds.getBottom() - 12.0f;
        
        // Separator before reverse button
        if (reverseButton.isVisible())
        {
            float x = reverseButton.getX() - 8.0f;
            g.setColour(separatorColor);
            g.drawLine(x, separatorY1, x, separatorY2, 2.0f);
        }
        
        // Separator before section controls
        if (sectionLoopButton.isVisible())
        {
            float x = sectionLoopButton.getX() - 8.0f;
            g.setColour(separatorColor);
            g.drawLine(x, separatorY1, x, separatorY2, 2.0f);
        }
    }
    
    void resized() override
    {
        auto bounds = getLocalBounds().reduced(16, 8);
        
        // Logo on left
        logoLabel.setBounds(bounds.removeFromLeft(180));
        
        // Settings, Session and Save buttons on right
        settingsButton.setBounds(bounds.removeFromRight(50).withSizeKeepingCentre(40, 40));
        bounds.removeFromRight(4);
        sessionButton.setBounds(bounds.removeFromRight(50).withSizeKeepingCentre(40, 40));
        bounds.removeFromRight(4);
        saveButton.setBounds(bounds.removeFromRight(50).withSizeKeepingCentre(40, 40));
        
        bounds.removeFromRight(20);
        
        // Transport buttons on right - organized in clear groups with labels
        auto transportArea = bounds.removeFromRight(1220);  // Extended for grouped section/reverse controls
        auto buttonW = 50;
        auto buttonH = 50;
        
        // Layout from right to left (reverse order)
        recordButton.setBounds(transportArea.removeFromRight(buttonW).withSizeKeepingCentre(buttonW, buttonH));
        transportArea.removeFromRight(8);
        pauseButton.setBounds(transportArea.removeFromRight(buttonW).withSizeKeepingCentre(buttonW, buttonH));
        transportArea.removeFromRight(8);
        playButton.setBounds(transportArea.removeFromRight(buttonW).withSizeKeepingCentre(buttonW, buttonH));
        transportArea.removeFromRight(8);
        stopButton.setBounds(transportArea.removeFromRight(buttonW).withSizeKeepingCentre(buttonW, buttonH));
        transportArea.removeFromRight(8);
        ffButton.setBounds(transportArea.removeFromRight(buttonW).withSizeKeepingCentre(buttonW, buttonH));
        transportArea.removeFromRight(8);
        rewindButton.setBounds(transportArea.removeFromRight(buttonW).withSizeKeepingCentre(buttonW, buttonH));
        transportArea.removeFromRight(12);
        // LTC button - blue highlight when active
        ltcButton.setBounds(transportArea.removeFromRight(38).withSizeKeepingCentre(38, 32));
        transportArea.removeFromRight(8);
        // Horizontal zoom controls
        zoomInButton.setBounds(transportArea.removeFromRight(24).withSizeKeepingCentre(24, 24));
        transportArea.removeFromRight(2);
        zoomLabel.setBounds(transportArea.removeFromRight(38).withSizeKeepingCentre(38, 24));
        transportArea.removeFromRight(2);
        zoomOutButton.setBounds(transportArea.removeFromRight(24).withSizeKeepingCentre(24, 24));
        transportArea.removeFromRight(8);
        // Amplitude zoom controls (vertical waveform size)
        ampZoomInButton.setBounds(transportArea.removeFromRight(24).withSizeKeepingCentre(24, 24));
        transportArea.removeFromRight(2);
        ampZoomLabel.setBounds(transportArea.removeFromRight(38).withSizeKeepingCentre(38, 24));
        transportArea.removeFromRight(2);
        ampZoomOutButton.setBounds(transportArea.removeFromRight(24).withSizeKeepingCentre(24, 24));
        transportArea.removeFromRight(8);
        // CLICK button - amber highlight when active
        clickButton.setBounds(transportArea.removeFromRight(44).withSizeKeepingCentre(44, 32));
        transportArea.removeFromRight(8);
        // Grid controls - green when active
        gridResButton.setBounds(transportArea.removeFromRight(38).withSizeKeepingCentre(38, 28));
        transportArea.removeFromRight(2);
        gridButton.setBounds(transportArea.removeFromRight(40).withSizeKeepingCentre(40, 32));
        transportArea.removeFromRight(8);
        // Varispeed controls - amber label like horizontal zoom
        speedResetButton.setBounds(transportArea.removeFromRight(26).withSizeKeepingCentre(26, 26));
        transportArea.removeFromRight(2);
        speedUpOctButton.setBounds(transportArea.removeFromRight(28).withSizeKeepingCentre(28, 24));
        transportArea.removeFromRight(2);
        speedUpSemiButton.setBounds(transportArea.removeFromRight(24).withSizeKeepingCentre(24, 24));
        transportArea.removeFromRight(2);
        speedLabel.setBounds(transportArea.removeFromRight(44).withSizeKeepingCentre(44, 24));
        transportArea.removeFromRight(2);
        speedDownSemiButton.setBounds(transportArea.removeFromRight(24).withSizeKeepingCentre(24, 24));
        transportArea.removeFromRight(2);
        speedDownOctButton.setBounds(transportArea.removeFromRight(28).withSizeKeepingCentre(28, 24));
        transportArea.removeFromRight(16);  // Larger gap before reverse
        
        // Reverse section with label
        auto reverseArea = transportArea.removeFromRight(70);
        reverseGroupLabel.setBounds(reverseArea.removeFromTop(14));
        reverseButton.setBounds(reverseArea.withSizeKeepingCentre(70, 40));
        transportArea.removeFromRight(16);  // Gap before section controls
        
        // Section controls grouped together with label
        auto sectionArea = transportArea.removeFromRight(320);
        sectionGroupLabel.setBounds(sectionArea.removeFromTop(14));
        
        // Create a sub-area for the controls
        auto sectionControlArea = sectionArea.withTrimmedTop(2);
        
        // Layout: [label] [IN] | [label] [OUT] || [LOOP] [CLEAR]
        // Right side: CLEAR and LOOP buttons
        sectionClearButton.setBounds(sectionControlArea.removeFromRight(55).withSizeKeepingCentre(55, 36));
        sectionControlArea.removeFromRight(4);
        sectionLoopButton.setBounds(sectionControlArea.removeFromRight(55).withSizeKeepingCentre(55, 36));
        sectionControlArea.removeFromRight(10);  // Larger gap
        
        // OUT point with label
        sectionEndButton.setBounds(sectionControlArea.removeFromRight(45).withSizeKeepingCentre(45, 36));
        sectionControlArea.removeFromRight(3);
        auto endLabelArea = sectionControlArea.removeFromRight(50);
        sectionEndLabel.setBounds(endLabelArea.removeFromTop(endLabelArea.getHeight() / 2 + 2));
        sectionControlArea.removeFromRight(8);
        
        // IN point with label
        sectionStartButton.setBounds(sectionControlArea.removeFromRight(45).withSizeKeepingCentre(45, 36));
        sectionControlArea.removeFromRight(3);
        auto startLabelArea = sectionControlArea.removeFromRight(50);
        sectionStartLabel.setBounds(startLabelArea.removeFromTop(startLabelArea.getHeight() / 2 + 2));
        
        // Center area with better formatting
        auto centerArea = bounds.reduced(16, 8);
        
        // Top row: BPM/Time Sig (left) and Bar.Beat (right)
        auto topRow = centerArea.removeFromTop(22);
        auto leftInfo = topRow.removeFromLeft(topRow.getWidth() / 2 - 10);
        topRow.removeFromLeft(20);  // Gap in middle
        auto rightInfo = topRow;
        
        // BPM display (left)
        bpmLabel.setBounds(leftInfo);
        
        // Bar.Beat display (right)
        barBeatLabel.setBounds(rightInfo);
        
        centerArea.removeFromTop(6);  // Gap between rows
        
        // Bottom row: Timecode (centered, larger)
        timecodeLabel.setBounds(centerArea);
    }
    
    void setTimecode(const juce::String& timecode)
    {
        timecodeLabel.setText(timecode, juce::dontSendNotification);
    }
    
    void setBPM(double bpm, int numerator, int denominator)
    {
        juce::String bpmText = juce::String(bpm, 1) + " BPM | " + 
                               juce::String(numerator) + "/" + juce::String(denominator);
        
        // Only update if text has changed (prevents flickering and editing interference)
        if (bpmLabel.getText() != bpmText)
        {
            bpmLabel.setText(bpmText, juce::dontSendNotification);
        }
    }
    
    void setBarBeat(int bar, int beat, int tick)
    {
        juce::String barBeatText = juce::String(bar) + "." + 
                                   juce::String(beat) + "." + 
                                   juce::String(tick).paddedLeft('0', 2);
        barBeatLabel.setText(barBeatText, juce::dontSendNotification);
    }
    
    void setPlaying(bool playing)
    {
        isPlaying = playing;
        playButton.setPlaying(playing);
        updateAllButtonStates();
    }
    
    void setRecording(bool recording)
    {
        isRecording = recording;
        recordButton.setRecording(recording);
        updateAllButtonStates();
    }
    
    void setPaused(bool paused)
    {
        isPaused = paused;
        pauseButton.setPaused(paused);
        updateAllButtonStates();
    }
    
    void updateTransportState(bool playing, bool recording, bool paused)
    {
        isPlaying = playing;
        isRecording = recording;
        isPaused = paused;
        updateAllButtonStates();
    }
    
    void updateAllButtonStates()
    {
        playButton.setPlaying(isPlaying);
        recordButton.setRecording(isRecording);
        pauseButton.setPaused(isPaused);
        stopButton.setTransportState(isPlaying, isRecording, isPaused);  // Update with all states
    }
    
    std::function<void()> onRewindStart;  // mouseDown: begin rewind
    std::function<void()> onRewindStop;   // mouseUp:   stop rewind
    std::function<void(bool)> onPlay;
    std::function<void()> onStop;
    std::function<void(bool)> onPause;
    std::function<void(bool)> onRecord;
    std::function<void()> onFFStart;      // mouseDown: begin fast-forward
    std::function<void()> onFFStop;       // mouseUp:   stop fast-forward
    std::function<void()> onSettings;
    std::function<void()> onSave;
    std::function<void()> onSession;
    std::function<void(double)> onBPMChanged;
    std::function<void(bool)> onClickToggle;  // CLICK metronome toggled
    std::function<void(bool)> onLTCToggle;    // LTC output toggled
    std::function<void(double)> onZoomChanged;  // Zoom level changed
    std::function<void(double)> onAmplitudeZoomChanged;  // Amplitude zoom level changed
    std::function<void(bool)> onGridToggle;     // Grid on/off toggled
    std::function<void()> onGridResolutionCycle;  // Grid resolution cycle button pressed
    std::function<void(int)> onSpeedChangeSemitones;  // Speed change by semitones (+1, -1, +12, -12)
    std::function<void()> onSpeedReset;  // Reset speed to 1.0x
    std::function<void()> onReverseToggle;  // Toggle reverse playback
    std::function<void()> onSectionSetStart;  // Set section start to playhead
    std::function<void()> onSectionSetEnd;    // Set section end to playhead
    std::function<void()> onSectionClear;     // Clear section
    std::function<void()> onSectionLoopToggle;  // Toggle section loop

    // Sync CLICK button visual state (call at startup)
    void setClickEnabled(bool enabled)
    {
        clickActive = enabled;
        clickButton.setToggleState(enabled, juce::dontSendNotification);
        updateClickButtonColour();
    }

    // Sync LTC output button visual state (call at startup and in timer)
    void setLTCEnabled(bool enabled)
    {
        ltcActive = enabled;
        ltcButton.setToggleState(enabled, juce::dontSendNotification);
        updateLTCButtonColour();
    }
    
    void setZoom(double zoom)
    {
        zoomLabel.setText(juce::String(zoom, 1) + "x", juce::dontSendNotification);
    }
    
    void setAmplitudeZoom(double zoom)
    {
        ampZoomLabel.setText(juce::String(zoom, 1) + "x", juce::dontSendNotification);
    }
    
    void setGridEnabled(bool enabled)
    {
        gridActive = enabled;
        gridButton.setToggleState(enabled, juce::dontSendNotification);
        updateGridButtonColour();
    }
    
    void setGridResolution(int resolution)
    {
        const char* labels[] = { "BARS", "1/4", "1/8", "1/16", "1/32" };
        if (resolution >= 0 && resolution <= 4)
            gridResButton.setButtonText(labels[resolution]);
    }
    
    void setSpeed(double speed)
    {
        speedLabel.setText(juce::String(speed, 2) + "x", juce::dontSendNotification);
    }
    
    void setReverseActive(bool active)
    {
        reverseActive = active;
        updateReverseButtonColour();
    }
    
    void setSectionLoopActive(bool active)
    {
        sectionLoopActive = active;
        updateSectionLoopButtonColour();
    }
    
    void setSectionPositions(double startTime, double endTime, double bpm)
    {
        // Convert seconds to bar:beat format (using colon for better readability)
        if (startTime >= 0.0 && bpm > 0.0)
        {
            double beatsFromStart = startTime * (bpm / 60.0);
            int bar = static_cast<int>(beatsFromStart / 4.0) + 1;  // 4/4 time, 1-based
            int beat = static_cast<int>(beatsFromStart) % 4 + 1;   // 1-based
            sectionStartLabel.setText(juce::String(bar) + ":" + juce::String(beat), juce::dontSendNotification);
        }
        else
        {
            sectionStartLabel.setText("--:--", juce::dontSendNotification);
        }
        
        if (endTime >= 0.0 && bpm > 0.0)
        {
            double beatsFromStart = endTime * (bpm / 60.0);
            int bar = static_cast<int>(beatsFromStart / 4.0) + 1;
            int beat = static_cast<int>(beatsFromStart) % 4 + 1;
            sectionEndLabel.setText(juce::String(bar) + ":" + juce::String(beat), juce::dontSendNotification);
        }
        else
        {
            sectionEndLabel.setText("--:--", juce::dontSendNotification);
        }
    }

private:
    juce::Label logoLabel;
    juce::Label timecodeLabel;
    juce::Label bpmLabel;  // BPM display
    juce::Label barBeatLabel;  // Bar.Beat display
    TransportButton rewindButton;
    TransportButton ffButton;
    TransportButton stopButton;
    TransportButton playButton;
    TransportButton pauseButton;
    TransportButton recordButton;
    TransportButton settingsButton;
    TransportButton saveButton;
    TransportButton sessionButton;
    juce::TextButton clickButton;  // CLICK metronome toggle
    juce::TextButton ltcButton;    // LTC output toggle
    juce::TextButton zoomOutButton;  // Zoom out (-)
    juce::TextButton zoomInButton;   // Zoom in (+)
    juce::Label zoomLabel;           // Shows current zoom level
    juce::TextButton ampZoomOutButton;  // Amplitude zoom out (-)
    juce::TextButton ampZoomInButton;   // Amplitude zoom in (+)
    juce::Label ampZoomLabel;           // Shows current amplitude zoom level
    juce::TextButton gridButton;        // Toggle grid on/off
    juce::TextButton gridResButton;     // Cycle through grid resolutions
    juce::TextButton speedDownOctButton;   // Speed down by octave
    juce::TextButton speedDownSemiButton;  // Speed down by semitone
    juce::Label speedLabel;                // Shows current speed (e.g., "1.0x", "2.0x")
    juce::TextButton speedUpSemiButton;    // Speed up by semitone
    juce::TextButton speedUpOctButton;     // Speed up by octave
    juce::TextButton speedResetButton;     // Reset to 1.0x
    
    // Reverse and section controls
    juce::TextButton reverseButton;        // Toggle reverse playback
    juce::TextButton sectionStartButton;   // Set section start
    juce::TextButton sectionEndButton;     // Set section end
    juce::TextButton sectionClearButton;   // Clear section
    juce::TextButton sectionLoopButton;    // Toggle section loop
    juce::Label sectionStartLabel;         // Display section start in bar.beat
    juce::Label sectionEndLabel;           // Display section end in bar.beat
    juce::Label sectionGroupLabel;         // "SECTION" label above buttons
    juce::Label reverseGroupLabel;         // "REVERSE" label above button
    
    bool clickActive = false;
    bool ltcActive   = false;
    bool gridActive  = false;
    bool reverseActive = false;
    bool sectionLoopActive = false;
    
    void updateClickButtonColour()
    {
        if (clickActive)
        {
            clickButton.setColour(juce::TextButton::buttonColourId, juce::Colour(VintageLookAndFeel::accentAmber));
            clickButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(VintageLookAndFeel::accentAmber));
            clickButton.setColour(juce::TextButton::textColourOnId,  juce::Colour(0xff1a1a14));
            clickButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff1a1a14));
        }
        else
        {
            clickButton.setColour(juce::TextButton::buttonColourId,  juce::Colour(0xff4a4a3a));
            clickButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff4a4a3a));
            clickButton.setColour(juce::TextButton::textColourOnId,  juce::Colour(0xff888877));
            clickButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff888877));
        }
        clickButton.repaint();
    }

    void updateLTCButtonColour()
    {
        if (ltcActive)
        {
            // LTC blue when active
            ltcButton.setColour(juce::TextButton::buttonColourId,  juce::Colour(0xff3366cc));
            ltcButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff3366cc));
            ltcButton.setColour(juce::TextButton::textColourOnId,  juce::Colours::white);
            ltcButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        }
        else
        {
            ltcButton.setColour(juce::TextButton::buttonColourId,  juce::Colour(0xff3a3a4a));
            ltcButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff3a3a4a));
            ltcButton.setColour(juce::TextButton::textColourOnId,  juce::Colour(0xff7788aa));
            ltcButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff7788aa));
        }
        ltcButton.repaint();
    }
    
    void updateGridButtonColour()
    {
        if (gridActive)
        {
            // Grid green when active
            gridButton.setColour(juce::TextButton::buttonColourId,  juce::Colour(VintageLookAndFeel::accentGreen));
            gridButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(VintageLookAndFeel::accentGreen));
            gridButton.setColour(juce::TextButton::textColourOnId,  juce::Colour(0xff1a1a14));
            gridButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff1a1a14));
        }
        else
        {
            gridButton.setColour(juce::TextButton::buttonColourId,  juce::Colour(0xff3a4a3a));
            gridButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff3a4a3a));
            gridButton.setColour(juce::TextButton::textColourOnId,  juce::Colour(0xff778877));
            gridButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff778877));
        }
        gridButton.repaint();
    }
    
    void updateReverseButtonColour()
    {
        if (reverseActive)
        {
            // Bright red when active with high contrast
            reverseButton.setColour(juce::TextButton::buttonColourId,  juce::Colour(0xffcc3333));
            reverseButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffcc3333));
            reverseButton.setColour(juce::TextButton::textColourOnId,  juce::Colours::white);
            reverseButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        }
        else
        {
            reverseButton.setColour(juce::TextButton::buttonColourId,  juce::Colour(0xff3a3a3a));
            reverseButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff3a3a3a));
            reverseButton.setColour(juce::TextButton::textColourOnId,  juce::Colour(0xff888888));
            reverseButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff888888));
        }
        reverseButton.repaint();
    }
    
    void updateSectionLoopButtonColour()
    {
        if (sectionLoopActive)
        {
            // Bright amber when active with high contrast
            sectionLoopButton.setColour(juce::TextButton::buttonColourId,  juce::Colour(0xffffaa00));
            sectionLoopButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffffaa00));
            sectionLoopButton.setColour(juce::TextButton::textColourOnId,  juce::Colour(0xff1a1a14));
            sectionLoopButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff1a1a14));
        }
        else
        {
            sectionLoopButton.setColour(juce::TextButton::buttonColourId,  juce::Colour(0xff3a3a3a));
            sectionLoopButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff3a3a3a));
            sectionLoopButton.setColour(juce::TextButton::textColourOnId,  juce::Colour(0xff888888));
            sectionLoopButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff888888));
        }
        sectionLoopButton.repaint();
    }
    
    bool isPlaying = false;
    bool isRecording = false;
    bool isPaused = false;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TransportComponent)
};
