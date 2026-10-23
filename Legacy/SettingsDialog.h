#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include "VintageLookAndFeel.h"
#include "Settings.h"
#include "AudioEngine.h"
#include "Track.h"

//==============================================================================
// Audio Settings Tab
//==============================================================================
class AudioSettingsTab : public juce::Component
{
public:
    AudioSettingsTab(Settings* settingsPtr, juce::AudioDeviceManager* deviceManager)
        : settings(settingsPtr), audioDeviceManager(deviceManager)
    {
        // ---- Device ----
        deviceLabel.setText("ASIO Device:", juce::dontSendNotification);
        deviceLabel.setFont(juce::Font(14.0f, juce::Font::bold));
        deviceLabel.setColour(juce::Label::textColourId, juce::Colour(VintageLookAndFeel::metalDark));
        addAndMakeVisible(deviceLabel);
        addAndMakeVisible(deviceCombo);

        // ---- Sample rate ----
        sampleRateLabel.setText("Sample Rate:", juce::dontSendNotification);
        sampleRateLabel.setFont(juce::Font(14.0f, juce::Font::bold));
        sampleRateLabel.setColour(juce::Label::textColourId, juce::Colour(VintageLookAndFeel::metalDark));
        addAndMakeVisible(sampleRateLabel);
        sampleRateCombo.addItem("44100 Hz", 1);
        sampleRateCombo.addItem("48000 Hz", 2);
        sampleRateCombo.addItem("88200 Hz", 3);
        sampleRateCombo.addItem("96000 Hz", 4);
        sampleRateCombo.setSelectedId(2);
        addAndMakeVisible(sampleRateCombo);

        // ---- Buffer size ----
        bufferSizeLabel.setText("Buffer Size:", juce::dontSendNotification);
        bufferSizeLabel.setFont(juce::Font(14.0f, juce::Font::bold));
        bufferSizeLabel.setColour(juce::Label::textColourId, juce::Colour(VintageLookAndFeel::metalDark));
        addAndMakeVisible(bufferSizeLabel);
        bufferSizeCombo.addItem("64 samples",   1);
        bufferSizeCombo.addItem("128 samples",  2);
        bufferSizeCombo.addItem("256 samples",  3);
        bufferSizeCombo.addItem("512 samples",  4);
        bufferSizeCombo.addItem("1024 samples", 5);
        bufferSizeCombo.setSelectedId(4);
        addAndMakeVisible(bufferSizeCombo);
        
        // ---- Waveform scroll direction ----
        waveformScrollLabel.setText("Waveform Scroll Direction:", juce::dontSendNotification);
        waveformScrollLabel.setFont(juce::Font(14.0f, juce::Font::bold));
        waveformScrollLabel.setColour(juce::Label::textColourId, juce::Colour(VintageLookAndFeel::metalDark));
        addAndMakeVisible(waveformScrollLabel);
        
        waveformScrollDownToggle.setButtonText("Downward (tape-style)");
        waveformScrollDownToggle.setToggleState(true, juce::dontSendNotification);
        waveformScrollDownToggle.onStateChange = [this]() {
            if (settings)
                settings->setWaveformScrollDown(waveformScrollDownToggle.getToggleState());
        };
        addAndMakeVisible(waveformScrollDownToggle);

        // ---- Channel name section headers ----
        for (auto* hdr : { &inputsHeader, &outputsHeader })
        {
            hdr->setFont(juce::Font(12.0f, juce::Font::bold));
            hdr->setColour(juce::Label::textColourId, juce::Colour(VintageLookAndFeel::accentAmber));
            addAndMakeVisible(hdr);
        }
        inputsHeader.setText("Hardware Inputs  — click name to rename",
                              juce::dontSendNotification);
        outputsHeader.setText("Hardware Outputs — click name to rename",
                              juce::dontSendNotification);

        // Viewports for the rename lists
        inputsViewport.setViewedComponent(&inputsContainer,   false);
        outputsViewport.setViewedComponent(&outputsContainer, false);
        inputsViewport.setScrollBarsShown(true, false);
        outputsViewport.setScrollBarsShown(true, false);
        addAndMakeVisible(inputsViewport);
        addAndMakeVisible(outputsViewport);
    }

    void parentHierarchyChanged() override
    {
        if (!devicesPopulated && isShowing())
        {
            populateASIODevices();
            loadSavedSettings();
            populateChannelRenames();
            devicesPopulated = true;
        }
    }

    void loadSavedSettings()
    {
        if (!settings) return;
        int savedRate = settings->getSampleRate();
        if      (savedRate == 44100) sampleRateCombo.setSelectedId(1, juce::dontSendNotification);
        else if (savedRate == 48000) sampleRateCombo.setSelectedId(2, juce::dontSendNotification);
        else if (savedRate == 88200) sampleRateCombo.setSelectedId(3, juce::dontSendNotification);
        else if (savedRate == 96000) sampleRateCombo.setSelectedId(4, juce::dontSendNotification);
        int savedBuffer = settings->getBufferSize();
        if      (savedBuffer == 64  ) bufferSizeCombo.setSelectedId(1, juce::dontSendNotification);
        else if (savedBuffer == 128 ) bufferSizeCombo.setSelectedId(2, juce::dontSendNotification);
        else if (savedBuffer == 256 ) bufferSizeCombo.setSelectedId(3, juce::dontSendNotification);
        else if (savedBuffer == 512 ) bufferSizeCombo.setSelectedId(4, juce::dontSendNotification);
        else if (savedBuffer == 1024) bufferSizeCombo.setSelectedId(5, juce::dontSendNotification);
        
        // Load waveform scroll direction
        waveformScrollDownToggle.setToggleState(settings->getWaveformScrollDown(), juce::dontSendNotification);
    }

    void populateChannelRenames()
    {
        // Clear existing
        inputEditors.clear();
        outputEditors.clear();
        inputsContainer.removeAllChildren();
        outputsContainer.removeAllChildren();

        if (!audioDeviceManager) return;
        auto* device = audioDeviceManager->getCurrentAudioDevice();
        if (!device) return;

        auto inNames  = device->getInputChannelNames();
        auto outNames = device->getOutputChannelNames();
        const int ROW_H = 26;

        auto makeRow = [&](juce::Component& container, juce::OwnedArray<juce::TextEditor>& editors,
                           int idx, const juce::String& hwName, bool isInput)
        {
            auto* ed = new juce::TextEditor();
            juce::String saved = isInput ? (settings ? settings->getInputChannelName(idx)  : "")
                                         : (settings ? settings->getOutputChannelName(idx) : "");
            // Default names from settings return "IN N" / "OUT N" if not customised.
            // We only pre-fill the editor if the user has actually set a custom name.
            bool hasCustom = isInput ? (settings && settings->getAudioSettings().inputChannelNames.count(idx) > 0)
                                     : (settings && settings->getAudioSettings().outputChannelNames.count(idx) > 0);
            ed->setText(hasCustom ? saved : "", false);
            ed->setTextToShowWhenEmpty(hwName, juce::Colour(0xff666055));
            ed->setFont(juce::Font(11.0f));
            ed->setColour(juce::TextEditor::backgroundColourId,     juce::Colour(0xff1e1b16));
            ed->setColour(juce::TextEditor::textColourId,           juce::Colour(0xffeedfbb));
            ed->setColour(juce::TextEditor::outlineColourId,        juce::Colour(0xff2a2520));
            ed->setColour(juce::TextEditor::focusedOutlineColourId, juce::Colour(VintageLookAndFeel::accentAmber));
            ed->setColour(juce::CaretComponent::caretColourId,      juce::Colour(VintageLookAndFeel::accentAmber));
            const int capturedIdx = idx;
            const bool capturedIsInput = isInput;
            ed->onFocusLost = [this, ed, capturedIdx, capturedIsInput]() {
                juce::String text = ed->getText();
                if (capturedIsInput)
                    { if (settings) settings->setInputChannelName(capturedIdx, text); }
                else
                    { if (settings) settings->setOutputChannelName(capturedIdx, text); }
            };
            container.addAndMakeVisible(ed);
            editors.add(ed);
        };

        for (int i = 0; i < inNames.size(); ++i)
            makeRow(inputsContainer, inputEditors, i, inNames[i], true);
        for (int i = 0; i < outNames.size(); ++i)
            makeRow(outputsContainer, outputEditors, i, outNames[i], false);

        const int ROW_HH = 26;
        inputsContainer.setSize(300, (int)inNames.size()  * ROW_HH + 4);
        outputsContainer.setSize(300, (int)outNames.size() * ROW_HH + 4);
        for (int i = 0; i < inputEditors.size();  ++i)  inputEditors[i] ->setBounds(4, 2 + i * ROW_HH, inputsContainer.getWidth()  - 8, ROW_HH - 4);
        for (int i = 0; i < outputEditors.size(); ++i) outputEditors[i]->setBounds(4, 2 + i * ROW_HH, outputsContainer.getWidth() - 8, ROW_HH - 4);
        resized();
    }

    void resized() override
    {
        auto b = getLocalBounds().reduced(16);
        const int labelH = 22, comboH = 28, gap = 10;

        deviceLabel.setBounds(b.removeFromTop(labelH));
        deviceCombo.setBounds(b.removeFromTop(comboH));
        b.removeFromTop(gap);

        sampleRateLabel.setBounds(b.removeFromTop(labelH));
        sampleRateCombo.setBounds(b.removeFromTop(comboH));
        b.removeFromTop(gap);

        bufferSizeLabel.setBounds(b.removeFromTop(labelH));
        bufferSizeCombo.setBounds(b.removeFromTop(comboH));
        b.removeFromTop(gap);
        
        waveformScrollLabel.setBounds(b.removeFromTop(labelH));
        waveformScrollDownToggle.setBounds(b.removeFromTop(comboH));
        b.removeFromTop(gap + 6);

        // Split remaining space left/right for input and output rename panels
        auto left  = b.removeFromLeft((b.getWidth() - 12) / 2);
        b.removeFromLeft(12);
        auto right = b;

        inputsHeader.setBounds(left.removeFromTop(18));
        left.removeFromTop(4);
        inputsViewport.setBounds(left);
        if (inputsContainer.getWidth() > 0)
            inputsContainer.setSize(left.getWidth() - inputsViewport.getScrollBarThickness(), inputsContainer.getHeight());

        outputsHeader.setBounds(right.removeFromTop(18));
        right.removeFromTop(4);
        outputsViewport.setBounds(right);
        if (outputsContainer.getWidth() > 0)
            outputsContainer.setSize(right.getWidth() - outputsViewport.getScrollBarThickness(), outputsContainer.getHeight());
    }

    int getSelectedSampleRate() const
    {
        switch (sampleRateCombo.getSelectedId())
        {
            case 1: return 44100; case 2: return 48000;
            case 3: return 88200; case 4: return 96000;
            default: return 48000;
        }
    }

    int getSelectedBufferSize() const
    {
        switch (bufferSizeCombo.getSelectedId())
        {
            case 1: return 64; case 2: return 128; case 3: return 256;
            case 4: return 512; case 5: return 1024; default: return 512;
        }
    }

    juce::String getSelectedDeviceName() const
    {
        int id = deviceCombo.getSelectedId();
        if (id > 0) return deviceCombo.getItemText(id - 1);
        return {};
    }

    juce::AudioIODeviceType* getSelectedDeviceType() const
    {
        auto& types = audioDeviceManager->getAvailableDeviceTypes();
        for (auto* t : types) if (t && t->getTypeName() == "ASIO") return t;
        return nullptr;
    }

private:
    void populateASIODevices()
    {
        try
        {
            if (!audioDeviceManager) return;
            auto& types = audioDeviceManager->getAvailableDeviceTypes();
            juce::AudioIODeviceType* asioType = nullptr;
            for (auto* t : types) if (t && t->getTypeName() == "ASIO") { asioType = t; break; }
            if (!asioType) return;
            asioType->scanForDevices();
            auto names = asioType->getDeviceNames(false);
            deviceCombo.clear();
            for (int i = 0; i < names.size(); ++i) deviceCombo.addItem(names[i], i + 1);
            auto* cur = audioDeviceManager->getCurrentAudioDevice();
            if (cur)
            {
                for (int i = 1; i <= deviceCombo.getNumItems(); ++i)
                    if (deviceCombo.getItemText(i - 1) == cur->getName())
                        { deviceCombo.setSelectedId(i, juce::dontSendNotification); break; }
            }
            else if (deviceCombo.getNumItems() > 0)
                deviceCombo.setSelectedId(1, juce::dontSendNotification);
        }
        catch (...) { juce::Logger::writeToLog("Exception scanning ASIO devices"); }
    }

    Settings*                  settings;
    juce::AudioDeviceManager*  audioDeviceManager;

    juce::Label    deviceLabel;
    juce::ComboBox deviceCombo;
    juce::Label    sampleRateLabel;
    juce::ComboBox sampleRateCombo;
    juce::Label    bufferSizeLabel;
    juce::ComboBox bufferSizeCombo;
    
    // Waveform scroll direction
    juce::Label        waveformScrollLabel;
    juce::ToggleButton waveformScrollDownToggle;

    // Channel renaming
    juce::Label   inputsHeader, outputsHeader;
    juce::Viewport inputsViewport, outputsViewport;
    juce::Component inputsContainer, outputsContainer;
    juce::OwnedArray<juce::TextEditor> inputEditors, outputEditors;

    bool devicesPopulated = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioSettingsTab)
};

//==============================================================================
// Routing Matrix Tab
#include "RoutingTabs.h"


//==============================================================================
// Session Tab
//==============================================================================
class SessionTab : public juce::Component
{
public:
    SessionTab(Settings* settingsPtr, AudioEngine* enginePtr = nullptr)
        : settings(settingsPtr), audioEngine(enginePtr)
    {
        // Title
        titleLabel.setText("Session Management", juce::dontSendNotification);
        titleLabel.setFont(juce::Font(16.0f, juce::Font::bold));
        titleLabel.setJustificationType(juce::Justification::centred);
        titleLabel.setColour(juce::Label::textColourId, juce::Colour(VintageLookAndFeel::metalDark));
        addAndMakeVisible(titleLabel);
        
        // Session path
        sessionPathLabel.setText("Sessions Location:", juce::dontSendNotification);
        sessionPathLabel.setFont(juce::Font(14.0f, juce::Font::bold));
        sessionPathLabel.setColour(juce::Label::textColourId, juce::Colour(VintageLookAndFeel::metalDark));
        addAndMakeVisible(sessionPathLabel);
        
        sessionPathEditor.setText(settings->getSessionPath());
        sessionPathEditor.setMultiLine(false);
        sessionPathEditor.setFont(juce::Font(13.0f));
        addAndMakeVisible(sessionPathEditor);
        
        browseButton.setButtonText("Browse...");
        browseButton.onClick = [this]() { browseForSessionPath(); };
        addAndMakeVisible(browseButton);
        
        // Track count
        trackCountLabel.setText("Track Count:", juce::dontSendNotification);
        trackCountLabel.setFont(juce::Font(14.0f, juce::Font::bold));
        trackCountLabel.setColour(juce::Label::textColourId, juce::Colour(VintageLookAndFeel::metalDark));
        addAndMakeVisible(trackCountLabel);
        
        trackCountCombo.addItem("8 Tracks", 1);
        trackCountCombo.addItem("16 Tracks", 2);
        trackCountCombo.addItem("32 Tracks", 3);
        trackCountCombo.addItem("64 Tracks", 4);
        int savedCount = settings->getTrackCount();
        if      (savedCount == 8)  trackCountCombo.setSelectedId(1, juce::dontSendNotification);
        else if (savedCount == 16) trackCountCombo.setSelectedId(2, juce::dontSendNotification);
        else if (savedCount == 32) trackCountCombo.setSelectedId(3, juce::dontSendNotification);
        else if (savedCount == 64) trackCountCombo.setSelectedId(4, juce::dontSendNotification);
        else                       trackCountCombo.setSelectedId(1, juce::dontSendNotification);
        addAndMakeVisible(trackCountCombo);
        
        // Session name
        sessionNameLabel.setText("Current Session Name:", juce::dontSendNotification);
        sessionNameLabel.setFont(juce::Font(14.0f, juce::Font::bold));
        sessionNameLabel.setColour(juce::Label::textColourId, juce::Colour(VintageLookAndFeel::metalDark));
        addAndMakeVisible(sessionNameLabel);
        sessionNameEditor.setText(settings->getSessionName());
        sessionNameEditor.setMultiLine(false);
        sessionNameEditor.setFont(juce::Font(13.0f));
        addAndMakeVisible(sessionNameEditor);
        
        // Session notes
        sessionNotesLabel.setText("Session Notes:", juce::dontSendNotification);
        sessionNotesLabel.setFont(juce::Font(14.0f, juce::Font::bold));
        sessionNotesLabel.setColour(juce::Label::textColourId, juce::Colour(VintageLookAndFeel::metalDark));
        addAndMakeVisible(sessionNotesLabel);
        sessionNotesEditor.setMultiLine(true);
        sessionNotesEditor.setReturnKeyStartsNewLine(true);
        sessionNotesEditor.setText(settings->getSessionNotes());
        sessionNotesEditor.setFont(juce::Font(13.0f));
        addAndMakeVisible(sessionNotesEditor);
        
        // ---- Virtual Track Controls ----
        vtracksHeader.setText("Virtual Tracks", juce::dontSendNotification);
        vtracksHeader.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::bold));
        vtracksHeader.setColour(juce::Label::textColourId, juce::Colour(VintageLookAndFeel::accentAmber));
        addAndMakeVisible(vtracksHeader);
        
        // Click track enable
        clickEnableToggle.setButtonText("Click Track (BPM-locked metronome)");
        clickEnableToggle.setClickingTogglesState(true);
        clickEnableToggle.setToggleState(audioEngine ? audioEngine->getClickEnabled() : false,
                                         juce::dontSendNotification);
        clickEnableToggle.onClick = [this] {
            if (audioEngine)
                audioEngine->setClickEnabled(clickEnableToggle.getToggleState());
        };
        addAndMakeVisible(clickEnableToggle);
        
        // Timecode track enable
        tcEnableToggle.setButtonText("Timecode Track (LTC SMPTE 30fps output)");
        tcEnableToggle.setClickingTogglesState(true);
        tcEnableToggle.setToggleState(audioEngine ? audioEngine->getTimecodeEnabled() : false,
                                      juce::dontSendNotification);
        tcEnableToggle.onClick = [this] {
            if (audioEngine)
                audioEngine->setTimecodeEnabled(tcEnableToggle.getToggleState());
        };
        addAndMakeVisible(tcEnableToggle);
        
        // LTC Input channel
        ltcInLabel.setText("LTC Input Ch:", juce::dontSendNotification);
        ltcInLabel.setFont(juce::Font(13.0f));
        ltcInLabel.setColour(juce::Label::textColourId, juce::Colour(VintageLookAndFeel::metalDark));
        addAndMakeVisible(ltcInLabel);
        
        ltcInCombo.addItem("Disabled", 1);
        for (int i = 1; i <= 32; ++i)
            ltcInCombo.addItem("Input " + juce::String(i), i + 1);
        int ltcCh = settings->getLTCInputChannel();
        ltcInCombo.setSelectedId(ltcCh < 0 ? 1 : ltcCh + 2, juce::dontSendNotification);
        ltcInCombo.onChange = [this] {
            int sel = ltcInCombo.getSelectedId();
            int ch = sel <= 1 ? -1 : sel - 2;
            settings->setLTCInputChannel(ch);
            if (audioEngine)
                audioEngine->setLTCInputEnabled(ch >= 0, ltcSlaveToggle.getToggleState());
        };
        addAndMakeVisible(ltcInCombo);
        
        // LTC slave mode
        ltcSlaveToggle.setButtonText("Chase (slave transport to LTC)");
        ltcSlaveToggle.setClickingTogglesState(true);
        ltcSlaveToggle.setToggleState(audioEngine ? audioEngine->getLTCSlaveEnabled() : false,
                                      juce::dontSendNotification);
        ltcSlaveToggle.onClick = [this] {
            if (audioEngine)
            {
                int ch = settings->getLTCInputChannel();
                bool slave = ltcSlaveToggle.getToggleState();
                audioEngine->setLTCInputEnabled(ch >= 0, slave);
                settings->setLTCSlaveMode(slave);
            }
        };
        addAndMakeVisible(ltcSlaveToggle);
        
        // Buttons
        manageSessionsButton.setButtonText("Manage Sessions...");
        manageSessionsButton.onClick = [this]() { if (onManageSessionsClick) onManageSessionsClick(); };
        addAndMakeVisible(manageSessionsButton);
        
        saveSessionButton.setButtonText("Save Current Session");
        saveSessionButton.onClick = [this]() { saveCurrentSession(); };
        addAndMakeVisible(saveSessionButton);
    }
    
    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(VintageLookAndFeel::bgTrack));
    }
    
    void resized() override
    {
        auto bounds = getLocalBounds().reduced(20);
        
        titleLabel.setBounds(bounds.removeFromTop(30));
        bounds.removeFromTop(10);
        
        // Session path
        sessionPathLabel.setBounds(bounds.removeFromTop(25));
        auto pathRow = bounds.removeFromTop(30);
        browseButton.setBounds(pathRow.removeFromRight(100));
        pathRow.removeFromRight(10);
        sessionPathEditor.setBounds(pathRow);
        bounds.removeFromTop(10);
        
        // Track count
        trackCountLabel.setBounds(bounds.removeFromTop(25));
        trackCountCombo.setBounds(bounds.removeFromTop(28).removeFromLeft(200));
        bounds.removeFromTop(10);
        
        // Session name
        sessionNameLabel.setBounds(bounds.removeFromTop(25));
        sessionNameEditor.setBounds(bounds.removeFromTop(28));
        bounds.removeFromTop(10);
        
        // Session notes
        sessionNotesLabel.setBounds(bounds.removeFromTop(25));
        sessionNotesEditor.setBounds(bounds.removeFromTop(70));
        bounds.removeFromTop(12);
        
        // Virtual tracks section
        vtracksHeader.setBounds(bounds.removeFromTop(20));
        bounds.removeFromTop(4);
        clickEnableToggle.setBounds(bounds.removeFromTop(24));
        bounds.removeFromTop(4);
        tcEnableToggle.setBounds(bounds.removeFromTop(24));
        bounds.removeFromTop(8);
        
        // LTC Input row
        {
            auto row = bounds.removeFromTop(26);
            ltcInLabel.setBounds(row.removeFromLeft(100));
            ltcInCombo.setBounds(row.removeFromLeft(130));
            row.removeFromLeft(16);
            ltcSlaveToggle.setBounds(row);
        }
        bounds.removeFromTop(12);
        
        // Buttons
        auto buttonRow = bounds.removeFromTop(36);
        saveSessionButton.setBounds(buttonRow.removeFromLeft(180));
        buttonRow.removeFromLeft(10);
        manageSessionsButton.setBounds(buttonRow.removeFromLeft(180));
    }
    
    void applySettings()
    {
        if (!settings) return;
        
        settings->setSessionPath(sessionPathEditor.getText());
        settings->setSessionName(sessionNameEditor.getText());
        settings->setSessionNotes(sessionNotesEditor.getText());
        
        // Save track count
        int trackCount = getSelectedTrackCount();
        settings->setTrackCount(trackCount);
    }
    
    int getSelectedTrackCount() const
    {
        switch (trackCountCombo.getSelectedId())
        {
            case 1: return 8;
            case 2: return 16;
            case 3: return 32;
            case 4: return 64;
            default: return 8;
        }
    }
    
    void saveCurrentSession()
    {
        applySettings();
        
        if (settings->saveSession())
        {
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::InfoIcon,
                "Session Saved",
                "Session saved successfully to:\n" + settings->getCurrentSessionDirectory().getFullPathName(),
                juce::String(),
                nullptr);
        }
        else
        {
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::WarningIcon,
                "Save Failed",
                "Failed to save session. Check the session path and try again.",
                juce::String(),
                nullptr);
        }
    }
    
    void browseForSessionPath()
    {
        auto chooser = std::make_shared<juce::FileChooser>("Select Session Location",
                                                            juce::File(sessionPathEditor.getText()));
        
        auto flags = juce::FileBrowserComponent::openMode 
                   | juce::FileBrowserComponent::canSelectDirectories;
        
        chooser->launchAsync(flags, [this, chooser](const juce::FileChooser& fc)
        {
            auto result = fc.getResult();
            if (result != juce::File())
            {
                sessionPathEditor.setText(result.getFullPathName());
            }
        });
    }
    
    std::function<void()> onManageSessionsClick;

private:
    Settings* settings;
    AudioEngine* audioEngine = nullptr;
    
    juce::Label titleLabel;
    juce::Label sessionPathLabel;
    juce::TextEditor sessionPathEditor;
    juce::TextButton browseButton;
    juce::Label trackCountLabel;
    juce::ComboBox trackCountCombo;
    juce::Label sessionNameLabel;
    juce::TextEditor sessionNameEditor;
    juce::Label sessionNotesLabel;
    juce::TextEditor sessionNotesEditor;
    juce::TextButton manageSessionsButton;
    juce::TextButton saveSessionButton;
    
    // Virtual track controls
    juce::Label vtracksHeader;
    juce::ToggleButton clickEnableToggle;
    juce::ToggleButton tcEnableToggle;
    juce::Label ltcInLabel;
    juce::ComboBox ltcInCombo;
    juce::ToggleButton ltcSlaveToggle;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SessionTab)
};

//==============================================================================
// OSC Settings Tab
//==============================================================================
class OSCTab : public juce::Component
{
public:
    OSCTab(Settings* settingsPtr)
        : settings(settingsPtr)
    {
        // Title
        titleLabel.setText("OSC Remote Control", juce::dontSendNotification);
        titleLabel.setFont(juce::Font(16.0f, juce::Font::bold));
        titleLabel.setJustificationType(juce::Justification::centred);
        titleLabel.setColour(juce::Label::textColourId, juce::Colour(VintageLookAndFeel::metalDark));
        addAndMakeVisible(titleLabel);
        
        // OSC Enabled toggle
        oscEnabledToggle.setButtonText("Enable OSC Control");
        oscEnabledToggle.setToggleState(settings->getOSCEnabled(), juce::dontSendNotification);
        addAndMakeVisible(oscEnabledToggle);
        
        // OSC Port label and editor
        oscPortLabel.setText("Receive Port:", juce::dontSendNotification);
        oscPortLabel.setFont(juce::Font(14.0f, juce::Font::bold));
        oscPortLabel.setColour(juce::Label::textColourId, juce::Colour(VintageLookAndFeel::metalDark));
        addAndMakeVisible(oscPortLabel);
        
        oscPortEditor.setText(juce::String(settings->getOSCPort()));
        oscPortEditor.setInputRestrictions(5, "0123456789");
        oscPortEditor.setFont(juce::Font(13.0f));
        addAndMakeVisible(oscPortEditor);
        
        // Feedback Port label and editor
        feedbackPortLabel.setText("Feedback Port:", juce::dontSendNotification);
        feedbackPortLabel.setFont(juce::Font(14.0f, juce::Font::bold));
        feedbackPortLabel.setColour(juce::Label::textColourId, juce::Colour(VintageLookAndFeel::metalDark));
        addAndMakeVisible(feedbackPortLabel);
        
        feedbackPortEditor.setText(juce::String(settings->getOSCPort() + 1));
        feedbackPortEditor.setReadOnly(true);  // Auto-calculated
        feedbackPortEditor.setFont(juce::Font(13.0f));
        addAndMakeVisible(feedbackPortEditor);
        
        // Update feedback port when receive port changes
        oscPortEditor.onTextChange = [this]() {
            int port = oscPortEditor.getText().getIntValue();
            feedbackPortEditor.setText(juce::String(port + 1));
        };
        
        // Info text
        infoLabel.setText("OSC allows remote control via TouchOSC, Open Stage Control, or custom controllers.\n\n"
                         "Commands:\n"
                         "/transport/play, /transport/stop, /transport/record\n"
                         "/transport/rewind, /transport/ffwd\n"
                         "/track/N/arm <0 or 1>\n"
                         "/track/N/monitor <0 or 1>\n\n"
                         "Feedback: All button states sent as floats (0.0 or 1.0)\n"
                         "When a button is pressed, it sends 1.0 and all others send 0.0",
                         juce::dontSendNotification);
        infoLabel.setFont(juce::Font(12.0f));
        infoLabel.setJustificationType(juce::Justification::topLeft);
        infoLabel.setColour(juce::Label::textColourId, juce::Colour(VintageLookAndFeel::metalDark));
        addAndMakeVisible(infoLabel);
    }
    
    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(VintageLookAndFeel::bgPanel));
    }
    
    void resized() override
    {
        auto bounds = getLocalBounds().reduced(20);
        
        titleLabel.setBounds(bounds.removeFromTop(40));
        bounds.removeFromTop(10);
        
        oscEnabledToggle.setBounds(bounds.removeFromTop(30));
        bounds.removeFromTop(20);
        
        auto portRow = bounds.removeFromTop(30);
        oscPortLabel.setBounds(portRow.removeFromLeft(150));
        oscPortEditor.setBounds(portRow.removeFromLeft(100));
        bounds.removeFromTop(10);
        
        auto feedbackRow = bounds.removeFromTop(30);
        feedbackPortLabel.setBounds(feedbackRow.removeFromLeft(150));
        feedbackPortEditor.setBounds(feedbackRow.removeFromLeft(100));
        bounds.removeFromTop(20);
        
        infoLabel.setBounds(bounds);
    }
    
    void saveSettings()
    {
        settings->setOSCEnabled(oscEnabledToggle.getToggleState());
        settings->setOSCPort(oscPortEditor.getText().getIntValue());
        settings->save();
    }
    
private:
    Settings* settings;
    
    juce::Label titleLabel;
    juce::ToggleButton oscEnabledToggle;
    juce::Label oscPortLabel;
    juce::TextEditor oscPortEditor;
    juce::Label feedbackPortLabel;
    juce::TextEditor feedbackPortEditor;
    juce::Label infoLabel;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OSCTab)
};

//==============================================================================
// Main Settings Dialog with Tabs
//==============================================================================
class SettingsDialog : public juce::Component
{
public:
    SettingsDialog(Settings* settingsPtr, juce::AudioDeviceManager* deviceManager, AudioEngine* audioEnginePtr = nullptr)
        : settings(settingsPtr), audioDeviceManager(deviceManager), audioEngine(audioEnginePtr)
    {
        try
        {
            juce::Logger::writeToLog("=== Creating SettingsDialog ===");
            juce::Logger::writeToLog("  Step 1: Creating title label...");
            
            // Title
            titleLabel.setText("VTape24 Settings", juce::dontSendNotification);
            titleLabel.setFont(juce::Font(20.0f, juce::Font::bold));
            titleLabel.setJustificationType(juce::Justification::centred);
            titleLabel.setColour(juce::Label::textColourId, juce::Colour(VintageLookAndFeel::metalDark));
            addAndMakeVisible(titleLabel);
            
            juce::Logger::writeToLog("  Step 2: Creating tabs...");
            
            // Create tabs
            tabbedComponent = std::make_unique<juce::TabbedComponent>(juce::TabbedButtonBar::TabsAtTop);
            
            juce::Logger::writeToLog("  Step 3: TabbedComponent created, creating AudioSettingsTab...");
            // Audio settings tab
            audioTab = std::make_unique<AudioSettingsTab>(settings, audioDeviceManager);
            
            juce::Logger::writeToLog("  Step 4: AudioSettingsTab created, adding to tabs...");
            tabbedComponent->addTab("Audio", juce::Colour(VintageLookAndFeel::bgTrack), 
                                   audioTab.get(), false);
            
            juce::Logger::writeToLog("  Step 5: Audio tab added, creating OSCTab...");
            juce::Logger::writeToLog("  Step 6f: Creating OSCTab...");
            // OSC tab
            oscTab = std::make_unique<OSCTab>(settings);
            
            juce::Logger::writeToLog("  Step 6g: OSCTab created, adding to tabs...");
            tabbedComponent->addTab("OSC", juce::Colour(VintageLookAndFeel::bgTrack), 
                                   oscTab.get(), false);
            
            juce::Logger::writeToLog("  Step 7: All tabs added, setting tab bar depth...");
            tabbedComponent->setTabBarDepth(35);
            
            juce::Logger::writeToLog("  Step 8: Making tabs visible...");
            addAndMakeVisible(tabbedComponent.get());
            
            juce::Logger::writeToLog("  Step 9: Creating buttons...");
            
            // Buttons
            applyButton.setButtonText("Apply");
            applyButton.onClick = [this]() { applySettings(); };
            addAndMakeVisible(applyButton);
            
            cancelButton.setButtonText("Cancel");
            cancelButton.onClick = [this]() { if (onClose) onClose(); };
            addAndMakeVisible(cancelButton);
            
            juce::Logger::writeToLog("  Step 10: All components created, NOW calling setSize(700, 550)...");
            
            // IMPORTANT: Set size LAST, after all components created
            // This prevents resized() from crashing when components don't exist yet
            setSize(700, 550);
            
            juce::Logger::writeToLog("  ✓ SettingsDialog created successfully");
        }
        catch (const std::exception& e)
        {
            juce::Logger::writeToLog("ERROR creating SettingsDialog: " + juce::String(e.what()));
            throw;  // Re-throw so caller knows it failed
        }
        catch (...)
        {
            juce::Logger::writeToLog("ERROR creating SettingsDialog: Unknown exception");
            throw;
        }
    }
    
    void resized() override
    {
        auto bounds = getLocalBounds().reduced(20);
        
        titleLabel.setBounds(bounds.removeFromTop(40));
        bounds.removeFromTop(10);
        
        auto buttonArea = bounds.removeFromBottom(40);
        auto buttonWidth = 100;
        cancelButton.setBounds(buttonArea.removeFromRight(buttonWidth));
        buttonArea.removeFromRight(10);
        applyButton.setBounds(buttonArea.removeFromRight(buttonWidth));
        
        bounds.removeFromBottom(10);
        
        tabbedComponent->setBounds(bounds);
    }
    
    std::function<void()> onClose;
    std::function<void()> onApply;

private:
    void applySettings()
    {
        try
        {
            if (!settings || !audioDeviceManager) return;
            
            juce::Logger::writeToLog("=== Applying settings ===");
            
            // Get audio settings from audio tab
            auto* selectedType = audioTab->getSelectedDeviceType();
            juce::String deviceName = audioTab->getSelectedDeviceName();
            int sampleRate = audioTab->getSelectedSampleRate();
            int bufferSize = audioTab->getSelectedBufferSize();
            
            if (!selectedType || deviceName.isEmpty())
            {
                juce::Logger::writeToLog("  ERROR: No device selected");
                juce::AlertWindow::showMessageBoxAsync(
                    juce::MessageBoxIconType::WarningIcon,
                    "No Device Selected",
                    "Please select an audio device.",
                    juce::String(),
                    nullptr);
                return;
            }
            
            juce::String driverTypeName = selectedType->getTypeName();
            juce::Logger::writeToLog("  Driver type: " + driverTypeName);
            juce::Logger::writeToLog("  Device: " + deviceName);
            juce::Logger::writeToLog("  Sample rate: " + juce::String(sampleRate));
            juce::Logger::writeToLog("  Buffer size: " + juce::String(bufferSize));
            
            settings->setAudioDevice(deviceName);  // Save device name!
            settings->setAudioDriverType(driverTypeName);  // Save driver type!
            settings->setSampleRate(sampleRate);
            settings->setBufferSize(bufferSize);
            
            // Apply OSC settings
            if (oscTab)
                oscTab->saveSettings();
            
            // Get track count from settings (managed by SessionDialog now)
            int trackCount = settings->getTrackCount();
            juce::Logger::writeToLog("  Track count: " + juce::String(trackCount));
            
            // Update routing tabs if track count changed
            // Change device type
            juce::Logger::writeToLog("  Setting device type to: " + driverTypeName);
            audioDeviceManager->setCurrentAudioDeviceType(driverTypeName, true);
            
            // Configure device
            juce::AudioDeviceManager::AudioDeviceSetup setup;
            setup = audioDeviceManager->getAudioDeviceSetup();
            setup.outputDeviceName = deviceName;
            setup.inputDeviceName = deviceName;
            setup.sampleRate = sampleRate;
            setup.bufferSize = bufferSize;
            
            juce::String error = audioDeviceManager->setAudioDeviceSetup(setup, true);
            
            if (error.isEmpty())
            {
                juce::Logger::writeToLog("  ✓ SUCCESS!");
                
                juce::AlertWindow::showMessageBoxAsync(
                    juce::MessageBoxIconType::InfoIcon,
                    "Success",
                    "Settings applied successfully!",
                    juce::String(),
                    nullptr);
            }
            else
            {
                juce::Logger::writeToLog("  ✗ ERROR: " + error);
                
                juce::AlertWindow::showMessageBoxAsync(
                    juce::MessageBoxIconType::WarningIcon,
                    "Audio Device Error",
                    "Failed to configure audio device:\n" + error,
                    juce::String(),
                    nullptr);
                return;
            }
            
            if (onApply) onApply();
            if (onClose) onClose();
        }
        catch (const std::exception& e)
        {
            juce::Logger::writeToLog("Exception: " + juce::String(e.what()));
        }
    }
    
    Settings* settings;
    juce::AudioDeviceManager* audioDeviceManager;
    AudioEngine* audioEngine;
    
    juce::Label titleLabel;
    std::unique_ptr<juce::TabbedComponent> tabbedComponent;
    std::unique_ptr<AudioSettingsTab> audioTab;

    std::unique_ptr<OSCTab> oscTab;
    juce::TextButton applyButton;
    juce::TextButton cancelButton;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsDialog)
};
