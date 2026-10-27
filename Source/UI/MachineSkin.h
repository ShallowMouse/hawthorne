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
    enum class Keys    { square, cream, bakelite, illuminated, chrome };
    enum class Counter { redLed, greenVfd, amberLed, drum };
    enum class Meter   { vu, ledBar };

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
            // Studer A800 / A827: anthracite, rubber keys with LEDs, red LED locator, LED bar meters
            { "STUDER", "A800", C (0xff2a2c2e), C (0xff1d1f21), C (0xff4a4d50), C (0xffe9ecef), C (0xff8d949a), false,
              Keys::square, C (0xff3a3d40), C (0xffe9ecef),
              Counter::redLed, C (0xffff3b2e), C (0xff120605),
              Meter::ledBar, C (0xff0c0d0e), C (0xff0c0d0e), C (0xffb9c2c9), C (0xffff3b2e), C (0xff1a1c1e), C (0xff15171a), C (0xffd0d6db), C (0xff000000),
              C (0xff222426), C (0xff16181a), C (0xff3f4245),
              C (0xff0f1011), C (0xffe9ecef) },
            // MCI JH-24: putty / beige, cream keys with coloured stripes, red LED autolocator, cream VUs
            { "MCI", "JH-24", C (0xffcfc6ae), C (0xffb8ae94), C (0xff8e8570), C (0xff2b261d), C (0xff6a614e), false,
              Keys::cream, C (0xffeee8d6), C (0xff2b261d),
              Counter::redLed, C (0xffff3326), C (0xff1a0907),
              Meter::vu, C (0xfffaf0c8), C (0xffeed58a), C (0xff1d1a14), C (0xffc0392b), C (0xff2b261d), C (0xff3a3328), C (0xffeee8d6), C (0xff1d1a14),
              C (0xffc4bba2), C (0xffb3a98e), C (0xff8e8570),
              C (0xff2b261d), C (0xffeee8d6) },
            // Ampex MM1200: blue-grey, black keys with lit labels, amber locator, white-faced VUs
            { "AMPEX", "MM1200", C (0xff5c666e), C (0xff4a535a), C (0xff3a4147), C (0xfff0f2f2), C (0xffb3bcc2), false,
              Keys::illuminated, C (0xff1e2124), C (0xfff0f2f2),
              Counter::amberLed, C (0xffffb020), C (0xff140d02),
              Meter::vu, C (0xfffdfcf6), C (0xffe9e6d8), C (0xff15171a), C (0xffd0312d), C (0xff1e2124), C (0xff2b3035), C (0xfff0f2f2), C (0xff15171a),
              C (0xff4c555c), C (0xff3f474d), C (0xff2d3338),
              C (0xff1e2124), C (0xffd9dee2) },
            // Otari MTR-90: warm off-white, big dark keys, green VFD locator, white-faced VUs
            { "OTARI", "MTR-90", C (0xffdedbd2), C (0xffc9c5b9), C (0xffa19c8f), C (0xff222222), C (0xff6e6a60), false,
              Keys::square, C (0xff3b3d3f), C (0xfff2f2f0),
              Counter::greenVfd, C (0xff5effc8), C (0xff031410),
              Meter::vu, C (0xfffafaf6), C (0xffeceade), C (0xff111111), C (0xffd8342a), C (0xff2a2a2a), C (0xff303030), C (0xfff2f2f0), C (0xff111111),
              C (0xff2c2d2e), C (0xff222324), C (0xff47494b),
              C (0xff2c2d2e), C (0xffdedbd2) },
            // Studer J37: grey-green hammertone, bakelite push buttons, mechanical counter, big cream VUs
            { "STUDER", "J37", C (0xff68736a), C (0xff535d55), C (0xff3d453f), C (0xffeef0e6), C (0xffb7bfb2), false,
              Keys::bakelite, C (0xff1b1a18), C (0xffeee6cf),
              Counter::drum, C (0xfff3eedc), C (0xff121212),
              Meter::vu, C (0xfff9e6b2), C (0xffe8bf5c), C (0xff1d1a14), C (0xffb3261e), C (0xff3d453f), C (0xff262b27), C (0xffeee6cf), C (0xff1d1a14),
              C (0xff5d685f), C (0xff4f5951), C (0xff3d453f),
              C (0xff2a2f2b), C (0xffd9cfaa) },
            // 3M M23: brushed aluminium, black keys, mechanical counter, black-faced VUs
            { "3M", "M23", C (0xffb9bab6), C (0xff9fa09b), C (0xff6f706c), C (0xff171717), C (0xff4f504c), true,
              Keys::chrome, C (0xff151515), C (0xfff1f1ee),
              Counter::drum, C (0xfff1f1ee), C (0xff101010),
              Meter::vu, C (0xff1c1c1c), C (0xff0e0e0e), C (0xffeeeeea), C (0xffff5a40), C (0xff2c2c2c), C (0xff111111), C (0xffeeeeea), C (0xfff5f5f0),
              C (0xff1f1f1f), C (0xff171717), C (0xff3a3a3a),
              C (0xff151515), C (0xffd8d8d4) },
        };
        return s[juce::jlimit (0, 5, i)];
    }

    /** The master meters, after the 2-track machine. */
    static const MachineSkin& master (int i)
    {
        using C = juce::Colour;
        static const MachineSkin s[] = {
            // Ampex ATR-102: cream VUs, dark grey
            { "AMPEX", "ATR-102", C (0xff3a3b3d), C (0xff2c2d2f), C (0xff4c4e50), C (0xffecebe6), C (0xff9c9a94), false,
              Keys::square, C (0xff2a2b2d), C (0xffecebe6), Counter::redLed, C (0xffff3b2e), C (0xff120605),
              Meter::vu, C (0xfffaefc4), C (0xffecd17e), C (0xff1d1a14), C (0xffc0392b), C (0xff1d1e20), C (0xff1a1b1d), C (0xffecebe6), C (0xff1d1a14),
              C (0xff2c2d2f), C (0xff222325), C (0xff444648), C (0xff1a1b1d), C (0xffecebe6) },
            // Studer A80: grey with white-faced VUs
            { "STUDER", "A80", C (0xff4d5257), C (0xff3d4246), C (0xff5e6368), C (0xffeef0f2), C (0xffa4abb1), false,
              Keys::square, C (0xff2f3337), C (0xffeef0f2), Counter::redLed, C (0xffff3b2e), C (0xff120605),
              Meter::vu, C (0xfffbfbf6), C (0xffe9e8dd), C (0xff15171a), C (0xffd0312d), C (0xff25292c), C (0xff1e2124), C (0xffeef0f2), C (0xff15171a),
              C (0xff3d4246), C (0xff32363a), C (0xff52575c), C (0xff1e2124), C (0xffeef0f2) },
            // Otari MTR-12: LED bars
            { "OTARI", "MTR-12", C (0xffdedbd2), C (0xffc9c5b9), C (0xffa19c8f), C (0xff222222), C (0xff6e6a60), false,
              Keys::square, C (0xff3b3d3f), C (0xfff2f2f0), Counter::greenVfd, C (0xff5effc8), C (0xff031410),
              Meter::ledBar, C (0xff0c0d0e), C (0xff0c0d0e), C (0xffb9c2c9), C (0xffff3b2e), C (0xff2a2a2a), C (0xff15171a), C (0xffd0d6db), C (0xff000000),
              C (0xff2c2d2e), C (0xff222324), C (0xff47494b), C (0xff2c2d2e), C (0xffdedbd2) },
            // Ampex 351: valve-era, deep amber-lit VUs
            { "AMPEX", "351", C (0xff4a4640), C (0xff3a3732), C (0xff5c574f), C (0xffefe6d2), C (0xffa89f8e), false,
              Keys::bakelite, C (0xff1b1a18), C (0xffeee6cf), Counter::drum, C (0xfff3eedc), C (0xff121212),
              Meter::vu, C (0xfff7dc98), C (0xffdca63e), C (0xff1d1a14), C (0xffa8231b), C (0xff2a2723), C (0xff221f1c), C (0xffeee6cf), C (0xff1d1a14),
              C (0xff3a3732), C (0xff302d29), C (0xff4c4842), C (0xff221f1c), C (0xffd9cfaa) },
            // EMI BTR2: EMI grey-green, big cream VUs
            { "EMI", "BTR2", C (0xff66706a), C (0xff535c56), C (0xff3d453f), C (0xffeef0e6), C (0xffb7bfb2), false,
              Keys::bakelite, C (0xff1b1a18), C (0xffeee6cf), Counter::drum, C (0xfff3eedc), C (0xff121212),
              Meter::vu, C (0xfff9e6b2), C (0xffe8bf5c), C (0xff1d1a14), C (0xffb3261e), C (0xff3d453f), C (0xff262b27), C (0xffeee6cf), C (0xff1d1a14),
              C (0xff5d685f), C (0xff4f5951), C (0xff3d453f), C (0xff2a2f2b), C (0xffd9cfaa) },
        };
        return s[juce::jlimit (0, 4, i)];
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
