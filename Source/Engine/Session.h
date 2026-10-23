#pragma once

#include <juce_core/juce_core.h>
#include <array>

namespace hawthorne
{

static constexpr int kNumTracks = 24;

struct TrackInfo
{
    juce::String name;
    int  input = -1;   // absolute device input channel, -1 = none
    bool armed = false;
};

//==============================================================================
/**
    A reel of 24-track tape.

    Each track is ONE continuous mono WAV file (Audio/Track01.wav ...). Recording
    overwrites that file in place at the tape position - exactly like tape there
    is no undo. Message thread only.
*/
class Session
{
public:
    static constexpr const char* fileExtension = ".hawthorne";

    /** Creates <parentFolder>/<name>/<name>.hawthorne plus an Audio folder. */
    static std::unique_ptr<Session> create (const juce::File& parentFolder, const juce::String& name,
                                            double sampleRate, juce::String& error);

    /** Loads a session and repairs any tape left mid-write by a crash. */
    static std::unique_ptr<Session> load (const juce::File& sessionFile, juce::String& error);

    /** Atomic save (temp file + rename, previous version kept as .bak). */
    bool save (juce::String& error);

    juce::File getFile() const          { return sessionFile; }
    juce::File getFolder() const        { return sessionFile.getParentDirectory(); }
    juce::File getAudioFolder() const   { return getFolder().getChildFile ("Audio"); }
    juce::String getName() const        { return sessionFile.getFileNameWithoutExtension(); }
    juce::File getTapeFile (int track) const
    {
        return getAudioFolder().getChildFile ("Track" + juce::String (track + 1).paddedLeft ('0', 2) + ".wav");
    }

    double sampleRate = 48000.0;
    int    bitDepth   = 24;
    std::array<TrackInfo, kNumTracks> tracks;

    /** Filled by load(): notes on anything repaired after a crash. */
    juce::StringArray recoveryLog;

private:
    Session() = default;
    std::unique_ptr<juce::XmlElement> toXml() const;
    bool fromXml (const juce::XmlElement&, juce::String& error);

    juce::File sessionFile;
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
