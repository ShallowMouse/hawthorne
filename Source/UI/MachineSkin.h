#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace hawthorne
{

//==============================================================================
/**
    The look of the machine on the deck: the transport panel, its keys and
    counter, and the meter bridge, styled after the machine model chosen for
    the reel (MACHINE). The master meters follow the 2-track machine.
*/
struct MachineSkin
{
    enum class Keys    { square, cream, bakelite, illuminated, chrome, coloured, ivory };
    enum class Counter { redLed, greenVfd, amberLed, drum };
    enum class Meter   { vu, ledBar, ppm };

    const char* brand;          // badge on the deck
    const char* model;
    // deck
    juce::Colour deck, deckLow, deckEdge, deckText, deckTextDim;
    bool brushed;               // brushed-metal texture
    // keys
    Keys keys;
    juce::Colour key, keyText;
    // counter
    Counter counter;
    juce::Colour counterInk, counterBg;
    // meters
    Meter meter;
    juce::Colour faceTop, faceBottom, meterInk, meterRed, bezel, plate, plateText, needle;
    // meter bridge
    juce::Colour bridge, bridgeRow, bridgeEdge;
    // badge
    juce::Colour badge, badgeText;

    //--------------------------------------------------------------------------
    static const MachineSkin& multitrack (int i)
    {
        using C = juce::Colour;
        static const MachineSkin s[] = {
            // Studer A800: brushed-silver deck on an anthracite cabinet, dark keys with orange lamps,
            // red LED locator; anthracite meter bridge of amber-backlit VU meters.
            { "STUDER", "A800", C (0xffbfc2c3), C (0xffa3a6a8), C (0xff2d3033), C (0xff1b1d1f), C (0xff4c5055), true,
              Keys::square, C (0xff3a3d40), C (0xffe9ecef),
              Counter::redLed, C (0xffff3b2e), C (0xff120605),
              Meter::vu, C (0xfffbe3a2), C (0xffe8b04c), C (0xff1d1a14), C (0xffc0392b), C (0xff23262a), C (0xff1c1f22), C (0xffe9ecef), C (0xff1d1a14),
              C (0xff2e3134), C (0xff26292c), C (0xff45494d),
              C (0xff1b1d1f), C (0xffd8dcdf) },
            // MCI JH-24: warm taupe-brown panels, small colour-coded square keys, red LED autolocator;
            // taupe meter bridge of white-faced VU meters.
            { "MCI", "JH-24", C (0xff6f675c), C (0xff5c554b), C (0xff3f3a33), C (0xffefe9dc), C (0xffc4bba9), false,
              Keys::coloured, C (0xffe7e2d6), C (0xff221f1a),
              Counter::redLed, C (0xffff3326), C (0xff1a0907),
              Meter::vu, C (0xfffcfbf6), C (0xffeceadf), C (0xff15171a), C (0xffd0312d), C (0xff3f3a33), C (0xff4a443b), C (0xffefe9dc), C (0xff15171a),
              C (0xff6a6257), C (0xff5f584e), C (0xff4a443b),
              C (0xff2e2a24), C (0xffefe9dc) },
            // Ampex MM1200: off-white deck, illuminated push buttons, red LED timer;
            // black meter strip of warm, backlit VU meters.
            { "AMPEX", "MM1200", C (0xffe4e1d8), C (0xffd1cdc2), C (0xffa8a396), C (0xff222222), C (0xff6e6a60), false,
              Keys::illuminated, C (0xff2a2b2d), C (0xfff0f0ec),
              Counter::redLed, C (0xffff3b2e), C (0xff140605),
              Meter::vu, C (0xfffbe3a2), C (0xffe6ad48), C (0xff1d1a14), C (0xffb3261e), C (0xff141414), C (0xff1a1a1a), C (0xfff0e6cc), C (0xff1d1a14),
              C (0xff1a1a1a), C (0xff141414), C (0xff2e2e2e),
              C (0xff1a1a1a), C (0xffe4e1d8) },
            // Otari MTR-90: off-white cabinet, dark grey transport strip with amber-lamp keys, red LED
            // counter; black meter panel of warm, backlit VU meters.
            { "OTARI", "MTR-90", C (0xff3c3e41), C (0xff303235), C (0xffb9b6ad), C (0xffeceae4), C (0xffa4a29b), false,
              Keys::square, C (0xff505357), C (0xfff2f2f0),
              Counter::redLed, C (0xffff3b2e), C (0xff140605),
              Meter::vu, C (0xfffbe5a8), C (0xffe9b456), C (0xff1d1a14), C (0xffc0392b), C (0xff0f0f0f), C (0xff141414), C (0xffe9e6dc), C (0xff1d1a14),
              C (0xff111111), C (0xff0c0c0c), C (0xff2a2a2a),
              C (0xffdedbd2), C (0xff2a2a2a) },
            // Studer J37: light grey hammertone, ivory push keys with an orange record key and an
            // amber-lit key when engaged, mechanical counter. The J37 had no meters of its own -
            // Abbey Road read levels on the EMI desk's PPMs: black faces, white marks 1-7.
            { "STUDER", "J37", C (0xffa2a49e), C (0xff8e908a), C (0xff6c6e69), C (0xff1c1d1b), C (0xff4a4c48), false,
              Keys::ivory, C (0xfff1ead6), C (0xff1c1d1b),
              Counter::drum, C (0xfff3eedc), C (0xff121212),
              Meter::ppm, C (0xff151515), C (0xff0b0b0b), C (0xfff2f2ee), C (0xfff2f2ee), C (0xff5a5c58), C (0xff2a2b29), C (0xfff1ead6), C (0xfff5f5f0),
              C (0xff8e908a), C (0xff80827c), C (0xff6c6e69),
              C (0xff1c1d1b), C (0xffe6e0cc) },
            // 3M M23: pale grey, a dark blue-grey front panel, light grey keys, mechanical counter;
            // pale grey electronics drawers each with an amber-backlit VU meter.
            { "3M", "M23", C (0xff3e4f5b), C (0xff33424d), C (0xffb8bab6), C (0xffe9ecee), C (0xffa9b4bc), false,
              Keys::cream, C (0xffd9dbd8), C (0xff1e2226),
              Counter::drum, C (0xfff1f1ee), C (0xff101010),
              Meter::vu, C (0xfffbe3a2), C (0xffe6ad48), C (0xff1d1a14), C (0xffb3261e), C (0xff9a9c98), C (0xffb4b6b2), C (0xff1e2226), C (0xff1d1a14),
              C (0xffc6c7c3), C (0xffb4b6b2), C (0xff9a9c98),
              C (0xffc6c7c3), C (0xff1e2226) },
        };
        return s[juce::jlimit (0, 5, i)];
    }

    /** The master meters, after the 2-track machine. */
    static const MachineSkin& master (int i)
    {
        using C = juce::Colour;
        static const MachineSkin s[] = {
            // Ampex ATR-102: black meter panel, orange-backlit VU meters
            { "AMPEX", "ATR-102", C (0xffe2d9c2), C (0xffcfc5ab), C (0xff1a1a1a), C (0xff1a1a1a), C (0xff6a634f), false,
              Keys::cream, C (0xffeee3c6), C (0xff1a1a1a), Counter::redLed, C (0xffff3b2e), C (0xff120605),
              Meter::vu, C (0xfffcd78a), C (0xffeea23a), C (0xff1d1a14), C (0xffb3261e), C (0xff141414), C (0xff1a1a1a), C (0xffeee3c6), C (0xff1d1a14),
              C (0xff1a1a1a), C (0xff141414), C (0xff333333), C (0xff1a1a1a), C (0xffeee3c6) },
            // Studer A80: black modules, pale cream VU faces
            { "STUDER", "A80", C (0xffbfc2c3), C (0xffa3a6a8), C (0xff2d3033), C (0xff1b1d1f), C (0xff4c5055), true,
              Keys::square, C (0xff2f3337), C (0xffeef0f2), Counter::redLed, C (0xffff3b2e), C (0xff120605),
              Meter::vu, C (0xfffaf4dc), C (0xffece2bf), C (0xff15171a), C (0xffc0392b), C (0xff1c1e21), C (0xff1f2225), C (0xffeef0f2), C (0xff15171a),
              C (0xff222528), C (0xff1c1e21), C (0xff3a3e42), C (0xff1e2124), C (0xffeef0f2) },
            // Otari MTR-12: light grey, white-faced VU meters
            { "OTARI", "MTR-12", C (0xffd8d6d0), C (0xffc5c2bb), C (0xff9e9b93), C (0xff222222), C (0xff6e6a60), false,
              Keys::coloured, C (0xffe6e4de), C (0xff222222), Counter::redLed, C (0xffff3b2e), C (0xff140605),
              Meter::vu, C (0xfffcfbf6), C (0xffeceadf), C (0xff15171a), C (0xffd0312d), C (0xff3a3a3a), C (0xff2c2c2c), C (0xfff2f2f0), C (0xff15171a),
              C (0xffc5c2bb), C (0xffb5b2ab), C (0xff9e9b93), C (0xff2c2c2c), C (0xffdedbd2) },
            // Ampex 351: brushed-aluminium valve electronics, cream VU faces
            { "AMPEX", "351", C (0xffc4c5c1), C (0xffadaeaa), C (0xff1b1b1b), C (0xff1b1b1b), C (0xff4f504c), true,
              Keys::bakelite, C (0xff1b1a18), C (0xffeee6cf), Counter::drum, C (0xfff3eedc), C (0xff121212),
              Meter::vu, C (0xfff9e3ae), C (0xffe9bf62), C (0xff1d1a14), C (0xffa8231b), C (0xff9fa09c), C (0xff1b1b1b), C (0xffeee6cf), C (0xff1d1a14),
              C (0xff1b1b1b), C (0xff141414), C (0xff333333), C (0xff1b1b1b), C (0xffd9d9d4) },
            // EMI BTR2: EMI sage green, black-faced meter
            { "EMI", "BTR2", C (0xff8ea692), C (0xff7b927f), C (0xff5a6e5e), C (0xff1a1f1b), C (0xff3d4a40), false,
              Keys::bakelite, C (0xff1b1a18), C (0xffeee6cf), Counter::drum, C (0xfff3eedc), C (0xff121212),
              Meter::ppm, C (0xff151515), C (0xff0b0b0b), C (0xfff2f2ee), C (0xfff2f2ee), C (0xff2a2f2b), C (0xff1c201d), C (0xffe6e0cc), C (0xfff5f5f0),
              C (0xff7b927f), C (0xff6e8572), C (0xff5a6e5e), C (0xff1c201d), C (0xffd9e0d6) },
        };
        return s[juce::jlimit (0, 4, i)];
    }

    /** Meters: 0 = as the machine had them (auto), 1 = always VU, 2 = always PPM (SETTINGS). */
    static int& meterChoice() { static int m = 0; return m; }

    /** A skin with the meter style chosen in SETTINGS applied. */
    static MachineSkin forMeters (const MachineSkin& base)
    {
        auto s = base;
        const int choice = meterChoice();
        if (choice == 1 && s.meter != Meter::vu)
        {
            s.meter = Meter::vu;
            s.faceTop = juce::Colour (0xfff7e9bc); s.faceBottom = juce::Colour (0xffe6c56a);
            s.meterInk = juce::Colour (0xff1d1a14); s.meterRed = juce::Colour (0xffc0392b); s.needle = juce::Colour (0xff1d1a14);
        }
        else if (choice == 2 && s.meter != Meter::ppm)
        {
            s.meter = Meter::ppm;
            s.faceTop = juce::Colour (0xff151515); s.faceBottom = juce::Colour (0xff0b0b0b);
            s.meterInk = juce::Colour (0xfff2f2ee); s.meterRed = juce::Colour (0xfff2f2ee); s.needle = juce::Colour (0xfff5f5f0);
        }
        return s;
    }

    // The skins in use (set when the reel or its machines change)
    static const MachineSkin*& currentPtr()       { static const MachineSkin* p = &multitrack (0); return p; }
    static const MachineSkin*& currentMasterPtr() { static const MachineSkin* p = &master (0); return p; }
    static const MachineSkin& current()           { return *currentPtr(); }
    static const MachineSkin& currentMaster()     { return *currentMasterPtr(); }
    static void select (int multitrackMachine, int masterMachine)
    {
        currentPtr() = &multitrack (multitrackMachine);
        currentMasterPtr() = &master (masterMachine);
    }
};

} // namespace hawthorne
