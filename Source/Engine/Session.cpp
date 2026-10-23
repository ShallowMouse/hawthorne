#include "Session.h"
#include <juce_audio_formats/juce_audio_formats.h>

namespace hawthorne
{

namespace
{
    void initDefaultTracks (std::array<TrackInfo, kNumTracks>& tracks)
    {
        for (int i = 0; i < kNumTracks; ++i)
        {
            tracks[(size_t) i].name  = "Track " + juce::String (i + 1);
            tracks[(size_t) i].input = i;
            tracks[(size_t) i].armed = false;
            tracks[(size_t) i].regions.clear();
        }
    }

    juce::uint32 readLE32 (const juce::uint8* p)
    {
        return (juce::uint32) p[0] | ((juce::uint32) p[1] << 8)
             | ((juce::uint32) p[2] << 16) | ((juce::uint32) p[3] << 24);
    }
}

//==============================================================================
std::unique_ptr<Session> Session::create (const juce::File& parentFolder,
                                          const juce::String& name,
                                          double sampleRate,
                                          juce::String& error)
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
    auto target = file;

    auto xml = juce::XmlDocument::parse (target);

    // Fall back to the backup if the main file is somehow unreadable.
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
    s->recoverPendingTakes();

    if (! s->recoveryLog.isEmpty())
    {
        juce::String saveErr;
        s->save (saveErr);
    }

    return s;
}

//==============================================================================
std::unique_ptr<juce::XmlElement> Session::toXml() const
{
    auto root = std::make_unique<juce::XmlElement> ("HawthorneSession");
    root->setAttribute ("formatVersion", 1);
    root->setAttribute ("sampleRate", sampleRate);
    root->setAttribute ("bitDepth", bitDepth);
    root->setAttribute ("nextTake", nextTake);

    for (int t = 0; t < kNumTracks; ++t)
    {
        const auto& tr = tracks[(size_t) t];
        auto* te = root->createNewChildElement ("Track");
        te->setAttribute ("index", t);
        te->setAttribute ("name", tr.name);
        te->setAttribute ("input", tr.input);
        te->setAttribute ("armed", tr.armed);

        for (const auto& r : tr.regions)
        {
            auto* re = te->createNewChildElement ("Region");
            re->setAttribute ("file", r.file);
            re->setAttribute ("start", juce::String (r.start));
            re->setAttribute ("offset", juce::String (r.offset));
            re->setAttribute ("length", juce::String (r.length));
            re->setAttribute ("take", r.take);
            if (r.pending)
                re->setAttribute ("pending", true);
        }
    }

    return root;
}

bool Session::fromXml (const juce::XmlElement& root, juce::String& error)
{
    sampleRate = root.getDoubleAttribute ("sampleRate", 48000.0);
    bitDepth   = root.getIntAttribute ("bitDepth", 24);
    nextTake   = juce::jmax (1, root.getIntAttribute ("nextTake", 1));

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

        for (auto* re : te->getChildWithTagNameIterator ("Region"))
        {
            Region r;
            r.file    = re->getStringAttribute ("file");
            r.start   = re->getStringAttribute ("start").getLargeIntValue();
            r.offset  = re->getStringAttribute ("offset").getLargeIntValue();
            r.length  = re->getStringAttribute ("length").getLargeIntValue();
            r.take    = re->getIntAttribute ("take");
            r.pending = re->getBoolAttribute ("pending", false);

            if (r.file.isNotEmpty())
                tr.regions.push_back (r);
        }
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

juce::File Session::makeTakeFile (int track, int take) const
{
    auto base = "T" + juce::String (track + 1).paddedLeft ('0', 2)
              + "_Take" + juce::String (take).paddedLeft ('0', 3);

    auto f = getAudioFolder().getChildFile (base + ".wav");

    for (int n = 2; f.exists(); ++n)
        f = getAudioFolder().getChildFile (base + "_" + juce::String (n) + ".wav");

    return f;
}

//==============================================================================
void Session::recoverPendingTakes()
{
    for (int t = 0; t < kNumTracks; ++t)
    {
        auto& regions = tracks[(size_t) t].regions;

        for (auto it = regions.begin(); it != regions.end();)
        {
            if (! it->pending)
            {
                ++it;
                continue;
            }

            const auto f = resolve (*it);
            bool repaired = false;
            const auto frames = f.existsAsFile() ? repairWavHeader (f, repaired) : (juce::int64) -1;

            if (it->start < 0 || frames <= it->offset)
            {
                recoveryLog.add ("Track " + juce::String (t + 1) + ": interrupted take "
                                 + juce::String (it->take) + " had no usable audio"
                                 + (f.existsAsFile() ? " (file kept: " + f.getFileName() + ")" : ""));
                it = regions.erase (it);
                continue;
            }

            it->length  = frames - it->offset;
            it->pending = false;

            recoveryLog.add ("Track " + juce::String (t + 1) + ": recovered interrupted take "
                             + juce::String (it->take) + " ("
                             + juce::String ((double) frames / sampleRate, 1) + " s"
                             + (repaired ? ", header repaired)" : ")"));
            ++it;
        }
    }
}

//==============================================================================
juce::int64 repairWavHeader (const juce::File& wav, bool& wasRepaired)
{
    wasRepaired = false;

    juce::int64 dataStart = -1, dataSizeField = 0, dataSizePos = -1;
    int blockAlign = 0;
    const auto fileSize = wav.getSize();

    {
        juce::FileInputStream in (wav);

        if (! in.openedOk() || fileSize < 44)
            return -1;

        juce::uint8 hdr[12];
        if (in.read (hdr, 12) != 12)
            return -1;

        const bool isRiff = memcmp (hdr, "RIFF", 4) == 0;
        const bool isRf64 = memcmp (hdr, "RF64", 4) == 0;

        if ((! isRiff && ! isRf64) || memcmp (hdr + 8, "WAVE", 4) != 0)
            return -1;

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
                    return -1;
                blockAlign = (int) fmt[12] | ((int) fmt[13] << 8);
            }
            else if (memcmp (ch, "data", 4) == 0)
            {
                dataStart     = bodyPos;
                dataSizeField = size;
                dataSizePos   = bodyPos - 4;
                break;
            }

            if (! in.setPosition (bodyPos + size + (size & 1)))
                break;
        }

        if (dataStart < 0 || blockAlign <= 0)
            return -1;

        // RF64 files (> 4 GB) keep their real size in the ds64 chunk; trust JUCE for those.
        if (isRf64)
        {
            juce::WavAudioFormat fmt;
            if (auto* r = fmt.createReaderFor (new juce::FileInputStream (wav), true))
            {
                std::unique_ptr<juce::AudioFormatReader> reader (r);
                return reader->lengthInSamples;
            }
            return -1;
        }
    }

    const auto actualData = ((fileSize - dataStart) / blockAlign) * blockAlign;
    const auto actualFrames = actualData / blockAlign;

    if (actualData > 0xffffffffLL)
        return dataSizeField / blockAlign; // too big for a plain RIFF header - leave it

    if (dataSizeField != actualData)
    {
        juce::FileOutputStream out (wav);

        if (out.openedOk())
        {
            const auto riffSize = (juce::uint32) juce::jmin ((juce::int64) 0xffffffff, dataStart + actualData - 8);
            out.setPosition (4);
            out.writeInt ((int) riffSize);
            out.setPosition (dataSizePos);
            out.writeInt ((int) (juce::uint32) actualData);
            out.flush();
            wasRepaired = ! out.getStatus().failed();
        }
    }

    return actualFrames;
}

} // namespace hawthorne
