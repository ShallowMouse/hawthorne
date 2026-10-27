#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Engine/Library.h"
#include "HawthorneLookAndFeel.h"

namespace hawthorne
{

//==============================================================================
/**
    The tape library (shown at launch). Sessions are groups of tape boxes on
    shelves; click a spine to pull the box out and read its label, double-click
    or LOAD REEL to put it on the deck.
*/
class LibraryView : public juce::Component
{
public:
    explicit LibraryView (juce::ApplicationProperties&);
    ~LibraryView() override;

    void refresh();                               // rescan the library folder
    void select (const juce::File& reelFile);

    std::function<void (const juce::File& reelFile)> onLoadReel;
    std::function<void()> onBackToDeck;
    std::function<juce::File()> getLoadedReel;    // reel currently on the deck (may be none)
    std::function<double()> getSampleRate;        // for new reels
    /** A reel's or session's box colour changed (file = .reel or session file) - keep the loaded copy in step. */
    std::function<void (const juce::File&, juce::Colour)> onColourChanged;

    juce::File getLibraryFolder() const;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    class Shelves;
    class BoxView;

    void updateSelection();
    void newSession();
    void newReel();
    void importRecordings();
    void loadSelected();
    void archiveSelected();
    void deleteSelected();
    void revealSelected();
    void chooseColour();
    void applyColour (const juce::File& file, juce::Colour);
    void changeFolder();
    void showError (const juce::String& title, const juce::String& message);

    const SessionSummary* selectedSession() const;
    const ReelSummary* selectedReel() const;
    bool isOnDeck (const juce::File& reelFile) const;

    juce::ApplicationProperties& props;
    std::vector<SessionSummary> sessions;
    juce::File selectedSessionFile, selectedReelFile;

    juce::TextEditor search;
    juce::ToggleButton showArchived { "Show archived" };
    juce::TextButton importButton { "IMPORT RECORDINGS..." };
    juce::TextButton newSessionButton { "NEW SESSION" }, backButton { "BACK TO DECK" }, folderButton { "CHANGE..." };
    juce::TextButton loadButton { "LOAD REEL" }, newReelButton { "NEW REEL" }, revealButton { "SHOW IN EXPLORER" },
                     archiveButton { "ARCHIVE" }, deleteButton { "DELETE" }, colourButton { "COLOUR" };

    std::unique_ptr<Shelves> shelves;
    juce::Viewport shelfView;
    std::unique_ptr<BoxView> box;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::Rectangle<int> topBar, folderArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LibraryView)
};

} // namespace hawthorne
