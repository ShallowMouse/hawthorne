#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include "Settings.h"
#include "Session.h"
#include "AudioEngine.h"
#include "VintageLookAndFeel.h"
#include "RoutingTabs.h"

//==============================================================================
// SessionDialog — session-specific settings with tabbed layout:
//   Session tab        : name, notes, path, BPM, time sig, track count, click/LTC
//   Input Routing tab  : device inputs → tracks
//   Output Routing tab : tracks → device outputs
//==============================================================================
class SessionDialog : public juce::DocumentWindow
{
public:
    SessionDialog(Settings*                  settingsPtr,
                  AudioEngine*               enginePtr,
                  Session*                   sessionPtr,
                  juce::AudioDeviceManager*  deviceMgr)
        : juce::DocumentWindow("Session Settings",
                               juce::Colour(VintageLookAndFeel::bgPanel),
                               juce::DocumentWindow::closeButton),
          settings(settingsPtr), audioEngine(enginePtr), session(sessionPtr)
    {
        setUsingNativeTitleBar(false);
        setResizable(true, false);
        setContentOwned(new ContentComponent(settingsPtr, enginePtr, sessionPtr, deviceMgr, this), true);
        centreWithSize(700, 630);
        setVisible(true);
    }

    void closeButtonPressed() override
    {
        if (auto* c = dynamic_cast<ContentComponent*>(getContentComponent()))
            c->applySession();
        setVisible(false);
        if (onClose) onClose();
    }

    std::function<void()> onClose;
    std::function<void()> onManageSessions;

private:
    Settings*    settings;
    AudioEngine* audioEngine;
    Session*     session;

    // =========================================================================
    struct SectionHeader : public juce::Component
    {
        juce::String text;
        SectionHeader(const juce::String& t) : text(t) {}
        void paint(juce::Graphics& g) override
        {
            auto b = getLocalBounds();
            g.setColour(juce::Colour(VintageLookAndFeel::accentAmber).withAlpha(0.25f));
            g.fillRoundedRectangle(b.toFloat(), 3.0f);
            g.setColour(juce::Colour(VintageLookAndFeel::accentAmber));
            g.setFont(juce::Font(11.0f, juce::Font::bold));
            g.drawText(text.toUpperCase(), b.reduced(8, 0), juce::Justification::centredLeft);
        }
    };

    // =========================================================================
    // Session info tab — form only (no buttons, no routing)
    // =========================================================================
    class SessionInfoTab : public juce::Component
    {
    public:
        SessionInfoTab(Settings* s, AudioEngine* e, Session* sess)
            : settings(s), audioEngine(e), session(sess)
        {
            addAndMakeVisible(secInfo);
            addAndMakeVisible(secDevice);
            addAndMakeVisible(secTempo);
            addAndMakeVisible(secTracks);
            addAndMakeVisible(secVirtual);
            buildInfoSection();
            buildDeviceSection();
            buildTempoSection();
            buildTracksSection();
            buildVirtualSection();
        }

        void paint(juce::Graphics& g) override
        {
            g.fillAll(juce::Colour(VintageLookAndFeel::bgPanel));
            g.setColour(juce::Colours::black.withAlpha(0.08f));
            for (int y = 0; y < getHeight(); y += 2)
                g.drawHorizontalLine(y, 0.0f, (float)getWidth());
        }

        void resized() override
        {
            auto b = getLocalBounds().reduced(16);
            const int secH = 22, labelH = 18, rowH = 28, gap = 6, secGap = 14;

            secInfo.setBounds(b.removeFromTop(secH));
            b.removeFromTop(gap);
            nameLabel.setBounds(b.removeFromTop(labelH));
            nameEditor.setBounds(b.removeFromTop(rowH));
            b.removeFromTop(gap);
            notesLabel.setBounds(b.removeFromTop(labelH));
            notesEditor.setBounds(b.removeFromTop(60));
            b.removeFromTop(gap);
            pathLabel.setBounds(b.removeFromTop(labelH));
            {
                auto row = b.removeFromTop(rowH);
                browseButton.setBounds(row.removeFromRight(90));
                row.removeFromRight(6);
                pathEditor.setBounds(row);
            }
            b.removeFromTop(secGap);

            secDevice.setBounds(b.removeFromTop(secH));
            b.removeFromTop(gap);
            {
                auto row = b.removeFromTop(rowH);
                deviceLabel.setBounds(row.removeFromLeft(110));
                deviceCombo.setBounds(row.removeFromLeft(300));
            }
            b.removeFromTop(secGap);

            secTempo.setBounds(b.removeFromTop(secH));
            b.removeFromTop(gap);
            {
                auto row = b.removeFromTop(rowH);
                bpmLabel.setBounds(row.removeFromLeft(50));
                bpmMinus.setBounds(row.removeFromLeft(28));
                bpmEditor.setBounds(row.removeFromLeft(70));
                bpmPlus.setBounds(row.removeFromLeft(28));
                row.removeFromLeft(20);
                timeSigLabel.setBounds(row.removeFromLeft(80));
                timeSigNum.setBounds(row.removeFromLeft(55));
                timeSigSlash.setBounds(row.removeFromLeft(14));
                timeSigDen.setBounds(row.removeFromLeft(55));
            }
            b.removeFromTop(secGap);

            secTracks.setBounds(b.removeFromTop(secH));
            b.removeFromTop(gap);
            {
                auto row = b.removeFromTop(rowH);
                trackCountLabel.setBounds(row.removeFromLeft(110));
                trackCountCombo.setBounds(row.removeFromLeft(140));
            }
            b.removeFromTop(secGap);

            secVirtual.setBounds(b.removeFromTop(secH));
            b.removeFromTop(gap);
            clickToggle.setBounds(b.removeFromTop(24));
            b.removeFromTop(gap);
            ltcStatusLabel.setBounds(b.removeFromTop(18));
            b.removeFromTop(gap);
            {
                auto row = b.removeFromTop(26);
                ltcInLabel.setBounds(row.removeFromLeft(110));
                ltcInCombo.setBounds(row.removeFromLeft(130));
                row.removeFromLeft(12);
                ltcSlaveToggle.setBounds(row);
            }
        }

        void applyAll()
        {
            applyInfo();
            applyTempo();
            applyTracks();
        }

    private:
        Settings*    settings;
        AudioEngine* audioEngine;
        Session*     session;

        SectionHeader secInfo  {"Session Info"};
        SectionHeader secDevice{"Audio Hardware"};
        SectionHeader secTempo {"Tempo"};
        SectionHeader secTracks{"Track Config"};
        SectionHeader secVirtual{"Virtual Tracks"};

        juce::Label      nameLabel, notesLabel, pathLabel;
        juce::TextEditor nameEditor, notesEditor, pathEditor;
        juce::TextButton browseButton;

        juce::Label    deviceLabel;
        juce::ComboBox deviceCombo;

        juce::Label      bpmLabel, timeSigLabel, timeSigSlash;
        juce::TextEditor bpmEditor;
        juce::TextButton bpmMinus, bpmPlus;
        juce::ComboBox   timeSigNum, timeSigDen;

        juce::Label    trackCountLabel;
        juce::ComboBox trackCountCombo;

        juce::ToggleButton clickToggle, ltcSlaveToggle;
        juce::Label ltcStatusLabel;
        juce::Label        ltcInLabel;
        juce::ComboBox     ltcInCombo;

    public:
        std::function<void()> onRoutingChanged; // called after LTC toggle changes routing

        void makeLabel(juce::Label& l, const juce::String& text)
        {
            l.setText(text, juce::dontSendNotification);
            l.setFont(juce::Font(12.0f, juce::Font::bold));
            l.setColour(juce::Label::textColourId, juce::Colour(0xffccbb99));
            addAndMakeVisible(l);
        }

        void makeEditor(juce::TextEditor& e, bool multiLine = false)
        {
            e.setMultiLine(multiLine);
            e.setReturnKeyStartsNewLine(multiLine);
            e.setFont(juce::Font(13.0f));
            e.setColour(juce::TextEditor::backgroundColourId,     juce::Colour(0xff1e1b16));
            e.setColour(juce::TextEditor::textColourId,           juce::Colour(0xffeedfbb));
            e.setColour(juce::TextEditor::outlineColourId,        juce::Colour(VintageLookAndFeel::metalDark));
            e.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colour(VintageLookAndFeel::accentAmber));
            e.setColour(juce::CaretComponent::caretColourId,      juce::Colour(VintageLookAndFeel::accentAmber));
            addAndMakeVisible(e);
        }

        void makeToggle(juce::ToggleButton& t, const juce::String& text)
        {
            t.setButtonText(text);
            t.setColour(juce::ToggleButton::textColourId,         juce::Colour(0xffccbb99));
            t.setColour(juce::ToggleButton::tickColourId,         juce::Colour(VintageLookAndFeel::accentAmber));
            t.setColour(juce::ToggleButton::tickDisabledColourId, juce::Colour(0xff555555));
            addAndMakeVisible(t);
        }

        void makeSmallButton(juce::TextButton& b, const juce::String& text)
        {
            b.setButtonText(text);
            b.setColour(juce::TextButton::buttonColourId,  juce::Colour(0xff2a2520));
            b.setColour(juce::TextButton::textColourOffId, juce::Colour(VintageLookAndFeel::accentAmber));
            addAndMakeVisible(b);
        }

        void buildInfoSection()
        {
            makeLabel(nameLabel,  "Session Name");
            makeLabel(notesLabel, "Notes");
            makeLabel(pathLabel,  "Sessions Folder");

            makeEditor(nameEditor);
            nameEditor.setText(settings ? settings->getSessionName() : "");

            makeEditor(notesEditor, true);
            notesEditor.setText(settings ? settings->getSessionNotes() : "");

            makeEditor(pathEditor);
            pathEditor.setText(settings ? settings->getSessionPath() : "");

            makeSmallButton(browseButton, "Browse...");
            browseButton.onClick = [this] { browseForPath(); };
        }

        void buildDeviceSection()
        {
            makeLabel(deviceLabel, "Audio Device");

            deviceCombo.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff1e1b16));
            deviceCombo.setColour(juce::ComboBox::textColourId,       juce::Colour(0xffeedfbb));
            deviceCombo.setColour(juce::ComboBox::outlineColourId,    juce::Colour(VintageLookAndFeel::metalDark));
            addAndMakeVisible(deviceCombo);

            // Populate with every device name from every driver type
            juce::String currentDevice = settings ? settings->getAudioDevice() : juce::String();
            if (audioEngine)
            {
                auto* dm = audioEngine->getDeviceManager();
                int itemId = 1;
                for (auto* type : dm->getAvailableDeviceTypes())
                {
                    type->scanForDevices();
                    for (const auto& name : type->getDeviceNames())
                    {
                        deviceCombo.addItem(name, itemId++);
                        if (name == currentDevice)
                            deviceCombo.setSelectedId(itemId - 1, juce::dontSendNotification);
                    }
                }
            }
        }

        void buildTempoSection()
        {
            makeLabel(bpmLabel,     "BPM");
            makeLabel(timeSigLabel, "Time Sig");

            timeSigSlash.setText("/", juce::dontSendNotification);
            timeSigSlash.setJustificationType(juce::Justification::centred);
            timeSigSlash.setColour(juce::Label::textColourId, juce::Colour(0xffccbb99));
            addAndMakeVisible(timeSigSlash);

            makeEditor(bpmEditor);
            double currentBPM = audioEngine ? audioEngine->getBPM()
                                : (session    ? session->getBPM() : 120.0);
            bpmEditor.setText(juce::String(currentBPM, 1));

            for (auto* btn : { &bpmMinus, &bpmPlus }) makeSmallButton(*btn, "");
            bpmMinus.setButtonText("-");
            bpmPlus.setButtonText("+");
            bpmMinus.onClick = [this] { nudgeBPM(-1.0); };
            bpmPlus.onClick  = [this] { nudgeBPM(+1.0); };

            for (int n : {2,3,4,5,6,7,8,9,12}) timeSigNum.addItem(juce::String(n), n);
            for (int d : {2,4,8,16})             timeSigDen.addItem(juce::String(d), d);
            timeSigNum.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff1e1b16));
            timeSigNum.setColour(juce::ComboBox::textColourId,       juce::Colour(0xffeedfbb));
            timeSigDen.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff1e1b16));
            timeSigDen.setColour(juce::ComboBox::textColourId,       juce::Colour(0xffeedfbb));
            addAndMakeVisible(timeSigNum);
            addAndMakeVisible(timeSigDen);

            int num = session ? session->getTimeSignatureNumerator()   : 4;
            int den = session ? session->getTimeSignatureDenominator() : 4;
            timeSigNum.setSelectedId(num, juce::dontSendNotification);
            timeSigDen.setSelectedId(den, juce::dontSendNotification);

            bpmEditor.onReturnKey = [this] { applyTempo(); };
            bpmEditor.onFocusLost = [this] { applyTempo(); };
            timeSigNum.onChange   = [this] { applyTempo(); };
            timeSigDen.onChange   = [this] { applyTempo(); };
        }

        void buildTracksSection()
        {
            makeLabel(trackCountLabel, "Track Count");
            trackCountCombo.addItem("8 Tracks",  1);
            trackCountCombo.addItem("16 Tracks", 2);
            trackCountCombo.addItem("32 Tracks", 3);
            trackCountCombo.addItem("64 Tracks", 4);
            trackCountCombo.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff1e1b16));
            trackCountCombo.setColour(juce::ComboBox::textColourId,       juce::Colour(0xffeedfbb));
            addAndMakeVisible(trackCountCombo);
            int saved = settings ? settings->getTrackCount() : 8;
            int id = (saved == 16) ? 2 : (saved == 32) ? 3 : (saved == 64) ? 4 : 1;
            trackCountCombo.setSelectedId(id, juce::dontSendNotification);
        }

        void buildVirtualSection()
        {
            makeToggle(clickToggle,   "Click Track — BPM-locked metronome");
            makeToggle(ltcSlaveToggle,"LTC Chase — slave transport to incoming LTC");

            clickToggle.setToggleState(audioEngine ? audioEngine->getClickEnabled()    : false, juce::dontSendNotification);
            ltcSlaveToggle.setToggleState(audioEngine ? audioEngine->getLTCSlaveEnabled() : false, juce::dontSendNotification);

            clickToggle.setClickingTogglesState(true);
            ltcSlaveToggle.setClickingTogglesState(true);

            clickToggle.onClick  = [this] { if (audioEngine) audioEngine->setClickEnabled(clickToggle.getToggleState()); };
            ltcSlaveToggle.onClick = [this] {
                if (audioEngine) {
                    int ch = settings ? settings->getLTCInputChannel() : -1;
                    audioEngine->setLTCInputEnabled(ch >= 0, ltcSlaveToggle.getToggleState());
                    if (settings) settings->setLTCSlaveMode(ltcSlaveToggle.getToggleState());
                }
            };

            // LTC output status label — always active, no toggle
            ltcStatusLabel.setText("LTC Output: Always Active (SMPTE 30fps) — route via Output Routing",
                                   juce::dontSendNotification);
            ltcStatusLabel.setFont(juce::Font(11.0f));
            ltcStatusLabel.setColour(juce::Label::textColourId, juce::Colour(0xff6699ff).withAlpha(0.8f));
            addAndMakeVisible(ltcStatusLabel);

            makeLabel(ltcInLabel, "LTC Input Ch");
            ltcInCombo.addItem("Disabled", 1);
            for (int i = 1; i <= 32; ++i)
                ltcInCombo.addItem("Input " + juce::String(i), i + 1);
            ltcInCombo.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff1e1b16));
            ltcInCombo.setColour(juce::ComboBox::textColourId,       juce::Colour(0xffeedfbb));
            addAndMakeVisible(ltcInCombo);
            int ltcCh = settings ? settings->getLTCInputChannel() : -1;
            ltcInCombo.setSelectedId(ltcCh < 0 ? 1 : ltcCh + 2, juce::dontSendNotification);
            ltcInCombo.onChange = [this] {
                int sel = ltcInCombo.getSelectedId();
                int ch  = sel <= 1 ? -1 : sel - 2;
                if (settings) settings->setLTCInputChannel(ch);
                if (audioEngine) audioEngine->setLTCInputEnabled(ch >= 0, ltcSlaveToggle.getToggleState());
            };
        }

        void applyInfo()
        {
            if (!settings) return;
            settings->setSessionName(nameEditor.getText());
            settings->setSessionNotes(notesEditor.getText());
            settings->setSessionPath(pathEditor.getText());

            // Apply audio device if it changed
            juce::String selectedDevice = deviceCombo.getText();
            if (selectedDevice.isNotEmpty() && audioEngine)
            {
                juce::String currentDevice = settings->getAudioDevice();
                if (selectedDevice != currentDevice)
                {
                    juce::Logger::writeToLog("SessionDialog: switching audio device to \"" + selectedDevice + "\"");
                    audioEngine->initializeWithDevice(selectedDevice);
                }
                // Always write to session so it travels with the file
                if (session) session->setAudioDevice(selectedDevice);
                settings->setAudioDevice(selectedDevice);
            }
        }

        void applyTempo()
        {
            double bpm = juce::jlimit(20.0, 999.0, bpmEditor.getText().getDoubleValue());
            bpmEditor.setText(juce::String(bpm, 1), false);
            if (audioEngine) audioEngine->setBPM(bpm);
            if (session)     session->setBPM(bpm);
            int num = timeSigNum.getSelectedId();
            int den = timeSigDen.getSelectedId();
            if (num > 0 && den > 0 && session)
                session->setTimeSignature(num, den);
        }

        void applyTracks()
        {
            if (!settings) return;
            int count = 8;
            switch (trackCountCombo.getSelectedId())
            {
                case 2: count = 16; break;
                case 3: count = 32; break;
                case 4: count = 64; break;
                default: break;
            }
            settings->setTrackCount(count);
        }

        void nudgeBPM(double delta)
        {
            double bpm = juce::jlimit(20.0, 999.0, bpmEditor.getText().getDoubleValue() + delta);
            bpmEditor.setText(juce::String(bpm, 1), false);
            applyTempo();
        }

        void browseForPath()
        {
            auto chooser = std::make_shared<juce::FileChooser>(
                "Select Sessions Folder", juce::File(pathEditor.getText()));
            chooser->launchAsync(
                juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                [this, chooser](const juce::FileChooser& fc)
                {
                    auto f = fc.getResult();
                    if (f != juce::File()) pathEditor.setText(f.getFullPathName());
                });
        }

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SessionInfoTab)
    };

    // =========================================================================
    // ContentComponent — TabbedComponent + bottom button row
    // =========================================================================
    class ContentComponent : public juce::Component
    {
    public:
        ContentComponent(Settings*                 s,
                         AudioEngine*              e,
                         Session*                  sess,
                         juce::AudioDeviceManager* deviceMgr,
                         SessionDialog*            parent)
            : settings(s), audioEngine(e), owner(parent),
              tabs(juce::TabbedButtonBar::TabsAtTop)
        {
            setLookAndFeel(&laf);

            sessionInfoTab  = std::make_unique<SessionInfoTab>(s, e, sess);

            // Input routing uses user-track count only (virtual tracks have no record inputs)
            // Output routing uses total track count (virtual tracks route to hardware outputs)
            int userTracks   = e ? e->getUserTrackCount()       : 8;
            int totalTracks  = e ? e->getNumTracks()            : 8;
            int firstUserIdx = e ? e->getFirstUserTrackIndex()  : 0;
            inputRoutingTab  = std::make_unique<RoutingMatrixTab>(s, deviceMgr, e, userTracks, firstUserIdx);
            outputRoutingTab = std::make_unique<OutputRoutingTab>(s, deviceMgr, e, totalTracks);

            // When the LTC toggle auto-routes to output 0, immediately reflect it in the matrix
            sessionInfoTab->onRoutingChanged = [this] {
                if (outputRoutingTab) outputRoutingTab->loadRouting();
            };

            const auto tabBg = juce::Colour(VintageLookAndFeel::bgTrack);
            tabs.addTab("Session",        tabBg, sessionInfoTab.get(),   false);
            tabs.addTab("Input Routing",  tabBg, inputRoutingTab.get(),  false);
            tabs.addTab("Output Routing", tabBg, outputRoutingTab.get(), false);
            tabs.setTabBarDepth(32);
            addAndMakeVisible(tabs);

            auto styleBtn = [this](juce::TextButton& b, const juce::String& text) {
                b.setButtonText(text);
                b.setColour(juce::TextButton::buttonColourId,   juce::Colour(0xff2a2520));
                b.setColour(juce::TextButton::buttonOnColourId, juce::Colour(VintageLookAndFeel::accentAmber));
                b.setColour(juce::TextButton::textColourOffId,  juce::Colour(VintageLookAndFeel::accentAmber));
                b.setColour(juce::TextButton::textColourOnId,   juce::Colour(0xff1a1510));
                addAndMakeVisible(b);
            };

            styleBtn(saveButton,   "Save Session");
            styleBtn(manageButton, "Manage Sessions...");
            styleBtn(closeButton,  "Close");

            saveButton.onClick   = [this] { applySession(); showSaveFeedback(); };
            manageButton.onClick = [this] { if (owner->onManageSessions) owner->onManageSessions(); };
            closeButton.onClick  = [this] { applySession(); if (owner) owner->closeButtonPressed(); };
        }

        ~ContentComponent() override { setLookAndFeel(nullptr); }

        void paint(juce::Graphics& g) override
        {
            g.fillAll(juce::Colour(VintageLookAndFeel::bgPanel));
            // Divider above button row
            g.setColour(juce::Colour(VintageLookAndFeel::metalDark));
            g.drawHorizontalLine(getHeight() - 50, 0.0f, (float)getWidth());
        }

        void resized() override
        {
            auto b = getLocalBounds();
            auto btnStrip = b.removeFromBottom(50).reduced(12, 8);
            saveButton.setBounds(btnStrip.removeFromLeft(130));
            btnStrip.removeFromLeft(8);
            manageButton.setBounds(btnStrip.removeFromLeft(160));
            btnStrip.removeFromLeft(8);
            closeButton.setBounds(btnStrip.removeFromLeft(80));
            tabs.setBounds(b);
        }

        void applySession()
        {
            if (sessionInfoTab) sessionInfoTab->applyAll();
            if (audioEngine)    audioEngine->saveSessionToDisk();
        }

    private:
        Settings*      settings;
        AudioEngine*   audioEngine;
        SessionDialog* owner;
        VintageLookAndFeel laf;

        juce::TabbedComponent             tabs;
        std::unique_ptr<SessionInfoTab>   sessionInfoTab;
        std::unique_ptr<RoutingMatrixTab> inputRoutingTab;
        std::unique_ptr<OutputRoutingTab> outputRoutingTab;

        juce::TextButton saveButton, manageButton, closeButton;

        void showSaveFeedback()
        {
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::InfoIcon,
                "Session Saved",
                "Session saved to:\n" +
                    (settings ? settings->getCurrentSessionDirectory().getFullPathName()
                              : juce::String("(unknown)")),
                juce::String(), nullptr);
        }

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ContentComponent)
    };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SessionDialog)
};
