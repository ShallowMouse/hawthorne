#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "VintageLookAndFeel.h"
#include "Session.h"
#include "Settings.h"

//==============================================================================
// New Session Dialog - Configure session settings
//==============================================================================
class NewSessionDialog : public juce::Component
{
public:
    NewSessionDialog()
    {
        setSize(600, 750);  // Increased to 750 to guarantee buttons show
        
        // Title
        titleLabel.setText("Create New Session", juce::dontSendNotification);
        titleLabel.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 28.0f, juce::Font::bold));
        titleLabel.setColour(juce::Label::textColourId, juce::Colour(0xfff8f0e0));
        titleLabel.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(titleLabel);
        
        // Session Name
        sessionNameLabel.setText("Session Name:", juce::dontSendNotification);
        sessionNameLabel.setFont(juce::Font(14.0f, juce::Font::bold));
        sessionNameLabel.setColour(juce::Label::textColourId, juce::Colour(0xfff8f0e0));
        addAndMakeVisible(sessionNameLabel);
        
        sessionNameEditor.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 14.0f, juce::Font::bold));
        sessionNameEditor.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xfff8f0e0));
        sessionNameEditor.setColour(juce::TextEditor::textColourId, juce::Colour(0xff2a2520));
        sessionNameEditor.setColour(juce::TextEditor::outlineColourId, juce::Colour(VintageLookAndFeel::metalDark));
        sessionNameEditor.setText("Untitled Session");
        addAndMakeVisible(sessionNameEditor);
        
        // Sample Rate
        sampleRateLabel.setText("Sample Rate:", juce::dontSendNotification);
        sampleRateLabel.setFont(juce::Font(14.0f, juce::Font::bold));
        sampleRateLabel.setColour(juce::Label::textColourId, juce::Colour(0xfff8f0e0));
        addAndMakeVisible(sampleRateLabel);
        
        sampleRateCombo.addItem("44100 Hz (CD Quality)", 1);
        sampleRateCombo.addItem("48000 Hz (Professional)", 2);
        sampleRateCombo.addItem("88200 Hz (High Quality)", 3);
        sampleRateCombo.addItem("96000 Hz (Studio)", 4);
        sampleRateCombo.setSelectedId(2);  // Default to 48000 Hz
        sampleRateCombo.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xfff8f0e0));
        sampleRateCombo.setColour(juce::ComboBox::textColourId, juce::Colour(0xff2a2520));
        sampleRateCombo.setColour(juce::ComboBox::outlineColourId, juce::Colour(VintageLookAndFeel::metalDark));
        addAndMakeVisible(sampleRateCombo);
        
        // Bit Depth
        bitDepthLabel.setText("Bit Depth:", juce::dontSendNotification);
        bitDepthLabel.setFont(juce::Font(14.0f, juce::Font::bold));
        bitDepthLabel.setColour(juce::Label::textColourId, juce::Colour(0xfff8f0e0));
        addAndMakeVisible(bitDepthLabel);
        
        bitDepthCombo.addItem("16-bit (CD Quality)", 1);
        bitDepthCombo.addItem("24-bit (Professional)", 2);
        bitDepthCombo.addItem("32-bit (Maximum Quality)", 3);
        bitDepthCombo.setSelectedId(2);  // Default to 24-bit
        bitDepthCombo.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xfff8f0e0));
        bitDepthCombo.setColour(juce::ComboBox::textColourId, juce::Colour(0xff2a2520));
        bitDepthCombo.setColour(juce::ComboBox::outlineColourId, juce::Colour(VintageLookAndFeel::metalDark));
        addAndMakeVisible(bitDepthCombo);
        
        // Buffer Size
        bufferSizeLabel.setText("Buffer Size:", juce::dontSendNotification);
        bufferSizeLabel.setFont(juce::Font(14.0f, juce::Font::bold));
        bufferSizeLabel.setColour(juce::Label::textColourId, juce::Colour(0xfff8f0e0));
        addAndMakeVisible(bufferSizeLabel);
        
        bufferSizeCombo.addItem("128 samples (Low Latency)", 1);
        bufferSizeCombo.addItem("256 samples (Balanced)", 2);
        bufferSizeCombo.addItem("512 samples (Recommended)", 3);
        bufferSizeCombo.addItem("1024 samples (Stable)", 4);
        bufferSizeCombo.addItem("2048 samples (Maximum Stability)", 5);
        bufferSizeCombo.setSelectedId(3);  // Default to 512
        bufferSizeCombo.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xfff8f0e0));
        bufferSizeCombo.setColour(juce::ComboBox::textColourId, juce::Colour(0xff2a2520));
        bufferSizeCombo.setColour(juce::ComboBox::outlineColourId, juce::Colour(VintageLookAndFeel::metalDark));
        addAndMakeVisible(bufferSizeCombo);
        
        // Track Count
        trackCountLabel.setText("Track Count:", juce::dontSendNotification);
        trackCountLabel.setFont(juce::Font(14.0f, juce::Font::bold));
        trackCountLabel.setColour(juce::Label::textColourId, juce::Colour(0xfff8f0e0));
        addAndMakeVisible(trackCountLabel);
        
        trackCountCombo.addItem("8 Tracks", 1);
        trackCountCombo.addItem("16 Tracks (Recommended)", 2);
        trackCountCombo.addItem("24 Tracks", 3);
        trackCountCombo.addItem("32 Tracks", 4);
        trackCountCombo.setSelectedId(2);  // Default to 16
        trackCountCombo.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xfff8f0e0));
        trackCountCombo.setColour(juce::ComboBox::textColourId, juce::Colour(0xff2a2520));
        trackCountCombo.setColour(juce::ComboBox::outlineColourId, juce::Colour(VintageLookAndFeel::metalDark));
        addAndMakeVisible(trackCountCombo);
        
        // Stereo Return checkbox
        stereoReturnToggle.setButtonText("Add Stereo Return Track (for mixdowns)");
        stereoReturnToggle.setToggleState(false, juce::dontSendNotification);
        stereoReturnToggle.setColour(juce::ToggleButton::textColourId, juce::Colour(0xfff8f0e0));
        stereoReturnToggle.setColour(juce::ToggleButton::tickColourId, juce::Colour(VintageLookAndFeel::accentRed));
        addAndMakeVisible(stereoReturnToggle);
        
        // Notes
        notesLabel.setText("Session Notes:", juce::dontSendNotification);
        notesLabel.setFont(juce::Font(14.0f, juce::Font::bold));
        notesLabel.setColour(juce::Label::textColourId, juce::Colour(0xfff8f0e0));
        addAndMakeVisible(notesLabel);
        
        notesEditor.setMultiLine(true);
        notesEditor.setReturnKeyStartsNewLine(true);
        notesEditor.setScrollbarsShown(true);
        notesEditor.setFont(juce::Font(12.0f));
        notesEditor.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xfff8f0e0));
        notesEditor.setColour(juce::TextEditor::textColourId, juce::Colour(0xff2a2520));
        notesEditor.setColour(juce::TextEditor::outlineColourId, juce::Colour(VintageLookAndFeel::metalDark));
        notesEditor.setText("Enter any notes about this session...");
        addAndMakeVisible(notesEditor);
        
        // Create Button
        createButton.setButtonText("Create Session");
        createButton.onClick = [this] { 
            if (validateAndCreate())
            {
                closeDialog();
            }
        };
        addAndMakeVisible(createButton);
        
        // Cancel Button
        cancelButton.setButtonText("Cancel");
        cancelButton.onClick = [this] {
            cancelled = true;
            closeDialog();
        };
        addAndMakeVisible(cancelButton);
    }
    
    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds();
        
        // Dark vintage background
        juce::ColourGradient bgGradient(
            juce::Colour(VintageLookAndFeel::bgTrack),
            juce::Point<float>(0.0f, 0.0f),
            juce::Colour(0xff0a0a08),
            juce::Point<float>(0.0f, (float)bounds.getHeight()),
            false);
        g.setGradientFill(bgGradient);
        g.fillAll();
        
        // Outer border
        g.setColour(juce::Colour(VintageLookAndFeel::metalDark));
        g.drawRect(bounds, 3);
        
        // Inner highlight
        g.setColour(juce::Colours::white.withAlpha(0.05f));
        g.drawRect(bounds.reduced(3), 1);
        
        // Header panel
        auto headerBounds = bounds.removeFromTop(80);
        juce::ColourGradient headerGradient(
            juce::Colour(VintageLookAndFeel::tapeBeige),
            juce::Point<float>(0.0f, 0.0f),
            juce::Colour(0xffc4b49a),
            juce::Point<float>(0.0f, (float)headerBounds.getHeight()),
            false);
        g.setGradientFill(headerGradient);
        g.fillRect(headerBounds);
        
        // Header border
        g.setColour(juce::Colour(VintageLookAndFeel::metalDark));
        g.drawLine((float)headerBounds.getX(), (float)headerBounds.getBottom(),
                  (float)headerBounds.getRight(), (float)headerBounds.getBottom(), 2.0f);
    }
    
    void resized() override
    {
        auto bounds = getLocalBounds().reduced(20, 20);
        
        // Header
        auto header = bounds.removeFromTop(80);
        titleLabel.setBounds(header);
        
        bounds.removeFromTop(20);  // Spacing
        
        // Session Name
        sessionNameLabel.setBounds(bounds.removeFromTop(25));
        sessionNameEditor.setBounds(bounds.removeFromTop(30));
        bounds.removeFromTop(12);  // Reduced spacing
        
        // Sample Rate
        sampleRateLabel.setBounds(bounds.removeFromTop(25));
        sampleRateCombo.setBounds(bounds.removeFromTop(30));
        bounds.removeFromTop(12);  // Reduced spacing
        
        // Bit Depth
        bitDepthLabel.setBounds(bounds.removeFromTop(25));
        bitDepthCombo.setBounds(bounds.removeFromTop(30));
        bounds.removeFromTop(12);  // Reduced spacing
        
        // Buffer Size
        bufferSizeLabel.setBounds(bounds.removeFromTop(25));
        bufferSizeCombo.setBounds(bounds.removeFromTop(30));
        bounds.removeFromTop(12);  // Reduced spacing
        
        // Track Count
        trackCountLabel.setBounds(bounds.removeFromTop(25));
        trackCountCombo.setBounds(bounds.removeFromTop(30));
        bounds.removeFromTop(8);  // Reduced spacing
        
        // Stereo Return toggle
        stereoReturnToggle.setBounds(bounds.removeFromTop(25));
        bounds.removeFromTop(12);  // Reduced spacing
        
        // Notes
        notesLabel.setBounds(bounds.removeFromTop(25));
        notesEditor.setBounds(bounds.removeFromTop(80));  // Reduced from 100 to 80
        bounds.removeFromTop(12);  // Reduced spacing
        
        // Buttons - always ensure they're visible even if space is tight
        if (bounds.getHeight() < 40)
        {
            // If we don't have enough space, position buttons at absolute bottom
            auto buttonRow = getLocalBounds().removeFromBottom(50).reduced(20, 5);
            int totalButtonWidth = 270;
            int leftMargin = (buttonRow.getWidth() - totalButtonWidth) / 2;
            buttonRow.removeFromLeft(leftMargin);
            cancelButton.setBounds(buttonRow.removeFromLeft(130));
            buttonRow.removeFromLeft(10);
            createButton.setBounds(buttonRow.removeFromLeft(130));
        }
        else
        {
            // Normal positioning
            auto buttonRow = bounds.removeFromTop(40);
            int totalButtonWidth = 270;  // 130 + 10 + 130
            int leftMargin = (buttonRow.getWidth() - totalButtonWidth) / 2;
            buttonRow.removeFromLeft(leftMargin);
            cancelButton.setBounds(buttonRow.removeFromLeft(130));
            buttonRow.removeFromLeft(10);
            createButton.setBounds(buttonRow.removeFromLeft(130));
        }
    }
    
    // Get the configured session
    std::unique_ptr<Session> createSession(const juce::File& projectFolder)
    {
        if (cancelled)
            return nullptr;
        
        auto session = std::make_unique<Session>();
        
        session->setSessionName(sessionNameEditor.getText());
        session->setNotes(notesEditor.getText());
        session->setSampleRate(getSampleRateValue());
        session->setBitDepth(getBitDepthValue());
        session->setBufferSize(getBufferSizeValue());
        session->setTrackCount(getTrackCountValue());
        session->setHasStereoReturn(stereoReturnToggle.getToggleState());
        
        // Copy global audio defaults from Settings
        auto settings = std::make_unique<Settings>();
        auto audioSettings = settings->getAudioSettings();
        
        // Set audio device from global settings
        session->setAudioDevice(audioSettings.audioDeviceName);
        
        // Copy global input routing defaults
        std::map<int, int> inputRouting;
        for (const auto& [track, input] : audioSettings.trackInputMapping)
        {
            inputRouting[track] = input;
        }
        session->setInputRouting(inputRouting);
        
        // Copy current global output routing into the new session (preserves multi-output)
        session->setOutputRouting(audioSettings.trackOutputMapping);
        
        if (!session->createSessionStructure(projectFolder, sessionNameEditor.getText()))
        {
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::WarningIcon,
                "Error",
                "Could not create session folder structure.");
            return nullptr;
        }
        
        return session;
    }
    
    bool wasCancelled() const { return cancelled; }
    
    std::function<void()> onDialogClosed;

private:
    bool validateAndCreate()
    {
        // Validate session name
        juce::String name = sessionNameEditor.getText().trim();
        if (name.isEmpty())
        {
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::WarningIcon,
                "Invalid Name",
                "Please enter a session name.");
            return false;
        }
        
        // Check for invalid characters
        if (name.containsAnyOf("\\/:*?\"<>|"))
        {
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::WarningIcon,
                "Invalid Characters",
                "Session name cannot contain: \\ / : * ? \" < > |");
            return false;
        }
        
        return true;
    }
    
    void closeDialog()
    {
        if (onDialogClosed)
            onDialogClosed();
    }
    
    int getSampleRateValue() const
    {
        switch (sampleRateCombo.getSelectedId())
        {
            case 1: return 44100;
            case 2: return 48000;
            case 3: return 88200;
            case 4: return 96000;
            default: return 48000;
        }
    }
    
    int getBitDepthValue() const
    {
        switch (bitDepthCombo.getSelectedId())
        {
            case 1: return 16;
            case 2: return 24;
            case 3: return 32;
            default: return 24;
        }
    }
    
    int getBufferSizeValue() const
    {
        switch (bufferSizeCombo.getSelectedId())
        {
            case 1: return 128;
            case 2: return 256;
            case 3: return 512;
            case 4: return 1024;
            case 5: return 2048;
            default: return 512;
        }
    }
    
    int getTrackCountValue() const
    {
        switch (trackCountCombo.getSelectedId())
        {
            case 1: return 8;
            case 2: return 16;
            case 3: return 24;
            case 4: return 32;
            default: return 16;
        }
    }
    
    // UI Components
    juce::Label titleLabel;
    juce::Label sessionNameLabel;
    juce::TextEditor sessionNameEditor;
    juce::Label sampleRateLabel;
    juce::ComboBox sampleRateCombo;
    juce::Label bitDepthLabel;
    juce::ComboBox bitDepthCombo;
    juce::Label bufferSizeLabel;
    juce::ComboBox bufferSizeCombo;
    juce::Label trackCountLabel;
    juce::ComboBox trackCountCombo;
    juce::ToggleButton stereoReturnToggle;
    juce::Label notesLabel;
    juce::TextEditor notesEditor;
    juce::TextButton createButton;
    juce::TextButton cancelButton;
    
    bool cancelled = false;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NewSessionDialog)
};
