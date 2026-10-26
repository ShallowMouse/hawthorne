#include "Session.h"

namespace hawthorne
{

namespace
{
    bool writeXmlAtomically (const juce::XmlElement& xml, const juce::File& target, juce::String& error)
    {
        target.getParentDirectory().createDirectory();
        juce::TemporaryFile temp (target);

        if (! xml.writeTo (temp.getFile()))
        {
            error = "Could not write " + target.getFileName() + " (disk full or read-only?).";
            return false;
        }

        if (target.existsAsFile())
            target.copyFileTo (target.withFileExtension (".bak"));

        if (! temp.overwriteTargetFileWithTemporary())
        {
            error = "Could not replace " + target.getFullPathName();
            return false;
        }
        return true;
    }

    juce::String reelFolderName (int n) { return "Reel " + juce::String (n).paddedLeft ('0', 2); }
}

//==============================================================================
const juce::StringArray& Session::infoFields()
{
    static const juce::StringArray fields { "Artist", "Title", "Client / Label", "Producer", "Engineer",
                                            "Tape Op", "Studio", "Job No.", "Notes" };
    return fields;
}

std::unique_ptr<Session> Session::create (const juce::File& library, const juce::String& name, juce::String& error)
{
    const auto cleanName = juce::File::createLegalFileName (name.trim());
    if (cleanName.isEmpty())
    {
        error = "Please give the session a name.";
        return nullptr;
    }

    const auto folder = library.getChildFile (cleanName);
    if (folder.exists())
    {
        error = "There's already a session called \"" + cleanName + "\" in the library.";
        return nullptr;
    }

    if (! folder.createDirectory())
    {
        error = "Could not create the session folder:\n" + folder.getFullPathName();
        return nullptr;
    }

    std::unique_ptr<Session> s (new Session());
    s->sessionFile = folder.getChildFile (cleanName + fileExtension);
    s->info.set ("Title", cleanName);

    if (! s->save (error))
        return nullptr;

    return s;
}

std::unique_ptr<Session> Session::load (const juce::File& file, juce::String& error)
{
    auto xml = juce::XmlDocument::parse (file);

    if (xml != nullptr && xml->hasTagName ("HawthorneSession") && xml->getChildByName ("Track") != nullptr)
    {
        // A v1.5-or-earlier single-reel session: convert it to a session holding Reel 01.
        juce::StringPairArray carried;
        if (! migrateLegacy (file, carried, error))
            return nullptr;

        std::unique_ptr<Session> s (new Session());
        s->sessionFile = file;
        s->info = carried;
        if (s->info["Title"].isEmpty())
            s->info.set ("Title", file.getFileNameWithoutExtension());
        s->log.add ("Converted \"" + s->getName() + "\" to the reel library format (it's now Reel 01).");

        if (! s->save (error))
            return nullptr;
        return s;
    }

    if (xml == nullptr || ! xml->hasTagName ("HawthorneProject"))
    {
        auto bak = file.withFileExtension (".bak");
        if (bak.existsAsFile())
            xml = juce::XmlDocument::parse (bak);

        if (xml == nullptr || ! xml->hasTagName ("HawthorneProject"))
        {
            error = "Not a Hawthorne session:\n" + file.getFullPathName();
            return nullptr;
        }
    }

    std::unique_ptr<Session> s (new Session());
    s->sessionFile = file;

    if (auto* ie = xml->getChildByName ("Info"))
        for (auto* f : ie->getChildWithTagNameIterator ("Field"))
            s->info.set (f->getStringAttribute ("name"), f->getStringAttribute ("value"));

    if (xml->hasAttribute ("colour"))
        s->colour = (juce::uint32) xml->getStringAttribute ("colour").getHexValue64();

    return s;
}

std::unique_ptr<Session> Session::openFolder (const juce::File& folder, juce::String& error)
{
    const auto named = folder.getChildFile (folder.getFileName() + fileExtension);
    if (named.existsAsFile())
        return load (named, error);

    for (auto& f : folder.findChildFiles (juce::File::findFiles, false, juce::String ("*") + fileExtension))
        return load (f, error);

    // No session file, but reels inside (e.g. interrupted conversion): recreate it.
    for (auto& d : folder.findChildFiles (juce::File::findDirectories, false))
        if (! d.findChildFiles (juce::File::findFiles, false, juce::String ("*") + Reel::fileExtension).isEmpty())
        {
            std::unique_ptr<Session> s (new Session());
            s->sessionFile = named;
            s->info.set ("Title", folder.getFileName());
            s->log.add ("Recreated the missing session file for \"" + folder.getFileName() + "\".");
            if (! s->save (error))
                return nullptr;
            return s;
        }

    error = "No session in " + folder.getFullPathName();
    return nullptr;
}

bool Session::save (juce::String& error)
{
    juce::XmlElement root ("HawthorneProject");
    root.setAttribute ("formatVersion", 1);
    if (colour != 0)
        root.setAttribute ("colour", juce::String::toHexString ((juce::int64) colour));

    auto* ie = root.createNewChildElement ("Info");
    for (auto& key : info.getAllKeys())
    {
        auto* f = ie->createNewChildElement ("Field");
        f->setAttribute ("name", key);
        f->setAttribute ("value", info[key]);
    }

    return writeXmlAtomically (root, sessionFile, error);
}

juce::Array<juce::File> Session::getReelFiles() const
{
    juce::Array<juce::File> reels;

    for (auto& d : getFolder().findChildFiles (juce::File::findDirectories, false))
    {
        const auto f = d.getChildFile (d.getFileName() + Reel::fileExtension);
        if (f.existsAsFile())
            reels.add (f);
    }

    struct ByName { int compareElements (const juce::File& a, const juce::File& b) const
                    { return a.getFileName().compareNatural (b.getFileName()); } } cmp;
    reels.sort (cmp);
    return reels;
}

std::unique_ptr<Reel> Session::createReel (int numTracks, double sampleRate, juce::String& error, double ips, int tapeStock)
{
    int n = 1;
    while (getFolder().getChildFile (reelFolderName (n)).exists())
        ++n;

    return Reel::create (getFolder(), reelFolderName (n), sampleRate, numTracks, error, ips, tapeStock);
}

//==============================================================================
bool Session::migrateLegacy (const juce::File& legacyFile, juce::StringPairArray& sessionInfo, juce::String& error)
{
    const auto folder   = legacyFile.getParentDirectory();
    const auto reelDir  = folder.getChildFile (reelFolderName (1));
    const auto reelFile = reelDir.getChildFile (reelFolderName (1) + Reel::fileExtension);

    if (reelDir.exists())
    {
        error = "Can't convert \"" + folder.getFileName() + "\": it already contains a \"Reel 01\" folder.";
        return false;
    }

    // Pull the shared fields out before moving anything.
    if (auto xml = juce::XmlDocument::parse (legacyFile))
        if (auto* ie = xml->getChildByName ("Info"))
            for (auto* f : ie->getChildWithTagNameIterator ("Field"))
                if (Reel::legacySessionFields().contains (f->getStringAttribute ("name")))
                    sessionInfo.set (f->getStringAttribute ("name"), f->getStringAttribute ("value"));

    // Renames within one folder: instant, and nothing is copied.
    if (! reelDir.createDirectory())
    {
        error = "Could not create " + reelDir.getFullPathName();
        return false;
    }

    const auto audio = folder.getChildFile ("Audio");
    if (audio.isDirectory() && ! audio.moveFileTo (reelDir.getChildFile ("Audio")))
    {
        error = "Could not move the Audio folder into Reel 01.";
        return false;
    }

    if (! legacyFile.moveFileTo (reelFile))
    {
        error = "Could not move the tape file into Reel 01.";
        return false;
    }

    const auto bak = legacyFile.withFileExtension (".bak");
    if (bak.existsAsFile())
        bak.moveFileTo (reelFile.withFileExtension (".bak"));

    return true;
}

} // namespace hawthorne
