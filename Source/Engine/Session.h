#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <vector>

namespace hawthorne
{

static constexpr int kNumTracks = 24;

//==============================================================================
/** A recorded take on one track.
    Covers timeline [start, start + length). Timeline sample p plays file sample
    (offset + p - start). Later regions in a track's list sit on top of earlier ones. */
struct Region
{
    juce::String file;          // relative to the session folder
    juce::int64  start   = 0;
    juce::int64  offset  = 0;
    juce::int64  length  = 0;
    int          take    = 0;
    bool         pending = false; // true while recording - used for crash recovery
};

struct TrackInfo
{
    juce::String        name;
    int                 input = -1;   // absolute device input channel, -1 = none
    bool                armed = false;
    std::vector<Region> regions;
};

//==============================================================================
/** Pure data model. Message thread only. */
class Session
{
public:
    static constexpr const char* fileExtension = ".hawthorne";

    /** Creates <parentFolder>/<name>/<name>.hawthorne plus an Audio folder. */
    static std::unique_ptr<Session> create (const juce::File& parentFolder,
                                            const juce::String& name,
                                            double sampleRate,
                                            juce::String& error);

    /** Loads a session and recovers any takes left pending by a crash. */
    static std::unique_ptr<Session> load (const juce::File& sessionFile, juce::String& error);

    /** Atomic save: writes a temp file, keeps a .bak of the previous version,
        then renames over the original. A crash mid-save can never corrupt it. */
    bool save (juce::String& error);

    juce::File getFile() const          { return sessionFile; }
    juce::File getFolder() const        { return sessionFile.getParentDirectory(); }
    juce::File getAudioFolder() const   { return getFolder().getChildFile ("Audio"); }
    juce::File resolve (const Region& r) const { return getFolder().getChildFile (r.file); }
    juce::String getName() const        { return sessionFile.getFileNameWithoutExtension(); }

    /** Returns a new, unused file path for a take on a track. */
    juce::File makeTakeFile (int track, int take) const;

    double sampleRate = 48000.0;
    int    bitDepth   = 24;
    int    nextTake   = 1;
    std::array<TrackInfo, kNumTracks> tracks;

    /** Filled by load(): human-readable notes on anything recovered/repaired. */
    juce::StringArray recoveryLog;

private:
    Session() = default;
    std::unique_ptr<juce::XmlElement> toXml() const;
    bool fromXml (const juce::XmlElement&, juce::String& error);
    void recoverPendingTakes();

    juce::File sessionFile;
};

//==============================================================================
/** If a WAV's header undercounts the audio actually on disk (crash before the
    final header write), rewrite the size fields from the real file length.
    Returns the number of sample frames in the file, or -1 if unreadable. */
juce::int64 repairWavHeader (const juce::File& wav, bool& wasRepaired);

} // namespace hawthorne
