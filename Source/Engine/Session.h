#pragma once

#include "Reel.h"

namespace hawthorne
{

//==============================================================================
/**
    A session (an album, a client, a job) - a folder holding one or more reels.

        <Library>/<Session>/<Session>.hawthorne      shared session sheet
        <Library>/<Session>/Reel 01/Reel 01.reel     one multitrack tape each
        <Library>/<Session>/Reel 01/Audio/Track01.wav ...

    Sessions from v1.5 and earlier (one reel per session) are converted on load:
    the files are moved inside the same folder, nothing is copied or lost.
*/
class Session
{
public:
    static constexpr const char* fileExtension = ".hawthorne";

    static std::unique_ptr<Session> create (const juce::File& libraryFolder, const juce::String& name, juce::String& error);
    static std::unique_ptr<Session> load (const juce::File& sessionFile, juce::String& error);

    /** Opens the session in a folder, converting an old single-reel session or
        recreating a missing session file if needed. */
    static std::unique_ptr<Session> openFolder (const juce::File& folder, juce::String& error);

    bool save (juce::String& error);

    juce::File getFile() const    { return sessionFile; }
    juce::File getFolder() const  { return sessionFile.getParentDirectory(); }
    juce::String getName() const  { return sessionFile.getFileNameWithoutExtension(); }

    /** Reel files in this session, in reel order. */
    juce::Array<juce::File> getReelFiles() const;

    /** Adds the next reel ("Reel 02" ...). */
    std::unique_ptr<Reel> createReel (int numTracks, double sampleRate, juce::String& error, double ips = 15.0);

    static const juce::StringArray& infoFields();
    juce::StringPairArray info;

    /** Notes on anything converted or repaired when opening. */
    juce::StringArray log;

private:
    Session() = default;
    static bool migrateLegacy (const juce::File& legacyFile, juce::StringPairArray& sessionInfo, juce::String& error);

    juce::File sessionFile;
};

} // namespace hawthorne
