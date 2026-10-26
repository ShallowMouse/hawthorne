#pragma once

#include "Session.h"

namespace hawthorne
{

/** What the shelves need to know about a reel, read without loading or touching it. */
struct ReelSummary
{
    juce::File file;
    juce::String name;                // "Reel 01"
    int numTracks = 0;
    double sampleRate = 0.0;
    juce::int64 lengthSamples = 0;
    juce::StringPairArray info;       // tape box label fields
    juce::StringArray trackNames;     // recorded / named tracks, for the box
    juce::StringArray masters;        // master mixes on the reel
    double ips = 15.0;                // speed it was recorded at
    int tapeStock = 0;                // tape::Stock
    int machine = 0;                  // multitrack machine model
    juce::uint32 colour = 0;          // its own box colour (ARGB), 0 = the session's
    std::vector<TakeEntry> takes;
    bool archived = false;
    juce::Time modified;
};

struct SessionSummary
{
    juce::File file;
    juce::String name;
    juce::StringPairArray info;
    juce::uint32 colour = 0;          // ARGB, 0 = automatic
    std::vector<ReelSummary> reels;

    bool hasVisibleReels() const
    {
        for (auto& r : reels) if (! r.archived) return true;
        return false;
    }
};

/** The tape library: every session in one folder. */
class Library
{
public:
    /** Scans (and converts any old single-reel sessions it finds). Sessions are
        sorted most-recently-used first. Anything worth telling the user goes in log. */
    static std::vector<SessionSummary> scan (const juce::File& libraryFolder, juce::StringArray& log);

    static ReelSummary summarise (const juce::File& reelFile);

    /** Tape box colour of a reel or a whole session (file = the .reel or the session file);
        0 = back to the default. Edits only that attribute. */
    static bool setColour (const juce::File& reelOrSessionFile, juce::uint32 argb, juce::String& error);
    static bool setArchived (const juce::File& reelFile, bool archived, juce::String& error);
    /** Moves the reel's folder (tape + audio) to the Recycle Bin. */
    static bool deleteReel (const juce::File& reelFile, juce::String& error);
    /** Moves a whole session folder to the Recycle Bin. */
    static bool deleteSession (const juce::File& sessionFile, juce::String& error);

    static juce::File defaultFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Hawthorne");
    }
};

} // namespace hawthorne
