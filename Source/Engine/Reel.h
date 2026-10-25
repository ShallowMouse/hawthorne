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

/** A stereo master mix recorded onto the reel, sample-aligned with its tracks. */
struct MasterInfo
{
    juce::String name;
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
                                            double sampleRate, int numTracks, juce::String& error,
                                            double ips = 15.0);

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

    /** Master mix files: Audio/Masters/<name> L.wav and R.wav. side 0 = L, 1 = R. */
    juce::File getMasterFile (int master, int side) const;

    /** Every tape file on this reel that exists (tracks and masters). */
    juce::Array<juce::File> getAllTapeFiles() const;

    /** Longest recorded track or master, in samples. */
    juce::int64 getReelLength() const;

    /** Pads every recorded track and master with silence up to the reel length.
        Returns the files that changed. */
    juce::Array<juce::File> equaliseTapeLengths (juce::String& error);

    // Masters
    int  addMaster (const juce::String& name);                     // returns its index
    bool renameMaster (int index, const juce::String& newName, juce::String& error);
    bool removeMaster (int index, juce::String& error);            // files go to the Recycle Bin
    juce::String uniqueMasterName (const juce::String& wanted) const;
    juce::String nextMasterName() const;                           // "Mix A", "Mix B" ...

    // Markers - always kept in time order
    int  addMarker (juce::int64 position, const juce::String& name);   // returns its index
    void sortMarkers();

    // Tape box label fields (per reel), in display order
    static const juce::StringArray& infoFields();

    /** The fields of an old single-reel file that belong to the session instead. */
    static const juce::StringArray& legacySessionFields();

    bool archived = false;     // hidden from the library shelves
    juce::uint32 colour = 0;   // tape box colour (ARGB), 0 = the session's

    double sampleRate = 48000.0;
    int    bitDepth   = 24;
    int    numTracks  = kDefaultTracks;
    std::array<TrackInfo, kMaxTracks> tracks;
    MasterRouting master;
    std::vector<MasterInfo> masters;
    int  activeMaster = -1;        // the master on the machine
    bool masterArmed = false;
    bool monitorMaster = false;    // hear the master tape instead of the live mix

    // Speed. The reel's speed is the one it was recorded at (fixed for life);
    // the deck can run it at another speed and/or varispeed, like a real machine.
    double ips = 15.0;             // 7.5, 15 or 30
    double deckIps = 15.0;
    bool   variOn = false;
    double variPercent = 0.0;      // -50 .. +50
    /** Tape speed relative to the reel's own: 1.0 = as recorded. */
    double getSpeedRatio() const noexcept
    {
        return (deckIps / ips) * (variOn ? 1.0 + variPercent / 100.0 : 1.0);
    }
    static juce::String formatIps (double v) { return v == 7.5 ? juce::String (juce::CharPointer_UTF8 ("7\xc2\xbd")) : juce::String ((int) v); }
    // Click and timecode (kept with the reel)
    bool   clickOn = false;
    double clickLevelDb = -12.0;
    ChannelList clickOutputs;          // device outputs for the click
    ChannelList ltcOutputs;            // device outputs for LTC timecode
    double ltcLevelDb = -10.0;
    int    tcRate = 1;                 // timecode::Rate (default 25)
    double tcStartSeconds = 0.0;       // timecode at the start of the tape (e.g. 01:00:00:00 = 3600)

    // Tempo (Hawthorne Sync). Set by a synced drum machine and kept with the reel,
    // so the drums land in the same place next time. bpm 0 = no tempo.
    double tempoBpm = 0.0;
    int    beatsPerBar = 4;
    double barOne = 0.0;           // tape position (reel samples) of bar 1's downbeat
    /** Bar and beat (both from 1) at a tape position; false if the reel has no tempo. */
    bool barBeatAt (juce::int64 pos, int& bar, int& beat) const noexcept
    {
        if (tempoBpm <= 0.0 || sampleRate <= 0.0) return false;
        const double beats = ((double) pos - barOne) / (sampleRate * 60.0 / tempoBpm);
        const auto b = (juce::int64) std::floor (beats + 1.0e-9);
        const int bpb = juce::jmax (1, beatsPerBar);
        auto barIdx = b >= 0 ? b / bpb : -((-b + bpb - 1) / bpb);
        bar = (int) (barIdx + (barIdx >= 0 ? 1 : 0));     // ... bar -1, bar 1, bar 2 ... (count-in bars are negative)
        beat = (int) (b - barIdx * bpb) + 1;
        return true;
    }
    /** "bar.beat.hundredths" (e.g. "12.3.50") at a tape position, or empty if the reel has no tempo. */
    juce::String bbtString (juce::int64 pos) const
    {
        int bar = 0, beat = 0;
        if (! barBeatAt (pos, bar, beat)) return {};
        const double beats = ((double) pos - barOne) / (sampleRate * 60.0 / tempoBpm);
        const int hundredths = juce::jlimit (0, 99, (int) std::floor ((beats - std::floor (beats + 1.0e-9)) * 100.0 + 1.0e-6));
        return juce::String (bar) + "." + juce::String (beat) + "." + juce::String (hundredths).paddedLeft ('0', 2);
    }

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
