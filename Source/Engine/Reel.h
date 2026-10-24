#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <vector>

namespace hawthorne
{

static constexpr int kMaxTracks     = 96;
static constexpr int kDefaultTracks = 24;

/** Absolute device channels (0-based), kept sorted. */
using ChannelList = juce::Array<int>;
juce::String channelsToString (const ChannelList&);      // "1,2,5" (1-based)
ChannelList  channelsFromString (const juce::String&);

struct TrackInfo
{
    juce::String name;
    int  input = -1;          // absolute device input channel, -1 = none
    bool armed = false;
    bool toMix = true;        // feeds the master bus
    ChannelList outputs;      // direct outs

    // Track sheet
    juce::String instrument, mic, notes;
};

/** The master is a stereo bus: the mix of the tracks plus any patched-in inputs. */
struct MasterRouting
{
    std::array<ChannelList, 2> inputs, outputs;
    MasterRouting() { outputs[0].add (0); outputs[1].add (1); }
};

struct Marker
{
    juce::int64 position = 0;
    juce::String name;
};

/** One line of the recording sheet - added automatically for every take. */
struct TakeEntry
{
    int take = 0;
    juce::String when;              // local date / time the take started
    juce::int64 start = 0, end = 0; // tape position
    juce::String tracks;            // e.g. "1-4, 7"
    juce::String song, status, comments;
};

//==============================================================================
/**
    A reel of multitrack tape (up to 96 tracks).

    Each track is ONE continuous mono WAV file (Audio/Track01.wav ...). Recording
    overwrites that file in place at the tape position - like tape there is no
    undo. Every recorded track file is kept the same length as the reel; unrecorded
    stretches are silence. Message thread only.
*/
class Reel
{
public:
    static constexpr const char* fileExtension = ".reel";
    static constexpr const char* legacyExtension = ".hawthorne";   // v1.5 and earlier: one reel per session

    /** Creates <parentFolder>/<name>/<name>.reel plus its Audio folder. The track count is fixed for life. */
    static std::unique_ptr<Reel> create (const juce::File& parentFolder, const juce::String& name,
                                            double sampleRate, int numTracks, juce::String& error);

    /** Loads a reel, repairs any tape left mid-write by a crash and evens up tape lengths. */
    static std::unique_ptr<Reel> load (const juce::File& reelFile, juce::String& error);

    /** Atomic save (temp file + rename, previous version kept as .bak). */
    bool save (juce::String& error);

    juce::File getFile() const          { return reelFile; }
    juce::File getFolder() const        { return reelFile.getParentDirectory(); }
    juce::File getAudioFolder() const   { return getFolder().getChildFile ("Audio"); }
    juce::String getName() const        { return reelFile.getFileNameWithoutExtension(); }
    juce::File getTapeFile (int track) const
    {
        return getAudioFolder().getChildFile ("Track" + juce::String (track + 1).paddedLeft ('0', 2) + ".wav");
    }

    /** Longest recorded track, in samples. */
    juce::int64 getReelLength() const;

    /** Pads every recorded track with silence up to the reel length.
        Returns the tracks whose files changed. */
    juce::Array<int> equaliseTapeLengths (juce::String& error);

    // Markers - always kept in time order
    int  addMarker (juce::int64 position, const juce::String& name);   // returns its index
    void sortMarkers();

    // Tape box label fields (per reel), in display order
    static const juce::StringArray& infoFields();

    /** The fields of an old single-reel file that belong to the session instead. */
    static const juce::StringArray& legacySessionFields();

    bool archived = false;     // hidden from the library shelves

    double sampleRate = 48000.0;
    int    bitDepth   = 24;
    int    numTracks  = kDefaultTracks;
    std::array<TrackInfo, kMaxTracks> tracks;
    MasterRouting master;
    std::vector<Marker> markers;
    juce::StringPairArray info;
    std::vector<TakeEntry> takeLog;
    int nextTake = 1;

    /** Filled by load(): notes on anything repaired after a crash. */
    juce::StringArray recoveryLog;

private:
    Reel() = default;
    std::unique_ptr<juce::XmlElement> toXml() const;
    bool fromXml (const juce::XmlElement&, juce::String& error);

    juce::File reelFile;
};

//==============================================================================
struct WavLayout
{
    juce::int64 dataStart = -1, dataSizePos = -1, headerFrames = 0;
    int blockAlign = 0, bitsPerSample = 0, numChannels = 0, formatTag = 0;
    double sampleRate = 0.0;
    bool rf64 = false;
};

/** Parses the RIFF chunk layout of a WAV file. */
bool readWavLayout (const juce::File&, WavLayout&);

/** If the header undercounts the audio on disk (crash before the header was
    rewritten), fix the size fields from the real file length.
    Returns the number of frames in the file, or -1 if unreadable. */
juce::int64 repairWavHeader (const juce::File& wav, bool& wasRepaired);

//==============================================================================
/**
    Random-access writer for a tape track: 24-bit mono PCM WAV that can be
    overwritten anywhere and extended. Not thread-safe; owned by one
    RecordingStream and used only on the disk thread / under its lock.
*/
class TapeFile
{
public:
    static bool createEmpty (const juce::File&, double sampleRate, juce::String& error);

    bool open (const juce::File&, double expectedSampleRate, juce::String& error);
    bool seek (juce::int64 frame);              // extends with silence if past the end
    bool extendTo (juce::int64 frames);         // pad with silence (fast: file is extended, not written)
    bool write (const float* samples, int numSamples);
    bool writeSilence (juce::int64 numSamples);
    bool flushHeader();
    bool close();

    juce::int64 getLength() const noexcept   { return length; }
    juce::int64 getPosition() const noexcept { return position; }

private:
    bool writeFrames (const float* samplesOrNull, juce::int64 numSamples);

    std::unique_ptr<juce::FileOutputStream> out;
    WavLayout layout;
    juce::int64 length = 0, position = 0;
    juce::HeapBlock<char> scratch;
    static constexpr int scratchFrames = 4096;
};

} // namespace hawthorne
