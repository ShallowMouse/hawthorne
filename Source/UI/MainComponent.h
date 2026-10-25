#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "../Engine/TapeEngine.h"
#include "HawthorneLookAndFeel.h"
#include "ChannelModule.h"
#include "VUMeter.h"
#include "MarkerPanel.h"
#include "RoutingMatrix.h"
#include "SessionSheet.h"
#include "LibraryView.h"
#include "NetworkWindow.h"
#include "../Updater.h"

namespace hawthorne
{

//==============================================================================
/** Tape-deck style transport key: labelled, with a lamp across the top. */
class TransportKey : public juce::Button
{
public:
    TransportKey (const juce::String& label, const juce::String& tip, juce::Colour lampColour)
        : juce::Button (label), lamp (lampColour) { setTooltip (tip); setWantsKeyboardFocus (false); }

    void setLit (bool b) { if (b != lit) { lit = b; repaint(); } }
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    juce::Colour lamp;
    bool lit = false;
};

//==============================================================================
/** The meter bridge: channel modules, 8 to a row, in rack-style rows. */
class MeterBridge : public juce::Component
{
public:
    juce::OwnedArray<ChannelModule> modules;
    static constexpr int perRow = 8;

    /** Lays out for a given width; returns the height needed. */
    int layoutForWidth (int width, int minHeight);
    void paint (juce::Graphics&) override;

private:
    std::vector<juce::Rectangle<int>> rowPanels;
};

//==============================================================================
class MainComponent : public juce::Component,
                      private juce::Timer,
                      private juce::ChangeListener
{
public:
    explicit MainComponent (juce::ApplicationProperties&);
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    /** Called before quitting: stops and saves cleanly. */
    void shutdown();

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    // Audio device
    void initialiseAudio();
    void matchDeviceToSession();
    void showAudioSettings();

    // Library / reels
    void showLibrary (bool show);
    void loadReel (const juce::File& reelFile);
    void setReel (std::unique_ptr<Reel>, std::unique_ptr<Session>);
    void saveReel();
    void saveSessionInfo();
    // Hawthorne Sync: tempo is kept with the reel; synced drum machines follow it
    void pushReelSync();
    void handleTempoRequests();
    juce::String tempoText() const;
    double tempoSaveDueMs = 0.0;

    // Tracks / bridge
    void rebuildModules();
    void refreshInputChoices();
    void refreshTrackNames();
    void renameTrack (int track);
    void layoutBridge();

    // Markers
    void addMarker();
    void jumpToMarker (int direction);

    // Speed
    void changeSpeed (double deckIps, bool variOn, double variPercent, bool save = true);
    void refreshSpeedSection();
    void stepSemitone (int direction);
    juce::String speedText() const;

    // Masters
    void showMasterMenu();
    void newMaster();
    void renameMaster();
    void deleteMaster();
    void exportMaster (juce::int64 start, juce::int64 end, const juce::String& suggestedName);
    void refreshMasterSection();

    // Latency (measured values are kept per audio device)
    juce::String latencyKey() const;
    void applyStoredLatency();

    void toggleFullScreen();

    // Network (Hawthorne Link)
    void showNetwork();
    void loadNetworkSettings();
    void saveNetworkSettings();
    void updateNetworkInputs();

    // Windows
    void showRouting();
    void updateRoutingDevice();
    void showSheet (bool takeLog = false);

    // Updates
    void refreshUpdateButton();
    void confirmUpdate();

    void showAlert (const juce::String& title, const juce::String& message);

    // Transport
    void doPlay();
    void doStop();
    void doRecord();
    void nudge (double seconds);
    void locate (juce::int64);

    juce::String buildStatus() const;
    double sessionRate() const { return reel != nullptr ? reel->sampleRate : 48000.0; }

    //==========================================================================
    juce::ApplicationProperties& props;
    HawthorneLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };

    NetReceiver netRx;                   // outlives the engine (which reads it on the audio thread)
    TapeEngine engine;
    juce::AudioDeviceManager deviceManager;
    std::unique_ptr<Reel> reel;          // the tape on the deck
    std::unique_ptr<Session> session;    // the session it belongs to

    juce::TextButton libraryButton { "LIBRARY" }, sheetButton { "SESSION SHEET" },
                     routingButton { "ROUTING" }, networkButton { "NETWORK" }, audioButton { "AUDIO SETTINGS" }, updateButton;
    Updater updater;

    TransportKey rtzKey  { "RTZ",  "Return to zero (Enter)",          Colours::mustard },
                 rewKey  { "<<",   "Back 5 seconds (Left)",           Colours::mustard },
                 ffwKey  { ">>",   "Forward 5 seconds (Right)",       Colours::mustard },
                 stopKey { "STOP", "Stop (Space)",                    Colours::mustard },
                 playKey { "PLAY", "Play (Space)",                    Colours::mustard },
                 recKey  { "REC",  "Record / punch in-out (R)",       Colours::red },
                 markKey { "MARK", "Drop a marker here (M)",          Colours::mustard };

    juce::Viewport bridgeView;
    MeterBridge bridge;
    VUMeter masterL { "L" }, masterR { "R" };
    juce::TextButton masterButton;
    TransportKey masterArmKey { "ARM",  "Arm the master: record the mix onto this reel, in step with the tracks", Colours::red },
                 monitorKey   { "TAPE", "Master monitoring: hear the master tape instead of the live mix",       Colours::mustard };
    juce::Rectangle<int> masterLabelArea, masterPanelArea;
    juce::TextButton fullScreenButton { "FULL SCREEN" };

    TransportKey ips7Key  { juce::String (juce::CharPointer_UTF8 ("7\xc2\xbd")), juce::String (juce::CharPointer_UTF8 ("Deck speed 7\xc2\xbd ips")), Colours::mustard },
                 ips15Key { "15",   "Deck speed 15 ips",  Colours::mustard },
                 ips30Key { "30",   "Deck speed 30 ips",  Colours::mustard },
                 variKey  { "VARI", "Varispeed on / off (keeps the setting)", Colours::mustard };
    juce::Slider variKnob { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    juce::Label variPctLabel, variStLabel;
    juce::TextButton stDownButton { "- ST" }, stUpButton { "+ ST" }, variResetButton { "0" };
    juce::Rectangle<int> speedLabelArea;
    MarkerPanel markers;
    LibraryView library { props };

    std::unique_ptr<RoutingWindow> routingWindow;
    std::unique_ptr<NetworkWindow> networkWindow;
    juce::String netInputsKey;
    std::unique_ptr<SessionSheetWindow> sheetWindow;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::Rectangle<int> headerArea, deckArea, counterArea, reelInfoArea, statusArea;
    juce::String statusText;
    int frame = 0;
    double lastTickMs = 0.0;
    bool lastRolling = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace hawthorne
