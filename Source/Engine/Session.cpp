#include "Session.h"
#include <juce_audio_formats/juce_audio_formats.h>

namespace hawthorne
{

namespace
{
    void initDefaultTracks (std::array<TrackInfo, kNumTracks>& tracks)
    {
        for (int i = 0; i < kNumTracks; ++i)
            tracks[(size_t) i] = { "Track " + juce::String (i + 1), i, false };
    }

    juce::uint32 readLE32 (const juce::uint8* p)
    {
        return (juce::uint32) p[0] | ((juce::uint32) p[1] << 8)
             | ((juce::uint32) p[2] << 16) | ((juce::uint32) p[3] << 24);
    }

    constexpr juce::int64 maxRiffBytes = 0xffffffffLL;
}

//==============================================================================
std::unique_ptr<Session> Session::create (const juce::File& parentFolder, const juce::String& name,
                                          double sampleRate, juce::String& error)
{
    const auto cleanName = juce::File::createLegalFileName (name.trim());

    if (cleanName.isEmpty())
    {
        error = "Invalid session name.";
        return nullptr;
    }

    auto folder = parentFolder.getChildFile (cleanName);

    if (folder.getChildFile (cleanName + fileExtension).existsAsFile())
    {
        error = "A session called \"" + cleanName + "\" already exists there.";
        return nullptr;
    }

    std::unique_ptr<Session> s (new Session());
    s->sessionFile = folder.getChildFile (cleanName + fileExtension);
    s->sampleRate  = sampleRate > 0 ? sampleRate : 48000.0;
    initDefaultTracks (s->tracks);

    if (! s->getAudioFolder().createDirectory())
    {
        error = "Could not create folder:\n" + s->getAudioFolder().getFullPathName();
        return nullptr;
    }

    if (! s->save (error))
        return nullptr;

    return s;
}

std::unique_ptr<Session> Session::load (const juce::File& file, juce::String& error)
{
    auto xml = juce::XmlDocument::parse (file);

    if (xml == nullptr || ! xml->hasTagName ("HawthorneSession"))
    {
        auto bak = file.withFileExtension (".bak");
        if (bak.existsAsFile())
            xml = juce::XmlDocument::parse (bak);

        if (xml == nullptr || ! xml->hasTagName ("HawthorneSession"))
        {
            error = "Not a valid Hawthorne session:\n" + file.getFullPathName();
            return nullptr;
        }
    }

    std::unique_ptr<Session> s (new Session());
    s->sessionFile = file;
    initDefaultTracks (s->tracks);

    if (! s->fromXml (*xml, error))
        return nullptr;

    s->getAudioFolder().createDirectory();

    // A crash mid-record leaves audio on disk beyond what the header says.
    for (int t = 0; t < kNumTracks; ++t)
    {
        const auto tape = s->getTapeFile (t);
        if (! tape.existsAsFile())
            continue;

        bool repaired = false;
        const auto frames = repairWavHeader (tape, repaired);

        if (frames < 0)
            s->recoveryLog.add ("Track " + juce::String (t + 1) + ": tape file is unreadable (" + tape.getFileName() + ")");
        else if (repaired)
            s->recoveryLog.add ("Track " + juce::String (t + 1) + ": tape restored after an unexpected shutdown ("
                                + juce::String ((double) frames / s->sampleRate, 1) + " s)");
    }

    return s;
}

//==============================================================================
std::unique_ptr<juce::XmlElement> Session::toXml() const
{
    auto root = std::make_unique<juce::XmlElement> ("HawthorneSession");
    root->setAttribute ("formatVersion", 2);
    root->setAttribute ("sampleRate", sampleRate);
    root->setAttribute ("bitDepth", bitDepth);

    for (int t = 0; t < kNumTracks; ++t)
    {
        const auto& tr = tracks[(size_t) t];
        auto* te = root->createNewChildElement ("Track");
        te->setAttribute ("index", t);
        te->setAttribute ("name", tr.name);
        te->setAttribute ("input", tr.input);
        te->setAttribute ("armed", tr.armed);
    }

    return root;
}

bool Session::fromXml (const juce::XmlElement& root, juce::String& error)
{
    sampleRate = root.getDoubleAttribute ("sampleRate", 48000.0);
    bitDepth   = 24;

    if (sampleRate < 8000.0 || sampleRate > 768000.0)
    {
        error = "Session has an invalid sample rate.";
        return false;
    }

    for (auto* te : root.getChildWithTagNameIterator ("Track"))
    {
        const int t = te->getIntAttribute ("index", -1);
        if (! juce::isPositiveAndBelow (t, kNumTracks))
            continue;

        auto& tr = tracks[(size_t) t];
        tr.name  = te->getStringAttribute ("name", tr.name);
        tr.input = te->getIntAttribute ("input", tr.input);
        tr.armed = te->getBoolAttribute ("armed", false);
    }

    return true;
}

bool Session::save (juce::String& error)
{
    auto xml = toXml();

    if (! getFolder().createDirectory())
    {
        error = "Session folder is not writable:\n" + getFolder().getFullPathName();
        return false;
    }

    juce::TemporaryFile temp (sessionFile);

    if (! xml->writeTo (temp.getFile()))
    {
        error = "Could not write session file (disk full or read-only?).";
        return false;
    }

    if (sessionFile.existsAsFile())
        sessionFile.copyFileTo (sessionFile.withFileExtension (".bak"));

    if (! temp.overwriteTargetFileWithTemporary())
    {
        error = "Could not replace session file:\n" + sessionFile.getFullPathName();
        return false;
    }

    return true;
}

//==============================================================================
bool readWavLayout (const juce::File& wav, WavLayout& l)
{
    l = {};
    const auto fileSize = wav.getSize();
    juce::FileInputStream in (wav);

    if (! in.openedOk() || fileSize < 44)
        return false;

    juce::uint8 hdr[12];
    if (in.read (hdr, 12) != 12)
        return false;

    const bool isRiff = memcmp (hdr, "RIFF", 4) == 0;
    l.rf64 = memcmp (hdr, "RF64", 4) == 0;

    if ((! isRiff && ! l.rf64) || memcmp (hdr + 8, "WAVE", 4) != 0)
        return false;

    while (in.getPosition() + 8 <= fileSize)
    {
        juce::uint8 ch[8];
        if (in.read (ch, 8) != 8)
            break;

        const auto size = (juce::int64) readLE32 (ch + 4);
        const auto bodyPos = in.getPosition();

        if (memcmp (ch, "fmt ", 4) == 0)
        {
            juce::uint8 fmt[16];
            if (in.read (fmt, 16) != 16)
                return false;
            l.formatTag     = (int) fmt[0] | ((int) fmt[1] << 8);
            l.numChannels   = (int) fmt[2] | ((int) fmt[3] << 8);
            l.sampleRate    = (double) readLE32 (fmt + 4);
            l.blockAlign    = (int) fmt[12] | ((int) fmt[13] << 8);
            l.bitsPerSample = (int) fmt[14] | ((int) fmt[15] << 8);
        }
        else if (memcmp (ch, "data", 4) == 0)
        {
            l.dataStart   = bodyPos;
            l.dataSizePos = bodyPos - 4;
            l.headerFrames = l.blockAlign > 0 ? size / l.blockAlign : 0;
            break;
        }

        if (! in.setPosition (bodyPos + size + (size & 1)))
            break;
    }

    return l.dataStart > 0 && l.blockAlign > 0;
}

juce::int64 repairWavHeader (const juce::File& wav, bool& wasRepaired)
{
    wasRepaired = false;
    WavLayout l;

    if (! readWavLayout (wav, l))
        return -1;

    if (l.rf64)
    {
        juce::WavAudioFormat fmt;
        if (auto* r = fmt.createReaderFor (new juce::FileInputStream (wav), true))
        {
            std::unique_ptr<juce::AudioFormatReader> reader (r);
            return reader->lengthInSamples;
        }
        return -1;
    }

    const auto actualData   = ((wav.getSize() - l.dataStart) / l.blockAlign) * l.blockAlign;
    const auto actualFrames = actualData / l.blockAlign;

    if (l.dataStart + actualData > maxRiffBytes)
        return l.headerFrames;

    if (l.headerFrames != actualFrames)
    {
        juce::FileOutputStream out (wav);

        if (out.openedOk())
        {
            out.setPosition (4);
            out.writeInt ((int) (juce::uint32) (l.dataStart + actualData - 8));
            out.setPosition (l.dataSizePos);
            out.writeInt ((int) (juce::uint32) actualData);
            out.flush();
            wasRepaired = ! out.getStatus().failed();
        }
    }

    return actualFrames;
}

//==============================================================================
bool TapeFile::createEmpty (const juce::File& f, double sampleRate, juce::String& error)
{
    f.getParentDirectory().createDirectory();
    juce::FileOutputStream out (f);

    if (! out.openedOk())
    {
        error = "Could not create tape file:\n" + f.getFullPathName();
        return false;
    }

    out.setPosition (0);
    out.truncate();

    const auto sr = (juce::uint32) juce::roundToInt (sampleRate);
    const int channels = 1, bits = 24, blockAlign = channels * bits / 8;

    out.write ("RIFF", 4);
    out.writeInt (36);
    out.write ("WAVEfmt ", 8);
    out.writeInt (16);
    out.writeShort (1);                                  // PCM
    out.writeShort ((short) channels);
    out.writeInt ((int) sr);
    out.writeInt ((int) (sr * (juce::uint32) blockAlign));
    out.writeShort ((short) blockAlign);
    out.writeShort ((short) bits);
    out.write ("data", 4);
    out.writeInt (0);
    out.flush();

    if (out.getStatus().failed())
    {
        error = "Could not write tape file:\n" + f.getFullPathName();
        return false;
    }

    return true;
}

bool TapeFile::open (const juce::File& f, double expectedSampleRate, juce::String& error)
{
    bool repaired = false;
    const auto frames = repairWavHeader (f, repaired);

    if (frames < 0 || ! readWavLayout (f, layout))
    {
        error = f.getFileName() + " is not a readable WAV file.";
        return false;
    }

    if (layout.rf64 || layout.formatTag != 1 || layout.numChannels != 1 || layout.bitsPerSample != 24
        || std::abs (layout.sampleRate - expectedSampleRate) > 0.5)
    {
        error = f.getFileName() + " is not a 24-bit mono tape at the session sample rate.";
        return false;
    }

    out = std::make_unique<juce::FileOutputStream> (f);

    if (! out->openedOk())
    {
        out.reset();
        error = "Could not open tape for writing:\n" + f.getFullPathName();
        return false;
    }

    length   = frames;
    position = 0;
    scratch.allocate ((size_t) scratchFrames * 3, true);
    return out->setPosition (layout.dataStart);
}

bool TapeFile::seek (juce::int64 frame)
{
    if (out == nullptr || frame < 0)
        return false;

    if (frame > length)
    {
        // Past the end of the tape: lay down silence up to the new position.
        if (! out->setPosition (layout.dataStart + length * 3))
            return false;
        position = length;
        return writeFrames (nullptr, frame - length);
    }

    position = frame;
    return out->setPosition (layout.dataStart + frame * 3);
}

bool TapeFile::write (const float* samples, int numSamples)
{
    return writeFrames (samples, numSamples);
}

bool TapeFile::writeSilence (juce::int64 numSamples)
{
    return writeFrames (nullptr, numSamples);
}

bool TapeFile::writeFrames (const float* src, juce::int64 numSamples)
{
    if (out == nullptr)
        return false;

    if (layout.dataStart + (position + numSamples) * 3 > maxRiffBytes)
        return false; // tape full (4 GB WAV limit)

    while (numSamples > 0)
    {
        const int n = (int) juce::jmin ((juce::int64) scratchFrames, numSamples);
        auto* d = reinterpret_cast<juce::uint8*> (scratch.get());

        for (int i = 0; i < n; ++i)
        {
            const float v = src != nullptr ? juce::jlimit (-1.0f, 1.0f, src[i]) : 0.0f;
            const int s = juce::roundToInt (v * 8388607.0f);
            d[i * 3]     = (juce::uint8) (s & 0xff);
            d[i * 3 + 1] = (juce::uint8) ((s >> 8) & 0xff);
            d[i * 3 + 2] = (juce::uint8) ((s >> 16) & 0xff);
        }

        if (! out->write (d, (size_t) n * 3))
            return false;

        if (src != nullptr)
            src += n;

        numSamples -= n;
        position   += n;
        length = juce::jmax (length, position);
    }

    return true;
}

bool TapeFile::flushHeader()
{
    if (out == nullptr)
        return false;

    const auto dataBytes = length * 3;
    const auto resume = out->getPosition();
    bool ok = out->setPosition (4);
    ok = ok && out->writeInt ((int) (juce::uint32) (layout.dataStart + dataBytes - 8));
    ok = ok && out->setPosition (layout.dataSizePos);
    ok = ok && out->writeInt ((int) (juce::uint32) dataBytes);
    ok = ok && out->setPosition (resume);
    out->flush();
    return ok && ! out->getStatus().failed();
}

bool TapeFile::close()
{
    if (out == nullptr)
        return true;

    const bool ok = flushHeader();
    out.reset();
    return ok;
}

} // namespace hawthorne
