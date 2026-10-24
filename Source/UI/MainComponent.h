#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "../Engine/TapeEngine.h"
#include "HawthorneLookAndFeel.h"
#include "ChannelModule.h"
#include "VUMeter.h"
#include "MarkerPanel.h"
#include "RoutingMatrix.h"
#include "SessionSheet.h"
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

    // Session
    void openInitialSession();
    void setSession (std::unique_ptr<Session>);
    void newSession();
    void createSessionWithTracks (int numTracks);
    void openSession();
    void saveSession();
    juce::String setTrackCount (int);

    // Tracks / bridge
    void rebuildModules();
    void refreshInputChoices();
    void refreshTrackNames();
    void renameTrack (int track);
    void layoutBridge();

    // Markers
    void addMarker();
    void jumpToMarker (int direction);

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
    double sessionRate() const { return session != nullptr ? session->sampleRate : 48000.0; }

    //==========================================================================
    juce::ApplicationProperties& props;
    HawthorneLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };

    TapeEngine engine;
    juce::AudioDeviceManager deviceManager;
    std::unique_ptr<Session> session;

    juce::TextButton newButton { "NEW" }, openButton { "OPEN" }, sheetButton { "SESSION SHEET" },
                     routingButton { "ROUTING" }, audioButton { "AUDIO SETTINGS" }, updateButton;
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
    MarkerPanel markers;

    std::unique_ptr<RoutingWindow> routingWindow;
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
