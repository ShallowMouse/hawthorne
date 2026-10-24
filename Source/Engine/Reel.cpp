#include "Reel.h"
#include <juce_audio_formats/juce_audio_formats.h>

namespace hawthorne
{

namespace
{
    void initDefaultTracks (std::array<TrackInfo, kMaxTracks>& tracks)
    {
        for (int i = 0; i < kMaxTracks; ++i)
        {
            TrackInfo t;
            t.name  = "Track " + juce::String (i + 1);
            t.input = i;
            tracks[(size_t) i] = t;
        }
    }

    juce::uint32 readLE32 (const juce::uint8* p)
    {
        return (juce::uint32) p[0] | ((juce::uint32) p[1] << 8)
             | ((juce::uint32) p[2] << 16) | ((juce::uint32) p[3] << 24);
    }

    constexpr juce::int64 maxRiffBytes = 0xffffffffLL;
}

//==============================================================================
std::unique_ptr<Reel> Reel::create (const juce::File& parentFolder, const juce::String& name,
                                          double sampleRate, int numTracks, juce::String& error)
{
    const auto cleanName = juce::File::createLegalFileName (name.trim());

    if (cleanName.isEmpty())
    {
        error = "Invalid reel name.";
        return nullptr;
    }

    auto folder = parentFolder.getChildFile (cleanName);

    if (folder.getChildFile (cleanName + fileExtension).existsAsFile())
    {
        error = "A reel called \"" + cleanName + "\" already exists there.";
        return nullptr;
    }

    std::unique_ptr<Reel> s (new Reel());
    s->reelFile = folder.getChildFile (cleanName + fileExtension);
    s->sampleRate  = sampleRate > 0 ? sampleRate : 48000.0;
    s->numTracks   = juce::jlimit (1, kMaxTracks, numTracks);
    initDefaultTracks (s->tracks);
    s->info.set ("Reel No.", cleanName.retainCharacters ("0123456789").trimCharactersAtStart ("0"));
    s->info.set ("Date", juce::Time::getCurrentTime().formatted ("%d %b %Y"));

    if (! s->getAudioFolder().createDirectory())
    {
        error = "Could not create folder:\n" + s->getAudioFolder().getFullPathName();
        return nullptr;
    }

    if (! s->save (error))
        return nullptr;

    return s;
}

std::unique_ptr<Reel> Reel::load (const juce::File& file, juce::String& error)
{
    auto xml = juce::XmlDocument::parse (file);

    auto isReelXml = [] (const std::unique_ptr<juce::XmlElement>& x)
    {
        return x != nullptr && (x->hasTagName ("HawthorneReel") || x->hasTagName ("HawthorneSession"));
    };

    if (! isReelXml (xml))
    {
        auto bak = file.withFileExtension (".bak");
        if (bak.existsAsFile())
            xml = juce::XmlDocument::parse (bak);

        if (! isReelXml (xml))
        {
            error = "Not a valid Hawthorne reel:\n" + file.getFullPathName();
            return nullptr;
        }
    }

    std::unique_ptr<Reel> s (new Reel());
    s->reelFile = file;
    initDefaultTracks (s->tracks);

    if (! s->fromXml (*xml, error))
        return nullptr;

    s->getAudioFolder().createDirectory();

    // A crash mid-record leaves audio on disk beyond what the header says.
    for (int t = 0; t < kMaxTracks; ++t)
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

    juce::String padError;
    s->equaliseTapeLengths (padError);
    if (padError.isNotEmpty())
        s->recoveryLog.add (padError);

    return s;
}

//==============================================================================
const juce::StringArray& Reel::infoFields()
{
    static const juce::StringArray fields { "Reel No.", "Date", "Tape Stock", "Speed", "EQ",
                                            "Alignment", "Noise Reduction", "Tones", "Reel Notes" };
    return fields;
}

const juce::StringArray& Reel::legacySessionFields()
{
    static const juce::StringArray fields { "Artist", "Title", "Client / Label", "Producer", "Engineer",
                                            "Tape Op", "Studio", "Job No.", "Notes" };
    return fields;
}

int Reel::addMarker (juce::int64 position, const juce::String& name)
{
    markers.push_back ({ juce::jmax ((juce::int64) 0, position), name });
    sortMarkers();

    for (int i = (int) markers.size(); --i >= 0;)
        if (markers[(size_t) i].position == position && markers[(size_t) i].name == name)
            return i;
    return -1;
}

void Reel::sortMarkers()
{
    std::stable_sort (markers.begin(), markers.end(),
                      [] (const Marker& a, const Marker& b) { return a.position < b.position; });
}

juce::int64 Reel::getReelLength() const
{
    juce::int64 reel = 0;

    for (int t = 0; t < kMaxTracks; ++t)
    {
        WavLayout l;
        const auto f = getTapeFile (t);
        if (f.existsAsFile() && readWavLayout (f, l))
            reel = juce::jmax (reel, l.headerFrames);
    }

    return reel;
}

juce::Array<int> Reel::equaliseTapeLengths (juce::String& error)
{
    juce::Array<int> changed;
    const auto reel = getReelLength();

    for (int t = 0; t < kMaxTracks; ++t)
    {
        WavLayout l;
        const auto f = getTapeFile (t);

        if (! f.existsAsFile() || ! readWavLayout (f, l) || l.headerFrames >= reel)
            continue;

        TapeFile tape;
        juce::String err;
        if (tape.open (f, sampleRate, err) && tape.extendTo (reel) && tape.close())
            changed.add (t);
        else
            error = "Track " + juce::String (t + 1) + ": could not pad the tape to the reel length. " + err;
    }

    return changed;
}

//==============================================================================
std::unique_ptr<juce::XmlElement> Reel::toXml() const
{
    auto root = std::make_unique<juce::XmlElement> ("HawthorneReel");
    root->setAttribute ("formatVersion", 4);
    root->setAttribute ("archived", archived);
    root->setAttribute ("sampleRate", sampleRate);
    root->setAttribute ("bitDepth", bitDepth);
    root->setAttribute ("numTracks", numTracks);
    root->setAttribute ("nextTake", nextTake);

    for (int t = 0; t < kMaxTracks; ++t)
    {
        const auto& tr = tracks[(size_t) t];
        auto* te = root->createNewChildElement ("Track");
        te->setAttribute ("index", t);
        te->setAttribute ("name", tr.name);
        te->setAttribute ("input", tr.input);
        te->setAttribute ("armed", tr.armed);
        te->setAttribute ("toMix", tr.toMix);
        te->setAttribute ("outputs", channelsToString (tr.outputs));
        if (tr.instrument.isNotEmpty()) te->setAttribute ("instrument", tr.instrument);
        if (tr.mic.isNotEmpty())        te->setAttribute ("mic", tr.mic);
        if (tr.notes.isNotEmpty())      te->setAttribute ("notes", tr.notes);
    }

    auto* me = root->createNewChildElement ("Master");
    me->setAttribute ("inL",  channelsToString (master.inputs[0]));
    me->setAttribute ("inR",  channelsToString (master.inputs[1]));
    me->setAttribute ("outL", channelsToString (master.outputs[0]));
    me->setAttribute ("outR", channelsToString (master.outputs[1]));

    auto* ie = root->createNewChildElement ("Info");
    for (auto& key : info.getAllKeys())
    {
        auto* f = ie->createNewChildElement ("Field");
        f->setAttribute ("name", key);
        f->setAttribute ("value", info[key]);
    }

    auto* mk = root->createNewChildElement ("Markers");
    for (auto& m : markers)
    {
        auto* e = mk->createNewChildElement ("Marker");
        e->setAttribute ("pos", juce::String (m.position));
        e->setAttribute ("name", m.name);
    }

    auto* tl = root->createNewChildElement ("TakeLog");
    for (auto& tk : takeLog)
    {
        auto* e = tl->createNewChildElement ("Take");
        e->setAttribute ("take", tk.take);
        e->setAttribute ("when", tk.when);
        e->setAttribute ("start", juce::String (tk.start));
        e->setAttribute ("end", juce::String (tk.end));
        e->setAttribute ("tracks", tk.tracks);
        e->setAttribute ("song", tk.song);
        e->setAttribute ("status", tk.status);
        e->setAttribute ("comments", tk.comments);
    }

    return root;
}

bool Reel::fromXml (const juce::XmlElement& root, juce::String& error)
{
    sampleRate = root.getDoubleAttribute ("sampleRate", 48000.0);
    bitDepth   = 24;
    numTracks  = juce::jlimit (1, kMaxTracks, root.getIntAttribute ("numTracks", kDefaultTracks));
    archived   = root.getBoolAttribute ("archived", false);
    nextTake   = juce::jmax (1, root.getIntAttribute ("nextTake", 1));

    if (sampleRate < 8000.0 || sampleRate > 768000.0)
    {
        error = "Reel has an invalid sample rate.";
        return false;
    }

    for (auto* te : root.getChildWithTagNameIterator ("Track"))
    {
        const int t = te->getIntAttribute ("index", -1);
        if (! juce::isPositiveAndBelow (t, kMaxTracks))
            continue;

        auto& tr = tracks[(size_t) t];
        tr.name       = te->getStringAttribute ("name", tr.name);
        tr.input      = te->getIntAttribute ("input", tr.input);
        tr.armed      = te->getBoolAttribute ("armed", false);
        tr.toMix      = te->getBoolAttribute ("toMix", true);
        tr.outputs    = channelsFromString (te->getStringAttribute ("outputs"));
        tr.instrument = te->getStringAttribute ("instrument");
        tr.mic        = te->getStringAttribute ("mic");
        tr.notes      = te->getStringAttribute ("notes");
    }

    if (auto* me = root.getChildByName ("Master"))
    {
        master.inputs[0]  = channelsFromString (me->getStringAttribute ("inL"));
        master.inputs[1]  = channelsFromString (me->getStringAttribute ("inR"));
        master.outputs[0] = channelsFromString (me->getStringAttribute ("outL", "1"));
        master.outputs[1] = channelsFromString (me->getStringAttribute ("outR", "2"));
    }

    if (auto* ie = root.getChildByName ("Info"))
        for (auto* f : ie->getChildWithTagNameIterator ("Field"))
            info.set (f->getStringAttribute ("name"), f->getStringAttribute ("value"));

    if (auto* mk = root.getChildByName ("Markers"))
        for (auto* e : mk->getChildWithTagNameIterator ("Marker"))
            markers.push_back ({ e->getStringAttribute ("pos").getLargeIntValue(), e->getStringAttribute ("name") });
    sortMarkers();

    if (auto* tl = root.getChildByName ("TakeLog"))
        for (auto* e : tl->getChildWithTagNameIterator ("Take"))
        {
            TakeEntry tk;
            tk.take     = e->getIntAttribute ("take");
            tk.when     = e->getStringAttribute ("when");
            tk.start    = e->getStringAttribute ("start").getLargeIntValue();
            tk.end      = e->getStringAttribute ("end").getLargeIntValue();
            tk.tracks   = e->getStringAttribute ("tracks");
            tk.song     = e->getStringAttribute ("song");
            tk.status   = e->getStringAttribute ("status");
            tk.comments = e->getStringAttribute ("comments");
            takeLog.push_back (tk);
        }

    return true;
}

bool Reel::save (juce::String& error)
{
    auto xml = toXml();

    if (! getFolder().createDirectory())
    {
        error = "Reel folder is not writable:\n" + getFolder().getFullPathName();
        return false;
    }

    juce::TemporaryFile temp (reelFile);

    if (! xml->writeTo (temp.getFile()))
    {
        error = "Could not write the reel file (disk full or read-only?).";
        return false;
    }

    if (reelFile.existsAsFile())
        reelFile.copyFileTo (reelFile.withFileExtension (".bak"));

    if (! temp.overwriteTargetFileWithTemporary())
    {
        error = "Could not replace the reel file:\n" + reelFile.getFullPathName();
        return false;
    }

    return true;
}

//==============================================================================
juce::String channelsToString (const ChannelList& list)
{
    juce::StringArray parts;
    for (auto ch : list)
        parts.add (juce::String (ch + 1));
    return parts.joinIntoString (",");
}

ChannelList channelsFromString (const juce::String& s)
{
    ChannelList list;
    for (auto& p : juce::StringArray::fromTokens (s, ",", {}))
    {
        const int ch = p.trim().getIntValue() - 1;
        if (ch >= 0)
            list.addIfNotAlreadyThere (ch);
    }
    list.sort();
    return list;
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
        error = f.getFileName() + " is not a 24-bit mono tape at the reel's sample rate.";
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

    if (frame > length && ! extendTo (frame))   // past the end: lay down silence first
        return false;

    position = frame;
    return out->setPosition (layout.dataStart + frame * 3);
}

bool TapeFile::extendTo (juce::int64 frames)
{
    if (out == nullptr)
        return false;

    if (frames <= length)
        return true;

    if (layout.dataStart + frames * 3 > maxRiffBytes)
        return false;

    // Growing the file reads back as zeros (sparse on Linux, lazily zeroed on NTFS),
    // so even hours of padding are instant.
    if (! out->setPosition (layout.dataStart + frames * 3) || out->truncate().failed())
        return false;

    length = frames;
    position = frames;
    return flushHeader();
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
