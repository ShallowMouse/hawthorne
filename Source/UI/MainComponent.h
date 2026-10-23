#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "../Engine/TapeEngine.h"
#include "HawthorneLookAndFeel.h"
#include "TrackStrip.h"
#include "VUMeter.h"

namespace hawthorne
{

//==============================================================================
class TransportButton : public juce::Button
{
public:
    enum class Icon { rtz, rewind, forward, stop, play, record };

    TransportButton (Icon i, const juce::String& tip) : juce::Button (tip), icon (i) { setTooltip (tip); }
    void setLit (bool b) { if (b != lit) { lit = b; repaint(); } }
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    Icon icon;
    bool lit = false;
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

    void initialiseAudio();
    void openInitialSession();
    void setSession (std::unique_ptr<Session>);
    void newSession();
    void openSession();
    void showAudioSettings();
    void matchDeviceToSession();
    void refreshInputChoices();
    void showAlert (const juce::String& title, const juce::String& message);

    void doPlay();
    void doStop();
    void doRecord();
    void nudge (double seconds);

    juce::String formatTime (juce::int64 samples) const;
    juce::String buildStatus() const;
    double sessionRate() const { return session != nullptr ? session->sampleRate : 48000.0; }

    //==========================================================================
    juce::ApplicationProperties& props;
    HawthorneLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };

    TapeEngine engine;
    juce::AudioDeviceManager deviceManager;
    std::unique_ptr<Session> session;

    juce::TextButton newButton { "NEW" }, openButton { "OPEN" }, audioButton { "AUDIO SETTINGS" };
    TransportButton rtzButton  { TransportButton::Icon::rtz,     "Return to zero (Enter)" },
                    rewButton  { TransportButton::Icon::rewind,  "Back 5 seconds (Left)" },
                    ffwButton  { TransportButton::Icon::forward, "Forward 5 seconds (Right)" },
                    stopButton { TransportButton::Icon::stop,    "Stop (Space)" },
                    playButton { TransportButton::Icon::play,    "Play (Space)" },
                    recButton  { TransportButton::Icon::record,  "Record / punch in (R)" };

    juce::OwnedArray<TrackStrip> strips;
    juce::OwnedArray<VUMeter> vus;   // 24 tracks, then master L / R
    juce::Rectangle<int> bridgeArea;

    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> headerArea, transportArea, counterArea, statusArea;
    juce::String statusText;
    int frame = 0;
    double lastTickMs = 0.0;
    bool lastRolling = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace hawthorne
