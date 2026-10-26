#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include "../Engine/TapeEngine.h"
#include "HawthorneLookAndFeel.h"

namespace hawthorne
{

//==============================================================================
/**
    Tempo, click and timecode: the reel's tempo (bars and beats), the click,
    timecode out (LTC on an output, MTC to a MIDI port) and chasing incoming
    timecode (LTC on an input, or MTC) - e.g. to run alongside Reaper.
*/
class SyncPanel : public juce::Component,
                  private juce::Timer
{
public:
    struct Settings
    {
        juce::String mtcOut;                      // MIDI output identifier ("" = off)
        TapeEngine::ChaseSource chase = TapeEngine::ChaseSource::off;
        int ltcIn = -1;                           // device input for LTC chase
        juce::String mtcIn;                       // MIDI input identifier
    };

    enum class Page { tempo, timecode };     // TEMPO: tempo + click.  TIMECODE: timecode out + chase.
    SyncPanel (Page, TapeEngine&, std::function<Reel*()> getReel, std::function<Settings()> getSettings);

    std::function<void()> onTempoChanged;         // reel tempo edited here
    std::function<void (const Settings&)> onSettingsChanged;

    /** The device's input names (for the LTC input list). */
    void setInputNames (const juce::StringArray& names, const juce::BigInteger& active);
    void refresh();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void applyTempo();
    void tap();
    void applySettings();
    void fillMidi();

    const Page page;
    TapeEngine& engine;
    std::function<Reel*()> getReel;
    std::function<Settings()> getSettings;

    juce::TextEditor bpm, tcStart;
    juce::ComboBox beats, tcRate, mtcOut, chase, ltcIn, mtcIn;
    juce::TextButton barOneButton { "BAR 1 = PLAYHEAD" }, tapButton { "TAP" }, clearTempo { "NO TEMPO" };
    juce::ToggleButton clickOn { "Click" }, clickMaster { "In master outs" };
    juce::Slider clickLevel { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight },
                 ltcLevel   { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Array<double> taps;
    juce::StringArray inputNames;
    juce::BigInteger inputActive;
    juce::Array<juce::MidiDeviceInfo> midiOuts, midiIns;
    juce::Rectangle<int> tempoArea, clickArea, tcArea, chaseArea, statusArea;
    bool updating = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SyncPanel)
};

//==============================================================================
class SyncWindow : public juce::DocumentWindow
{
public:
    SyncWindow (SyncPanel::Page page, TapeEngine& e, std::function<Reel*()> gr, std::function<SyncPanel::Settings()> gs)
        : juce::DocumentWindow (page == SyncPanel::Page::tempo ? "Hawthorne - Tempo & Click" : "Hawthorne - Timecode",
                                Colours::panel, juce::DocumentWindow::closeButton),
          panel (page, e, std::move (gr), std::move (gs))
    {
        setUsingNativeTitleBar (true);
        setContentNonOwned (&panel, false);
        setResizable (false, false);
        centreWithSize (760, page == SyncPanel::Page::tempo ? 280 : 320);
    }
    ~SyncWindow() override { clearContentComponent(); }
    SyncPanel& getPanel() { return panel; }
    void closeButtonPressed() override { setVisible (false); }

private:
    SyncPanel panel;
};

} // namespace hawthorne
