#pragma once

#include "TapeStock.h"

namespace hawthorne
{

//==============================================================================
/**
    Machines built by the user (tape::MachineParts), on disk.

    A reel that uses one keeps its own copy (Reel::customMachine / customMaster),
    so it plays back the same wherever it goes. The shelf of saved machines is a
    folder of small files that any reel can take one from.
*/
namespace tape
{
    struct NamedMachine
    {
        juce::String name;
        MachineParts parts;
        bool master = false;        // built for the 2-track
    };

    inline void partsToXml (juce::XmlElement& e, const MachineParts& p, const juce::String& name)
    {
        e.setAttribute ("name", name);
        e.setAttribute ("electronics", p.electronics);
        e.setAttribute ("trackWidth", p.trackWidth);
        e.setAttribute ("heads", p.heads);
        e.setAttribute ("contour", p.contour);
        e.setAttribute ("transport", p.transport);
        e.setAttribute ("look", p.look);
        e.setAttribute ("level", p.level);
        e.setAttribute ("colour", p.colour);
        e.setAttribute ("lowEnd", p.lowEnd);
        e.setAttribute ("topEnd", p.topEnd);
        e.setAttribute ("noise", p.noise);
        e.setAttribute ("wobble", p.wobble);
    }

    inline void partsFromXml (const juce::XmlElement& e, MachineParts& p, juce::String& name)
    {
        MachineParts d;
        name = e.getStringAttribute ("name");
        p.electronics = e.getIntAttribute ("electronics", d.electronics);
        p.trackWidth  = e.getIntAttribute ("trackWidth", d.trackWidth);
        p.heads       = e.getIntAttribute ("heads", d.heads);
        p.contour     = e.getIntAttribute ("contour", d.contour);
        p.transport   = e.getIntAttribute ("transport", d.transport);
        p.look        = e.getIntAttribute ("look", d.look);
        p.level       = e.getDoubleAttribute ("level", d.level);
        p.colour      = e.getDoubleAttribute ("colour", d.colour);
        p.lowEnd      = e.getDoubleAttribute ("lowEnd", d.lowEnd);
        p.topEnd      = e.getDoubleAttribute ("topEnd", d.topEnd);
        p.noise       = e.getDoubleAttribute ("noise", d.noise);
        p.wobble      = e.getDoubleAttribute ("wobble", d.wobble);
        p = tidy (p);
    }

    /** The shelf: Documents/Hawthorne/Machines, one file per machine. */
    struct MachineShelf
    {
        static juce::File& folderOverride() { static juce::File f; return f; }      // (tests)
        static juce::File folder()
        {
            if (folderOverride() != juce::File()) return folderOverride();
            return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Hawthorne").getChildFile ("Machines");
        }
        static juce::File fileFor (const juce::String& name, bool master)
        {
            return folder().getChildFile (juce::File::createLegalFileName (name.trim()) + (master ? ".2track" : ".multitrack") + ".hmachine");
        }

        static std::vector<NamedMachine> list (bool master)
        {
            std::vector<NamedMachine> out;
            auto files = folder().findChildFiles (juce::File::findFiles, false, master ? "*.2track.hmachine" : "*.multitrack.hmachine");
            files.sort();
            for (auto& f : files)
                if (auto xml = juce::parseXML (f))
                    if (xml->hasTagName ("HawthorneMachine"))
                    {
                        NamedMachine m;
                        partsFromXml (*xml, m.parts, m.name);
                        m.master = master;
                        if (m.name.isNotEmpty()) out.push_back (m);
                    }
            return out;
        }

        static bool save (const NamedMachine& m)
        {
            if (m.name.trim().isEmpty() || ! folder().createDirectory()) return false;
            juce::XmlElement xml ("HawthorneMachine");
            partsToXml (xml, m.parts, m.name.trim());
            xml.setAttribute ("for", m.master ? "2track" : "multitrack");
            return xml.writeTo (fileFor (m.name, m.master));
        }

        static bool remove (const juce::String& name, bool master) { return fileFor (name, master).deleteFile(); }
    };
}

} // namespace hawthorne
