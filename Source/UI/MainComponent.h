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
#include "../Melville.h"
#include "SyncWindow.h"
#include "../Engine/Mtc.h"
#include "../Updater.h"
#include "Layout.h"
#include "Icons.h"

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
    bool isLit() const { return lit; }
    /** Show an icon in place of the word (the word stays as its name). */
    void setIcon (Icon i) { icon = i; repaint(); }
    Icon getIcon() const { return icon; }
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    juce::Colour lamp;
    bool lit = false;
    Icon icon = Icon::none;
};

//==============================================================================
/** The meter bridge: channel modules, 8 to a row, in rack-style rows. */
class MeterBridge : public juce::Component
{
public:
    juce::OwnedArray<ChannelModule> modules;
    /** Meters per row (8 / 12 / 16 / 24), or 0: fit them all in without scrolling. */
    void setPerRow (int n) { perRowSetting = n == 0 || n == 8 || n == 12 || n == 16 || n == 24 ? n : 8; }
    int  getPerRow() const { return perRowSetting; }
    int  perRowSetting = 8, perRow = 8;
    bool nrStrip = false;          // the reel uses noise reduction: a strip under each meter

    /** Lays out for a given width; returns the height needed. */
    int layoutForWidth (int width, int minHeight);
    void paint (juce::Graphics&) override;

private:
    std::vector<juce::Rectangle<int>> rowPanels;
};

//==============================================================================
/** Tempo in the transport: BPM (drag up / down, shift = fine; double-click to type),
    time signature (click), and TAP. */
class TempoBox : public juce::Component,
                 public juce::SettableTooltipClient
{
public:
    TempoBox();

    std::function<Reel*()> getReel;
    std::function<void (bool save)> onChanged;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    void setBpm (double bpm, bool save);
    juce::Rectangle<int> bpmArea, sigArea;
    juce::TextButton tapButton { "TAP" };
    juce::TextEditor editor;
    juce::Array<double> taps;
    double dragStart = 0.0;
    bool dragging = false;
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
    void mouseMove (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

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
    void applyTempo (double bpm, int beatsPerBar, double barOne);   // the reel's tempo (and everyone following it)
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
    void setTrackName (int track, const juce::String&);
    void setTrackColour (int track, juce::Colour);       // transparent: none
    static const juce::Array<juce::Colour>& trackPalette();
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
    void newMaster (std::function<void()> then = {});
    void showMachines();
    void renameMaster();
    void deleteMaster();
    void exportMaster (juce::int64 start, juce::int64 end, const juce::String& suggestedName);
    // Tape jobs (transport stopped, files let go while they run)
    void showAlignment();
    void recordTones();
    void setNoiseReduction (int system);
    void showTrackMenu (int track);
    void showMachineMenu();
    void showManual();
    /** Style the deck and meters after the reel's machines. */
    void applySkin();
    void importAudio (int track);
    void exportTracks (juce::Array<int> tracks);
    void flipReel();
    void runTapeJob (const juce::String& title, std::function<juce::String (std::function<bool (double)>)> work,
                     std::function<void (bool ok)> after);
    void refreshMasterSection();

    // Latency (measured values are kept per audio device)
    juce::String latencyKey() const;
    void applyStoredLatency();

    void toggleFullScreen();
    void showSyncMenu();
    void showCounterMenu();
    int counterMode = 0;              // big display: 0 min:sec, 1 bars.beats, 2 timecode

    // Tempo, click, timecode
    void showSync (SyncPanel::Page);
    void tempoEdited();
    void loadSyncSettings();
    void applySyncSettings (const SyncPanel::Settings&);

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

    // Hawthorne Link. Inside MELVILLE the receiver is MELVILLE's (its NETWORK panel); Hawthorne
    // still tells the senders where the tape is and takes their tempo changes.
    // Outlives the engine (which reads it on the audio thread).
    std::unique_ptr<NetReceiver> ownNetRx { melville::networkReceiver() == nullptr ? std::make_unique<NetReceiver>() : nullptr };
    NetReceiver& netRx { ownNetRx != nullptr ? *ownNetRx : *melville::networkReceiver() };
    TapeEngine engine;
    MtcOutput mtcOutput { engine };
    MtcInput mtcInput { engine };
    SyncPanel::Settings syncSettings;
    juce::AudioDeviceManager deviceManager;
    std::unique_ptr<Reel> reel;          // the tape on the deck
    std::unique_ptr<Session> session;    // the session it belongs to

    IconButton libraryButton { "Library", Icon::library }, sheetButton { "Session sheet", Icon::sheet },
               routingButton { "Routing", Icon::routing }, networkButton { "Network (Hawthorne Link)", Icon::network },
               syncButton { "Tempo and click", Icon::tempo }, timecodeButton { "Timecode", Icon::timecode },
               audioButton { "Settings", Icon::settings }, machineButton { "Machine", Icon::machine },
               layoutButton { "Displays (extra screens)", Icon::layout }, helpButton { "Manual", Icon::help };
    juce::TextButton updateButton;
    Updater updater;

    TransportKey rtzKey  { "RTZ",  "Return to zero (Enter)",          Colours::mustard },
                 rewKey  { "<<",   "Rewind: winds back while held (Left arrow: back 5 seconds)",       Colours::mustard },
                 ffwKey  { ">>",   "Fast forward: winds on while held (Right arrow: forward 5 seconds)", Colours::mustard },
                 stopKey { "STOP", "Stop (Space)",                    Colours::mustard },
                 playKey { "PLAY", "Play (Space)",                    Colours::mustard },
                 recKey  { "REC",  "Record / punch in-out (R)",       Colours::red },
                 markKey { "MARK", "Drop a marker here (M)",          Colours::mustard },
                 clickKey { "CLICK", "Click on / off (needs a tempo - TEMPO)", Colours::mustard },
                 inputKey { "INPUT", "INPUT: armed tracks always play their inputs, so you hear what you're recording while the tape plays "
                                     "(drums from THOREAU, say). Off: auto input, as on tape - armed tracks hear their input when stopped or "
                                     "recording, and the tape on playback.", Colours::mustard },
                 liftKey { "CUE", "Lifter defeat: keep the tape on the heads while it spools, so you hear it wind (the chatter) - on the meters and outputs, never recorded.", Colours::mustard };

    juce::Viewport bridgeView;
    MeterBridge bridge;
    VUMeter masterL { "L" }, masterR { "R" };
    juce::TextButton masterButton;
    TransportKey masterArmKey { "ARM",  "Arm the master: record the mix onto this reel, in step with the tracks", Colours::red },
                 monitorKey   { "TAPE", "Master monitoring: hear the master tape instead of the live mix",       Colours::mustard };
    juce::Rectangle<int> deckBadgeArea, masterBadgeArea;
    juce::Rectangle<int> masterLabelArea, masterPanelArea, locatorArea, locatorLabelArea, inTimeArea, outTimeArea;
    TransportKey inKey   { "IN",   "Punch IN point: click to set it here. Shift-click to go there.", Colours::mustard },
                 outKey  { "OUT",  "Punch OUT point: click to set it here. Shift-click to go there.", Colours::mustard },
                 autoKey { "AUTO", "Auto punch: REC spools to the pre-roll, plays, and punches in and out at IN / OUT (pre / post roll in SETTINGS)", Colours::red },
                 rehKey  { "REH",  "Rehearse: with AUTO, the punch is rehearsed - armed tracks switch to input between IN and OUT, nothing is recorded", Colours::mustard },
                 loopKey { "LOOP", "Cycle: play IN to OUT round and round. With AUTO, the whole punch (pre-roll to post-roll) repeats, recording over the last try each time", Colours::mustard };
    void setPunchHere (bool isIn);
    TempoBox tempoBox;
    juce::TextButton syncSourceButton;
    bool rewHeld = false, ffwHeld = false;
    IconButton fullScreenButton { "Full screen (F11)", Icon::fullScreen };
    WindowButton winMinButton { WindowButton::minimise }, winMaxButton { WindowButton::maximise }, winCloseButton { WindowButton::close };
    juce::ComponentDragger windowDragger;
    bool draggingWindow = false;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void toggleMaximised();

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
    std::unique_ptr<SyncWindow> tempoWindow, timecodeWindow;
    juce::String netInputsKey;
    std::unique_ptr<SessionSheetWindow> sheetWindow;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::Rectangle<int> headerArea, deckArea, deckBand, counterArea, reelInfoArea, statusArea;

    // Layout: the panels, docked in the main window or other windows (LAYOUT)
    // the main window's sections (a fixed layout), and the extra displays built from blocks
    Panel bridgePanel { "bridge", "Meters" }, transportPanel { "transport", "Transport" }, masterPanel { "master", "Master" },
          locatorPanel { "locator", "Autolocator" }, markersPanel { "markers", "Markers" };
    LayoutManager layout { props };
    void paintCounter (juce::Graphics&, juce::Rectangle<int> area, int mode);
    // this frame's meter readings (read once, shown on the main window and the displays)
    std::array<float, 96> frameRms {}, framePeak {};
    std::array<float, 2> frameMasterRms {}, frameMasterPeak {};
    float frameDt = 1.0f / 30.0f;
    bool frameFlash = false;
    void wireModule (ChannelModule&);
    friend class MetersBlock; friend class TimeBlock; friend class TransportBlock; friend class VarispeedBlock;
    friend class TempoBlock; friend class MasterBlock; friend class LocatorBlock; friend class MarkersBlock; friend class ReelBlock;
    void setupPanels();
    void showLayoutMenu();
    void paintTransport (juce::Graphics&);
    void paintMaster (juce::Graphics&);
    void paintLocator (juce::Graphics&);
    void layoutTransport();
    void layoutMaster();
    void layoutLocator();
    void layoutBridgePanel();
    void repaintPanels();
    void transportMouseMove (const juce::MouseEvent&);
    void transportMouseUp (const juce::MouseEvent&);
    juce::String statusText;
    int frame = 0;
    double lastTickMs = 0.0;
    bool lastRolling = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace hawthorne
