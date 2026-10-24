#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Engine/Session.h"
#include "HawthorneLookAndFeel.h"

namespace hawthorne
{

/** The studio paperwork: reel sheet, track sheet and take log - typed on paper. */
namespace Paper
{
    const juce::Colour paper  { 0xfff1e8cf };
    const juce::Colour paperDark { 0xffe4d8b8 };
    const juce::Colour ink    { 0xff221f19 };
    const juce::Colour faint  { 0xff8a8170 };
    const juce::Colour rule   { 0xffa9bdd3 };
    const juce::Colour margin { 0xffc0392b };

    inline juce::Font type (float size, bool bold = false)
    {
        return juce::Font (juce::FontOptions ("Courier New", size, bold ? juce::Font::bold : juce::Font::plain));
    }
}

//==============================================================================
class SessionSheet : public juce::Component,
                     private juce::Timer
{
public:
    SessionSheet (std::function<Reel*()> getReel, std::function<Session*()> getSession);
    ~SessionSheet() override;

    /** Reload everything from the reel (new reel, new take, renamed track...). */
    void refresh();
    void showTakeLog();

    std::function<void()> onSave;                          // something was edited
    std::function<void()> onTrackNamesChanged;

    void resized() override;
    void paint (juce::Graphics&) override;

    class InfoPage;
    class TrackSheetPage;
    class TakeLogPage;

private:
    void markDirty();
    void timerCallback() override;

    std::function<Reel*()> getReel;
    std::function<Session*()> getSession;
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    InfoPage* infoPage = nullptr;
    InfoPage* reelPage = nullptr;
    TrackSheetPage* trackPage = nullptr;
    TakeLogPage* takePage = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SessionSheet)
};

//==============================================================================
class SessionSheetWindow : public juce::DocumentWindow
{
public:
    SessionSheetWindow (std::function<Reel*()> getReel, std::function<Session*()> getSession);
    ~SessionSheetWindow() override { clearContentComponent(); }
    SessionSheet& getSheet() { return sheet; }
    void closeButtonPressed() override { setVisible (false); }

private:
    SessionSheet sheet;
};

} // namespace hawthorne
