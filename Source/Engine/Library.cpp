#include "Library.h"

namespace hawthorne
{

ReelSummary Library::summarise (const juce::File& reelFile)
{
    ReelSummary r;
    r.file = reelFile;
    r.name = reelFile.getFileNameWithoutExtension();
    r.modified = reelFile.getLastModificationTime();

    auto xml = juce::XmlDocument::parse (reelFile);
    if (xml == nullptr)
        return r;

    r.numTracks  = xml->getIntAttribute ("numTracks", kDefaultTracks);
    r.sampleRate = xml->getDoubleAttribute ("sampleRate", 48000.0);
    r.archived   = xml->getBoolAttribute ("archived", false);

    if (auto* ie = xml->getChildByName ("Info"))
        for (auto* f : ie->getChildWithTagNameIterator ("Field"))
            r.info.set (f->getStringAttribute ("name"), f->getStringAttribute ("value"));

    const auto audio = reelFile.getParentDirectory().getChildFile ("Audio");

    for (auto* te : xml->getChildWithTagNameIterator ("Track"))
    {
        const int t = te->getIntAttribute ("index", -1);
        if (! juce::isPositiveAndBelow (t, r.numTracks))
            continue;

        const auto name = te->getStringAttribute ("name");
        const auto tape = audio.getChildFile ("Track" + juce::String (t + 1).paddedLeft ('0', 2) + ".wav");
        const bool named = name.isNotEmpty() && name != "Track " + juce::String (t + 1);

        WavLayout l;
        if (tape.existsAsFile() && readWavLayout (tape, l))
            r.lengthSamples = juce::jmax (r.lengthSamples, l.headerFrames);

        if (named || tape.existsAsFile())
            r.trackNames.add (juce::String (t + 1).paddedLeft (' ', 2) + "  " + (name.isNotEmpty() ? name : "Track " + juce::String (t + 1)));
    }

    if (const auto v = xml->getDoubleAttribute ("ips"); v == 7.5 || v == 30.0)
        r.ips = v;

    if (auto* ms = xml->getChildByName ("Masters"))
        for (auto* e : ms->getChildWithTagNameIterator ("Mix"))
            r.masters.add (e->getStringAttribute ("name"));

    if (auto* tl = xml->getChildByName ("TakeLog"))
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
            r.takes.push_back (tk);
        }

    return r;
}

std::vector<SessionSummary> Library::scan (const juce::File& folder, juce::StringArray& log)
{
    std::vector<SessionSummary> sessions;
    folder.createDirectory();

    for (auto& dir : folder.findChildFiles (juce::File::findDirectories, false))
    {
        const bool hasSessionFile = ! dir.findChildFiles (juce::File::findFiles, false, juce::String ("*") + Session::fileExtension).isEmpty();
        bool hasReels = false;
        for (auto& d : dir.findChildFiles (juce::File::findDirectories, false))
            hasReels = hasReels || ! d.findChildFiles (juce::File::findFiles, false, juce::String ("*") + Reel::fileExtension).isEmpty();

        if (! hasSessionFile && ! hasReels)
            continue;

        juce::String err;
        auto s = Session::openFolder (dir, err);
        if (s == nullptr)
        {
            log.add (dir.getFileName() + ": " + err);
            continue;
        }
        log.addArray (s->log);

        SessionSummary sum;
        sum.file = s->getFile();
        sum.name = s->getName();
        sum.info = s->info;
        for (auto& rf : s->getReelFiles())
            sum.reels.push_back (summarise (rf));

        sessions.push_back (std::move (sum));
    }

    auto lastUsed = [] (const SessionSummary& s)
    {
        auto t = s.file.getLastModificationTime();
        for (auto& r : s.reels) t = juce::jmax (t, r.modified);
        return t;
    };
    std::sort (sessions.begin(), sessions.end(), [&] (auto& a, auto& b) { return lastUsed (a) > lastUsed (b); });
    return sessions;
}

bool Library::setArchived (const juce::File& reelFile, bool archived, juce::String& error)
{
    auto xml = juce::XmlDocument::parse (reelFile);
    if (xml == nullptr)
    {
        error = "Could not read " + reelFile.getFileName();
        return false;
    }

    xml->setAttribute ("archived", archived);

    juce::TemporaryFile temp (reelFile);
    if (! xml->writeTo (temp.getFile()) || ! temp.overwriteTargetFileWithTemporary())
    {
        error = "Could not update " + reelFile.getFileName();
        return false;
    }
    return true;
}

bool Library::deleteReel (const juce::File& reelFile, juce::String& error)
{
    const auto dir = reelFile.getParentDirectory();
    if (! dir.moveToTrash())
    {
        error = "Could not move " + dir.getFileName() + " to the Recycle Bin.";
        return false;
    }
    return true;
}

bool Library::deleteSession (const juce::File& sessionFile, juce::String& error)
{
    const auto dir = sessionFile.getParentDirectory();
    if (! dir.moveToTrash())
    {
        error = "Could not move " + dir.getFileName() + " to the Recycle Bin.";
        return false;
    }
    return true;
}

} // namespace hawthorne
