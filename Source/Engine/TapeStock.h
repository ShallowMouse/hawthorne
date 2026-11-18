#pragma once

#include <complex>

#include <juce_core/juce_core.h>
#include <array>
#include <atomic>
#include <cmath>
#include <string>

namespace hawthorne
{

//==============================================================================
/**
    Tape emulation.

    Chosen per reel when it's made (the stock), fixed for the life of the reel.

    RECORD (printed into the tape files, like real tape):
        record pre-emphasis (NAB or CCIR, by tape speed)
        -> magnetic saturation, 2x oversampled (soft knee set by the stock's
           headroom, a little asymmetry for even harmonics)
        -> playback de-emphasis (the exact inverse, so quiet signals come back
           flat; loud highs compress first, as on tape)
        -> modulation noise riding on the signal
       It adds a fixed latency which the recorder compensates, so tracks stay
       sample-aligned.

    PLAYBACK (live, from the tape files):
        head bump and the dip above it, low-end roll-off, high-frequency loss
        (all by the speed the tape is actually running at) and tape hiss.

    TRANSPORT: wow, flutter and scrape flutter - on the transport playing back, printed by the record head recording.
*/
namespace tape
{
    // Saved by number: only ever add to the end.
    enum class Stock { clean = 0, us456, highOutput, vintage, european, emitape, mastering499, sm900, emi888, emi811, emi815, scotch111, basfLgs };
    constexpr int numStocks = 13;
    /** The order they're offered in: oldest first (clean goes last). */
    inline constexpr Stock menuOrder[] = { Stock::scotch111, Stock::basfLgs, Stock::emi888, Stock::emitape, Stock::emi811, Stock::emi815, Stock::vintage,
                                          Stock::us456, Stock::european, Stock::mastering499, Stock::highOutput, Stock::sm900 };
    /** Where each tape came from, and when (oldest first is menuOrder). */
    inline const char* era (Stock s)
    {
        switch (s)
        {
            case Stock::scotch111:    return "3M Scotch  -  USA  -  1948";
            case Stock::basfLgs:      return "BASF  -  Germany  -  1950s";
            case Stock::emi888:       return "EMI  -  England  -  early 1960s";
            case Stock::emitape:      return "EMI  -  England  -  1963 to 1965";
            case Stock::emi811:       return "EMI  -  England  -  1966 to 1969";
            case Stock::emi815:       return "EMI  -  England  -  early 1970s";
            case Stock::vintage:      return "3M Scotch  -  USA  -  early 1970s";
            case Stock::us456:        return "Ampex  -  USA  -  1975";
            case Stock::european:     return "BASF  -  Germany  -  1986";
            case Stock::mastering499: return "Ampex  -  USA  -  early 1990s";
            case Stock::highOutput:   return "Quantegy  -  USA  -  1998";
            case Stock::sm900:        return "BASF / Emtec  -  Germany  -  late 1990s";
            case Stock::clean:        return "Hawthorne  -  digital";
        }
        return "";
    }
    enum StockUse { forMultitrack = 1, forMaster = 2 };

    struct StockInfo
    {
        const char* name;
        const char* label;          // for the tape box
        const char* description;
        double headroomDb;          // above 0 VU where it reaches ~3 % distortion
        double asymmetry;           // even harmonics
        double modNoiseDb;          // modulation noise, relative to the signal
        double hissDb;              // hiss at 15 ips, relative to 0 VU
        double bumpDb;              // head bump at 15 ips
        double hfSoftDb;            // extra softening of the top end
        bool ccir;                  // European equalisation
        int use = forMultitrack;    // multitrack reels and/or the 2-track master
        double solGapDb = 2.0;      // at 15 ips: how far the 10 kHz saturation output (SOL10) sits below the 1 kHz MOL
    };

    inline const StockInfo& info (Stock s)
    {
        static const StockInfo stocks[numStocks] = {
            { "Clean (no tape)",          "CLEAN",         "No tape sound at all. Records bit-for-bit what comes in: no saturation, no EQ change, no hiss, no wow or flutter.",
              99.0, 0.0,   -200.0, -200.0, 0.0, 0.0, false, forMultitrack | forMaster },
            { "Ampex 456 Grand Master",  "AMPEX 456",   "Ampex's +6 tape of 1975, the studio standard for the next twenty years. Warm, fat and punchy. A strong low-end bump (around 60 Hz at 15 ips) and a slightly rounded top. Saturates fairly early and musically - push the levels for glue and thickness on drums and bass. Moderate hiss. The classic 70s / 80s rock sound.",
              9.5,  0.025, -60.0,  -67.0,  2.0, 0.5, false, forMultitrack, 2.0 },
            { "Quantegy GP9",             "QUANTEGY GP9",   "Quantegy's 1998 refinement of 3M's 996, among the last new analogue tapes. Clean, tight and modern, made for +9. Lots of headroom: you can hit it hard before it gets gritty, so it colours little. Smaller low bump, full top end, low hiss. Tape glue without obvious distortion.",
              11.0, 0.015, -57.0,  -68.0,  1.5, 0.0, false, forMultitrack, 1.5 },
            { "Scotch 250",               "SCOTCH 250",      "3M's Scotch 250, the high-output US studio tape of the early 70s, made for +6 - Abbey Road had moved to Scotch by about 1969. 3M rated it 78 dB of dynamic range, 12 dB more than Scotch 111. Big and warm: the biggest low bump of the modern tapes, a slightly rounded top, and a full-bodied break-up when pushed.",
              9.0,  0.035, -58.0,  -69.0,  2.5, 0.8, false, forMultitrack, 2.0 },
            { "BASF SM911",               "BASF SM 911",   "BASF's 1986 answer to 456, with a stable binder, made for +6. Smooth and polished: a little more headroom than 456, a firm but restrained low end and a silky top (European CCIR EQ). Refined rather than vibey.",
              10.5, 0.02,  -59.0,  -66.5,  1.2, 0.3, true, forMultitrack, 2.0 },
            { "EMI Emitape 77",           "EMITAPE 77",       "The Emitape the Beatles recorded on at Abbey Road from Please Please Me to Rubber Soul. Standard level: breaks up early and sweetly with plenty of even harmonics, a soft top, a round low end and audible hiss (CCIR EQ). Pair it with the J37 or the 3M M23 - and the BTR2 for the master.",
              5.5,  0.06,  -53.0,  -59.5,  2.3, 1.6, true, forMultitrack | forMaster, 4.0 },
            { "Ampex 499 Grand Master Gold", "AMPEX 499",   "Ampex's Grand Master Gold of the early 90s, mastering-grade US tape for the 2-track. Big headroom with a warm, solid low end and a clean top: glues the mix without obvious distortion. Low noise.",
              11.0, 0.02,  -56.5,  -67.0,  2.0, 0.3, false, forMaster, 1.5 },
            { "Emtec SM900",              "EMTEC SM 900",     "BASF's very high output mastering tape of the late 90s, made to take on 499 and 996. Huge headroom and the lowest noise: the cleanest tape print, a subtle firm low end and a full top. For loud, modern masters.",
              11.5, 0.012, -58.0,  -68.5,  1.6, 0.0, true, forMaster, 1.5 },
            { "EMI Emitape 888",      "EMITAPE 888",   "EMI's early-60s studio tape, made for Abbey Road. The dirtiest of the EMI tapes: lo-fi and grainy, breaking up early with gritty upper mids, a soft top, a round low end and plenty of hiss (CCIR EQ). Great on rock and on acoustic guitars and percussion.",
              5.0,  0.07,  -51.0,  -59.0,  2.4, 2.2, true, forMultitrack | forMaster, 4.5 },
            { "EMI Emitape 811",   "EMITAPE 811",   "The Beatles' tape from Revolver (1966) until Abbey Road moved to Scotch around 1969. Cleaner than the 888: a better top end and a little less distortion, still warm and sweet when pushed (CCIR EQ). Character without the grit - lovely on single instruments like piano.",
              6.5,  0.05,  -54.0,  -60.5,  2.2, 1.2, true, forMultitrack | forMaster, 3.5 },
            { "EMI Emitape 815",      "EMITAPE 815",   "EMI's early-70s tape: the cleanest of the three. A flatter top end and less distortion, with a rounder bass and natural highs (CCIR EQ). Smooth and controlled - the one to use when you want the EMI sound with the least colour.",
              8.5,  0.03,  -58.0,  -63.5,  2.0, 0.5, true, forMultitrack | forMaster, 2.5 },
            { "Scotch 111",               "SCOTCH 111",    "3M's acetate tape of 1948, the American studio standard through the 50s and into the 60s - machines of the day were lined up to it. Low output and early, grainy break-up, a dull, soft top, a thick low end and lots of hiss and modulation noise (NAB EQ). Pair it with the Ampex 300s and the 351.",
              5.0,  0.075, -48.0,  -61.0,  2.6, 2.6, false, forMultitrack | forMaster, 4.5 },
            { "BASF LGS",                 "BASF LGS",      "BASF's PVC-based standard tape of the 50s, from the home of the Magnetophon. Steadier and a touch cleaner than Scotch 111 - a smoother break-up and a little less hiss - with a soft, rounded top (CCIR EQ). Pair it with the Telefunken M5.",
              5.5,  0.055, -51.0,  -61.5,  2.2, 2.0, true, forMultitrack | forMaster, 4.0 },
        };
        return stocks[juce::jlimit (0, numStocks - 1, (int) s)];
    }

    /** Alignment: the operating level (dB over 185 nWb/m) and record bias (dB of overbias
        at 10 kHz) each stock is made for. Hawthorne's 0 VU is -18 dBFS. */
    inline int recommendedLevel (Stock s)
    {
        switch (s)
        {
            case Stock::us456:        return 6;
            case Stock::vintage:      return 6;     // Scotch 250 (3M's alignment bulletin: 370 nWb/m)
            case Stock::highOutput:   return 9;
            case Stock::european:     return 6;     // SM911
            case Stock::mastering499: return 9;
            case Stock::sm900:        return 9;
            case Stock::emi815:       return 3;
            default:                  return 0;     // standard-level tapes (Scotch 111, BASF LGS, Emitape 77 / 888 / 811) and clean
        }
    }
    inline double recommendedBias (Stock s)
    {
        switch (s)
        {
            case Stock::vintage:      return 2.5;
            case Stock::emitape:      return 2.0;
            case Stock::scotch111:    return 2.0;
            case Stock::basfLgs:      return 2.0;
            case Stock::emi888:       return 2.0;
            case Stock::emi811:       return 2.5;
            case Stock::highOutput:   return 3.5;
            case Stock::sm900:        return 3.5;
            default:                  return 3.0;
        }
    }
    /** Operating levels offered on the alignment panel, and their fluxivity. */
    inline int levelFluxivity (int levelDb) { return levelDb >= 12 ? 740 : levelDb >= 9 ? 520 : levelDb >= 6 ? 370 : levelDb >= 3 ? 260 : 185; }

    //==========================================================================
    /** Tape machine models: their electronics and heads colour the stock.
        Record side (headroom, asymmetry) is printed; repro side is live, so
        changing the machine changes how existing tape plays back. */
    struct MachineInfo
    {
        const char* name;
        const char* label;
        const char* description;
        double headroomDb;      // added to the stock's (the tape overloads long before the electronics, so this is small)
        double asymmetry;       // the electronics' even harmonics (valves the most)
        double bumpScale;       // head bump size
        double bumpFreq;        // head bump frequency
        double hfScale;         // high-frequency extension (corner multiplier)
        double hfSoftDb;        // added top-end softening (negative = brighter)
        double lowCut;          // low roll-off frequency multiplier
        double hissDb;          // added hiss
        double wowFlutter;      // wow and flutter, rms % at 15 ips (published specs; DIN peak-weighted figures / 1.5)
        double crosstalkDb = 0.0;   // added to the crosstalk between neighbouring tracks (see crosstalkDb())
    };

    /** How much of each neighbouring track a head picks up, at 1 kHz, against the track's own level:
        the narrower the tracks (the more of them on the tape), the more. About -50 dB on 24 tracks on
        2-inch - "a crosstalk figure of 50 dB can be reached in practice and is generally considered
        acceptable" (Revox's service manual); the 16-track version is better (sellers of Studer A800s);
        the home formats about -40 to -45 dB (Fostex, Sound on Sound). Below a few hundred hertz it
        rises: long wavelengths spread out past the track's edge (fringing) - see crosstalkLowBoostDb. */
    inline double crosstalkDb (int tracks, const MachineInfo& m)
    {
        const double base = tracks <= 2 ? -62.0 : tracks <= 4 ? -60.0 : tracks <= 8 ? -57.0 : tracks <= 16 ? -54.0 : tracks <= 24 ? -50.0 : -46.0;
        return base + m.crosstalkDb;
    }
    constexpr double crosstalkLowBoostDb = 9.0;
    /** Where the low-frequency rise begins: a wavelength, so it moves with the tape speed. */
    inline double crosstalkLowHz (double ips) { return 140.0 * juce::jlimit (3.0, 30.0, ips) / 15.0; }

    inline constexpr MachineInfo neutralMachine { "None", "", "", 0, 0, 1, 1, 1, 0, 1, 0, 0.03 };

    constexpr int customMachine = 63;                       // a machine built by the user (see CustomSlot)
    const MachineInfo& customMachineInfo (bool master);

    constexpr int numMultitrackMachines = 13;    // 6 is "None": bit-perfect (later machines are added after it)
    constexpr int noMultitrackMachine = 6;
    inline const MachineInfo& multitrackMachine (int i)
    {
        static const MachineInfo m[numMultitrackMachines] = {
            { "Studer A800 / A827", "STUDER A800", "Clean, punchy and tight: a firm, controlled low end, an extended top and a rock-steady transport. The modern studio standard.",
              0.0, 0.0,   1.0,  1.0,  1.1,  0.0,  0.9,  0.0, 0.027 },
            { "MCI JH-24", "MCI JH-24", "Bright and aggressive: forward upper mids, a lighter low end and a bit more grit. The 70s / 80s rock workhorse.",
              -1.0, 0.01, 0.8,  1.1,  1.15, -0.6, 1.1,  1.0, 0.027 },
            { "Ampex MM1200", "AMPEX MM1200", "Big and warm: the fattest low end and a soft, rounded top. Thick and forgiving.",
              -0.5, 0.015, 1.35, 0.9, 0.85, 0.8,  0.85, 1.5, 0.05 },
            { "Otari MTR-90", "OTARI MTR-90", "Neutral and solid: modern, honest and quiet, with a dependable transport. Colours the least.",
              1.0, -0.005, 0.9, 1.0,  1.0,  0.0,  1.0, -1.0, 0.033 },
            { "Studer J37 (EMI, valve 4-track)", "STUDER J37", "Abbey Road's 4-track from 1965 until 8-track arrived (Revolver, Sgt. Pepper). Valve electronics: thick, warm and slightly compressed, with soft valve grit, a gentle top and more hiss and wobble.",
              -1.0, 0.05, 1.3,  1.0,  0.8,  1.5,  1.1,  3.0, 0.06 },
            { "3M M23 (8-track)", "3M M23", "The Beatles' first 8-track at Abbey Road (1968-69, the White Album, Abbey Road). Early transistor: punchy and solid, a little darker and grainier than later machines.",
              -0.7, 0.025, 1.2, 1.0,  0.95, 0.6,  1.0,  1.5, 0.04 },
            { "None (bit-perfect)", "NO MACHINE", "No machine: nothing between you and the files. No tape sound, no hiss, no wow or flutter - bit-for-bit, whatever the tape on the reel.",
              0, 0, 1, 1, 1, 0, 1, 0, 0 },
            { "Scully 280 (8-track)", "SCULLY 280", "Scully's 1965 solid-state transport; the 1-inch 8-track came in 1966. Punchy and forward in the mids, a solid low end and a slightly soft top.",
              -1.0, 0.02, 1.15, 1.0, 0.95, 0.5,  1.0,  1.5, 0.07 },
            { "3M M79", "3M M79", "3M's 1972 2-inch 24-track with the Isoloop drive. Round and warm with smooth low mids and a steady, low-flutter transport.",
              -0.5, 0.012, 1.25, 0.95, 0.95, 0.4, 0.9,  1.0, 0.035 },
            { "Stephens 821B", "STEPHENS 821B", "John Stephens' capstanless, transformerless 24-track of 1974. Open and detailed: a deep, clean low end and an extended top.",
              0.5, 0.0,   1.1,  0.9,  1.2, -0.4,  0.8,  0.0, 0.03 },
            { "Tascam 80-8 (1/2\" 8-track)", "TASCAM 80-8", "TEAC's 1976 home-studio 8-track: eight narrow tracks on half-inch tape. Less headroom, a lighter low end, a softer top and more hiss.",
              -2.0, 0.02, 0.85, 1.1,  0.9,  0.8,  1.2,  4.0, 0.035 },
            { "Ampex 300-3 (1/2\" 3-track)", "AMPEX 300-3", "Ampex's valve 3-track of the mid 50s, on half-inch tape: thick, warm and compressed, with a dull top, a big low end, lots of hiss and an unsteady transport.",
              -1.0, 0.055, 1.35, 0.95, 0.75, 1.8,  1.1,  3.5, 0.12 },
            { "Ampex 300-8 (Sel-Sync 8-track)", "AMPEX 300-8", "The first Sel-Sync 8-track, on 1-inch tape (1957): valve electronics, warm and dense, soft on top with plenty of hiss and wobble.",
              -1.0, 0.05,  1.3,  0.95, 0.78, 1.6,  1.1,  3.0, 0.12 },
        };
        return i == customMachine ? customMachineInfo (false) : m[juce::jlimit (0, numMultitrackMachines - 1, i)];
    }

    constexpr int numMasterMachines = 12;        // 5 is "None": bit-perfect (later machines are added after it)
    constexpr int noMasterMachine = 5;
    inline const MachineInfo& masterMachine (int i)
    {
        static const MachineInfo m[numMasterMachines] = {
            { "Ampex ATR-102", "AMPEX ATR-102", "The classic mastering deck: a huge, deep low end, an open top and very low noise.",
              1.0, 0.005, 1.4,  0.85, 1.15, -0.2, 0.75, -1.5, 0.03 },
            { "Studer A80 / A820", "STUDER A80", "Smooth and detailed with a slightly silky top. Polished and refined.",
              0.5, 0.0,   1.0,  1.0,  1.05, 0.3,  0.9, -1.0, 0.027 },
            { "Otari MTR-12", "OTARI MTR-12", "Clean and neutral: an honest print of the mix.",
              1.0, 0.0,   0.85, 1.0,  1.0,  0.0,  1.0, -1.0, 0.03 },
            { "Ampex 351 (valve)", "AMPEX 351", "Vintage valve electronics: thick even-harmonic warmth, a softer top and more noise.",
              -1.0, 0.05, 1.2,  0.95, 0.75, 1.5,  0.9,  3.0, 0.12 },
            { "EMI BTR2 (valve)", "EMI BTR2", "EMI's valve 2-track: the Beatles' mono and stereo masters. Warm and round with gentle valve saturation and a soft top.",
              -0.7, 0.04, 1.25, 0.95, 0.85, 1.0,  0.9,  2.0, 0.1 },
            { "None (bit-perfect)", "NO MACHINE", "No machine: the master is recorded bit-for-bit, whatever its tape.",
              0, 0, 1, 1, 1, 0, 1, 0, 0 },
            { "Ampex AG-440", "AMPEX AG-440", "The solid-state Ampex of the late 60s and 70s, in studios and broadcasters everywhere. Warm and solid with a full low end.",
              0.0, 0.01,  1.25, 0.9,  1.0,  0.4,  0.85, 0.5, 0.05 },
            { "Scully 280", "SCULLY 280", "The 60s / 70s American 2-track for mastering and broadcast. Punchy and direct.",
              0.0, 0.015, 1.15, 1.0,  1.0,  0.3,  0.95, 0.5, 0.07 },
            { "Studer A810", "STUDER A810", "Studer's 80s microprocessor 2-track. Clean, precise and quiet: the tidiest print of the mix.",
              1.0, 0.0,   0.95, 1.0,  1.1, -0.1,  0.95, -1.5, 0.033 },
            { "Revox A77", "REVOX A77", "The home-studio reel-to-reel of the late 60s and 70s. A little less headroom and more hiss, with a friendly, slightly soft sound.",
              -1.5, 0.015, 0.95, 1.05, 0.9,  0.7,  1.1,  2.5, 0.04 },
            { "Ampex 300 (valve)", "AMPEX 300", "Ampex's 1949 studio recorder, the American standard of the early 50s. Valve warmth and grit, a dull top, a fat low end, plenty of hiss and some wobble.",
              -1.0, 0.055, 1.25, 0.95, 0.72, 1.8,  0.95, 3.5, 0.1 },
            { "Telefunken M5 (valve)", "TELEFUNKEN M5", "The German valve studio deck of 1954. Smoother and quieter than the American valve machines: a gentle, silky saturation and a soft top.",
              -0.7, 0.04,  1.1,  1.0,  0.85, 1.0,  1.0,  2.0, 0.08 },
        };
        return i == customMachine ? customMachineInfo (true) : m[juce::jlimit (0, numMasterMachines - 1, i)];
    }


    //==========================================================================
    /** A machine built by the user: parts chosen as a machine was put together, then trimmed.
        It is machine number customMachine (63) on a reel (multitrack and 2-track each have one), and
        the reel keeps its own copy of the parts, so it always plays back the same. */
    struct MachineParts
    {
        int electronics = 2;        // 0 valve, 1 early transistor, 2 discrete transistor, 3 op-amp
        int trackWidth = 1;         // 0 wide, 1 standard, 2 narrow, 3 very narrow
        int heads = 1;              // 0 wide gap, 1 standard, 2 narrow gap
        int contour = 1;            // 0 small bump, 1 medium, 2 large
        int transport = 2;          // 0 early, 1 synchronous motor and pinch roller, 2 servo capstan, 3 servo without a pinch roller
        int look = 0;               // which machine's faceplate it wears
        // trims
        double level = 0.0;         // headroom, dB (-3 .. +3)
        double colour = 1.0;        // even harmonics (0 .. 2)
        double lowEnd = 1.0;        // head bump (0.5 .. 1.5)
        double topEnd = 1.0;        // top-end extension (0.8 .. 1.2)
        double noise = 0.0;         // hiss, dB (-6 .. +6)
        double wobble = 1.0;        // wow and flutter (0.25 .. 3)

        bool operator== (const MachineParts& o) const noexcept
        {
            return electronics == o.electronics && trackWidth == o.trackWidth && heads == o.heads && contour == o.contour
                && transport == o.transport && look == o.look && level == o.level && colour == o.colour && lowEnd == o.lowEnd
                && topEnd == o.topEnd && noise == o.noise && wobble == o.wobble;
        }
    };

    struct PartChoice { const char* name; const char* note; };
    enum PartKind { partElectronics = 0, partTrackWidth, partHeads, partContour, partTransport, numPartKinds };
    inline const char* partKindName (int kind)
    {
        static const char* n[numPartKinds] = { "ELECTRONICS", "TRACK WIDTH", "HEADS", "HEAD CONTOUR", "TRANSPORT" };
        return n[juce::jlimit (0, numPartKinds - 1, kind)];
    }
    inline int numPartChoices (int kind) { static const int n[numPartKinds] = { 4, 4, 3, 3, 4 }; return n[juce::jlimit (0, numPartKinds - 1, kind)]; }
    inline const PartChoice& partChoice (int kind, int i)
    {
        static const PartChoice c[numPartKinds][4] = {
            { { "Valve",                 "The 1950s: valves, with transformers in and out. Thick, the most second harmonic, a soft top and the most hiss." },
              { "Early transistor",      "The mid 1960s: germanium and early silicon. Punchy and a little grainy, with some hiss." },
              { "Discrete transistor",   "The 1970s: discrete silicon and transformers. Warm and solid - the classic studio machine." },
              { "Op-amp, transformerless", "The 1980s: integrated circuits and no transformers. Clean, extended at both ends and quiet." } },
            { { "Wide",                  "Full-track mono, 3 tracks on half-inch, 4 on one-inch. The quietest: the most tape under each head." },
              { "Standard",              "2 tracks on quarter-inch, 8 on one-inch, 16 on two-inch." },
              { "Narrow",                "24 tracks on two-inch. A little more hiss." },
              { "Very narrow",           "8 tracks on half-inch, 16 on one-inch: the home-studio formats. The most hiss." } },
            { { "Wide gap",              "Early heads: the top rolls away soonest." },
              { "Standard",              "Flat to 20 kHz at 15 ips." },
              { "Narrow gap",            "Later, finer heads: the most extended top.", },
              { "", "" } },
            { { "Small",                 "A small, tight head bump, a little higher up: a lean, controlled low end." },
              { "Medium",                "The usual bump: about 60 Hz at 15 ips." },
              { "Large",                 "A big, deep head bump: the fattest low end." },
              { "", "" } },
            { { "Early",                 "A 1950s transport: the most wow and flutter (0.12 %)." },
              { "Synchronous motor",     "A mains-locked capstan motor and pinch roller, as through the 1960s (0.07 %)." },
              { "Servo capstan",         "A servo-controlled capstan, as from the 1970s (0.035 %)." },
              { "Servo, no pinch roller", "The steadiest: a large servo capstan with no pinch roller, or no capstan at all (0.022 %)." } },
        };
        return c[juce::jlimit (0, numPartKinds - 1, kind)][juce::jlimit (0, numPartChoices (kind) - 1, i)];
    }
    inline int& partOf (MachineParts& p, int kind)
    {
        switch (kind) { case partElectronics: return p.electronics; case partTrackWidth: return p.trackWidth; case partHeads: return p.heads;
                        case partContour: return p.contour; default: return p.transport; }
    }
    inline int partOf (const MachineParts& p, int kind) { return partOf (const_cast<MachineParts&> (p), kind); }

    /** The trims: name, range and resting value. */
    struct TrimInfo { const char* name; const char* tip; double lo, hi, rest; const char* unit; };
    constexpr int numTrims = 6;
    inline const TrimInfo& trimInfo (int i)
    {
        static const TrimInfo t[numTrims] = {
            { "HEADROOM",      "How hard the machine can be hit before it breaks up, against what its parts give.", -3.0, 3.0, 0.0, "dB" },
            { "COLOUR",        "Even harmonics from the electronics: 0 none, 1 what the parts give, 2 twice that.", 0.0, 2.0, 1.0, "x" },
            { "LOW END",       "The size of the head bump.", 0.5, 1.5, 1.0, "x" },
            { "TOP END",       "How far the top extends.", 0.8, 1.2, 1.0, "x" },
            { "HISS",          "More or less hiss than the parts give.", -6.0, 6.0, 0.0, "dB" },
            { "WOW & FLUTTER", "How unsteady the transport is, against what it was built as.", 0.25, 3.0, 1.0, "x" },
        };
        return t[juce::jlimit (0, numTrims - 1, i)];
    }
    inline double& trimOf (MachineParts& p, int i)
    {
        switch (i) { case 0: return p.level; case 1: return p.colour; case 2: return p.lowEnd; case 3: return p.topEnd; case 4: return p.noise; default: return p.wobble; }
    }
    inline double trimOf (const MachineParts& p, int i) { return trimOf (const_cast<MachineParts&> (p), i); }

    inline MachineParts tidy (MachineParts p)
    {
        for (int k = 0; k < numPartKinds; ++k) partOf (p, k) = juce::jlimit (0, numPartChoices (k) - 1, partOf (p, k));
        for (int i = 0; i < numTrims; ++i) trimOf (p, i) = juce::jlimit (trimInfo (i).lo, trimInfo (i).hi, trimOf (p, i));
        p.look = juce::jmax (0, p.look);
        return p;
    }

    /** What the parts add up to (name, label and description are the caller's). */
    inline MachineInfo buildMachine (const MachineParts& parts)
    {
        const auto p = tidy (parts);
        //                           headroom  asym   hfSoft  lowCut  hiss
        static const double el[4][5] = { { -1.0, 0.055, 1.6, 1.10,  3.0 },
                                         { -0.7, 0.025, 0.6, 1.00,  1.5 },
                                         { -0.3, 0.012, 0.3, 0.95,  0.5 },
                                         {  0.5, 0.0,   0.0, 0.85, -1.0 } };
        static const double widthHiss[4] = { -2.5, 0.0, 2.0, 4.5 };
        static const double gap[3] = { 0.75, 1.0, 1.15 };
        static const double bump[3][2] = { { 0.8, 1.1 }, { 1.0, 1.0 }, { 1.35, 0.9 } };
        static const double wf[4] = { 0.12, 0.07, 0.035, 0.022 };
        MachineInfo m = neutralMachine;
        const auto& e = el[p.electronics];
        m.headroomDb = e[0] + p.level;
        m.asymmetry  = juce::jmax (0.0, e[1] * p.colour + 0.01 * (p.colour - 1.0));
        m.hfSoftDb   = e[2] - (p.heads == 2 ? 0.3 : 0.0);
        m.lowCut     = e[3];
        m.hissDb     = e[4] + widthHiss[p.trackWidth] + p.noise;
        m.hfScale    = gap[p.heads] * p.topEnd;
        m.bumpScale  = bump[p.contour][0] * p.lowEnd;
        m.bumpFreq   = bump[p.contour][1];
        m.wowFlutter = wf[p.transport] * p.wobble;
        static const double widthXt[4] = { -4.0, 0.0, 3.0, 6.0 };   // (wide tracks have wide guard bands between them)
        m.crosstalkDb = widthXt[p.trackWidth];
        return m;
    }

    /** The two custom machines in use (multitrack, 2-track): set from the reel when it goes on, and when
        one is built or changed. The audio thread reads them through multitrackMachine / masterMachine. */
    struct CustomSlot
    {
        CustomSlot() { set ({}, {}); }

        void set (const MachineParts& p, const std::string& called)
        {
            const int next = 1 - active.load();
            auto& b = built[next];
            b.name = called.empty() ? std::string ("Custom machine") : called;
            b.label = b.name.substr (0, 18);
            for (auto& ch : b.label) ch = (char) std::toupper ((unsigned char) ch);
            const auto t = tidy (p);
            b.description = std::string ("Built to order: ") + partChoice (partElectronics, t.electronics).name + " electronics, "
                          + partChoice (partTrackWidth, t.trackWidth).name + " tracks, " + partChoice (partHeads, t.heads).name + " heads, a "
                          + partChoice (partContour, t.contour).name + " head bump and a " + partChoice (partTransport, t.transport).name + " transport.";
            for (size_t i = 16; i < b.description.size(); ++i) b.description[i] = (char) std::tolower ((unsigned char) b.description[i]);
            b.info = buildMachine (t);
            b.info.name = b.name.c_str();
            b.info.label = b.label.c_str();
            b.info.description = b.description.c_str();
            parts = t;
            active = next;
            ++version;
        }
        const MachineInfo& get() const noexcept { return built[active.load (std::memory_order_relaxed)].info; }

        MachineParts parts;
        std::atomic<int> version { 0 };

    private:
        struct Built { std::string name, label, description; MachineInfo info = neutralMachine; };
        Built built[2];
        std::atomic<int> active { 0 };
    };
    inline CustomSlot& customSlot (bool master) { static CustomSlot s[2]; return s[master ? 1 : 0]; }

    inline const MachineInfo& customMachineInfo (bool master) { return customSlot (master).get(); }

    inline int validMultitrack (int i) { return i == customMachine || (i >= 0 && i < numMultitrackMachines) ? i : 0; }
    inline int validMaster (int i)     { return i == customMachine || (i >= 0 && i < numMasterMachines) ? i : 0; }

    //==========================================================================
    /** The machines in the order they're offered: oldest first, no machine (digital) last. */
    inline const std::vector<int>& multitrackOrder()
    {
        static const std::vector<int> o { 11 /*Ampex 300-3 mid 50s*/, 12 /*Ampex 300-8 1957*/, 4 /*J37 1964*/, 7 /*Scully 280 1965*/, 5 /*3M M23 1966*/, 8 /*3M M79 1972*/,
                                          9 /*Stephens 821B 1974*/, 2 /*MM1200 1976*/, 10 /*Tascam 80-8 1976*/, 0 /*A800 1978*/,
                                          3 /*MTR-90 1979*/, 1 /*JH-24 1980*/, noMultitrackMachine };
        return o;
    }

    inline const std::vector<int>& masterOrder()
    {
        static const std::vector<int> o { 4 /*BTR2 late 1940s*/, 10 /*Ampex 300 1949*/, 11 /*Telefunken M5 1954*/, 3 /*Ampex 351 1958*/, 7 /*Scully 280 1965*/, 6 /*AG-440 1967*/, 9 /*Revox A77 1967*/,
                                          1 /*A80 1970*/, 0 /*ATR-102 1976*/, 8 /*A810 1982*/, 2 /*MTR-12 1984*/, noMasterMachine };
        return o;
    }


    //==========================================================================
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;

        static Biquad peak (double sr, double f, double q, double gainDb)
        {
            const double A = std::pow (10.0, gainDb / 40.0), w = juce::MathConstants<double>::twoPi * f / sr;
            const double alpha = std::sin (w) / (2.0 * q), c = std::cos (w);
            const double a0 = 1.0 + alpha / A;
            return { (1.0 + alpha * A) / a0, -2.0 * c / a0, (1.0 - alpha * A) / a0, -2.0 * c / a0, (1.0 - alpha / A) / a0 };
        }
        static Biquad highpass (double sr, double f, double q)
        {
            const double w = juce::MathConstants<double>::twoPi * f / sr, alpha = std::sin (w) / (2.0 * q), c = std::cos (w);
            const double a0 = 1.0 + alpha;
            return { (1.0 + c) / 2.0 / a0, -(1.0 + c) / a0, (1.0 + c) / 2.0 / a0, -2.0 * c / a0, (1.0 - alpha) / a0 };
        }
        static Biquad highShelf (double sr, double f, double gainDb)
        {
            const double A = std::pow (10.0, gainDb / 40.0), w = juce::MathConstants<double>::twoPi * f / sr;
            const double c = std::cos (w), alpha = std::sin (w) / 2.0 * std::sqrt (2.0), sq = 2.0 * std::sqrt (A) * alpha;
            const double a0 = (A + 1) - (A - 1) * c + sq;
            return { A * ((A + 1) + (A - 1) * c + sq) / a0, -2 * A * ((A - 1) + (A + 1) * c) / a0, A * ((A + 1) + (A - 1) * c - sq) / a0,
                     2 * ((A - 1) - (A + 1) * c) / a0, ((A + 1) - (A - 1) * c - sq) / a0 };
        }
        Biquad scaled (double g) const { auto c = *this; c.b0 *= g; c.b1 *= g; c.b2 *= g; return c; }
        /** First-order shelf (1 + s*t1) / (1 + s*t2), bilinear (exactly invertible by swapping t1/t2). */
        static Biquad firstOrder (double sr, double t1, double t2)
        {
            const double k = 2.0 * sr;
            const double a0 = 1.0 + k * t2;
            return { (1.0 + k * t1) / a0, (1.0 - k * t1) / a0, 0.0, (1.0 - k * t2) / a0, 0.0 };
        }
        /** The same shelf with its two corners (Hz) put exactly where they're asked for, whatever the sample rate. */
        static Biquad shelfAt (double sr, double zeroHz, double poleHz)
        {
            const auto t = [sr] (double f) { return 1.0 / (2.0 * sr * std::tan (juce::MathConstants<double>::pi * juce::jmin (f, 0.49 * sr) / sr)); };
            return firstOrder (sr, t (zeroHz), t (poleHz));
        }
        /** A head's top-end loss: two first-order roll-offs at fc (-6 dB there), each matched to the analogue
            curve at 0 Hz and at half the sample rate - so it measures the same at 44.1, 48 and 96 kHz, and fc
            may sit above half the sample rate. */
        static Biquad gapLoss (double sr, double fc)
        {
            const double p = std::exp (-juce::MathConstants<double>::twoPi * fc / sr);
            const double x = 0.5 * sr / fc, gN = 1.0 / std::sqrt (1.0 + x * x);
            const double r = juce::jmin (0.999, gN * (1.0 + p) / (1.0 - p));
            const double c = (1.0 - r) / (1.0 + r), g = (1.0 - p) / (1.0 + c);
            return { g * g, 2.0 * g * g * c, g * g * c * c, -2.0 * p, p * p };
        }
        double gainAt (double f, double sr) const
        {
            const std::complex<double> zi = std::polar (1.0, -juce::MathConstants<double>::twoPi * f / sr);
            return std::abs ((b0 + b1 * zi + b2 * zi * zi) / (1.0 + a1 * zi + a2 * zi * zi));
        }
    };

    struct BiquadState
    {
        double z1 = 0, z2 = 0;
        inline float process (const Biquad& c, float x) noexcept
        {
            const double y = c.b0 * x + z1;
            z1 = c.b1 * x - c.a1 * y + z2;
            z2 = c.b2 * x - c.a2 * y;
            return (float) y;
        }
        void reset() noexcept { z1 = z2 = 0; }
    };

    /** Fast per-track noise. */
    struct Noise
    {
        uint32_t s = 0x9e3779b9u;
        inline float next() noexcept
        {
            s ^= s << 13; s ^= s >> 17; s ^= s << 5;
            return (float) ((double) s * (2.0 / 4294967296.0) - 1.0);
        }
    };

    //==========================================================================
    /** Linear-phase half-band FIR for 2x oversampling. Integer latency (in base-rate samples). */
    class HalfBand
    {
    public:
        static constexpr int length = 63;                     // 4K + 3
        static constexpr int latency = (length - 1) / 2;      // up + down, at the base rate

        static const std::array<float, length>& coeffs()
        {
            static const std::array<float, length> c = []
            {
                std::array<float, length> h {};
                const int m = (length - 1) / 2;
                auto i0 = [] (double x) { double s = 1, t = 1; for (int k = 1; k < 40; ++k) { t *= (x / (2 * k)) * (x / (2 * k)); s += t; } return s; };
                const double beta = 8.0;
                for (int n = 0; n < length; ++n)
                {
                    const int k = n - m;
                    const double sinc = k == 0 ? 0.5 : std::sin (juce::MathConstants<double>::pi * k / 2.0) / (juce::MathConstants<double>::pi * k);
                    const double z = (double) k / m;
                    h[(size_t) n] = (float) (sinc * i0 (beta * std::sqrt (juce::jmax (0.0, 1.0 - z * z))) / i0 (beta));
                }
                return h;
            }();
            return c;
        }

        void reset() noexcept { up.fill (0.0f); down.fill (0.0f); upPos = downPos = 0; }

        /** One base-rate sample in -> two oversampled samples out. */
        inline void upsample (float x, float& y0, float& y1) noexcept
        {
            // zero-stuffed input: x, 0
            y0 = filter (up, upPos, 2.0f * x);
            y1 = filter (up, upPos, 0.0f);
        }

        /** Two oversampled samples in -> one base-rate sample out. */
        inline float downsample (float x0, float x1) noexcept
        {
            const float y = filter (down, downPos, x0);
            filter (down, downPos, x1);
            return y;
        }

    private:
        static inline float filter (std::array<float, 2 * length>& line, int& pos, float x) noexcept
        {
            line[(size_t) pos] = x;
            line[(size_t) pos + length] = x;
            const float* d = line.data() + pos + 1;   // oldest .. newest
            const auto& c = coeffs();
            float acc = 0.0f;
            for (int i = 0; i < length; ++i)
                acc += c[(size_t) (length - 1 - i)] * d[i];
            pos = pos + 1 == length ? 0 : pos + 1;
            return acc;
        }

        std::array<float, 2 * length> up {}, down {};
        int upPos = 0, downPos = 0;
    };

    /** The record head's flutter is a small moving delay: it sits this far back (samples) so it can move both ways. */
    constexpr int flutterDelay = 16;
    /** The record path's fixed delay, which the recorder takes off so tracks stay sample-aligned. */
    constexpr int recordLatency = HalfBand::latency + flutterDelay;

    /** A-weighting (0 dB at 1 kHz). */
    inline double aWeight (double f) noexcept
    {
        const double f2 = f * f;
        return 1.2589 * 12194.0 * 12194.0 * f2 * f2
             / ((f2 + 20.6 * 20.6) * std::sqrt ((f2 + 107.7 * 107.7) * (f2 + 737.9 * 737.9)) * (f2 + 12194.0 * 12194.0));
    }

    //==========================================================================
    /** Settings shared by every track for the current stock and speed. */
    struct Shared
    {
        Stock stock = Stock::clean;
        double ips = 15.0, sr = 48000.0;
        // record
        Biquad preEmph, deEmph, lfPre, lfDe, biasEq;
        float drive = 1.0f, bias = 0.0f, biasOut = 0.0f, modNoise = 0.0f, modCoef = 0.1f;
        // playback
        Biquad bump, dip, lowCut, hfLoss, hfSoft;
        float hiss = 0.0f;
        Biquad hissHp, hissEq;

        /** The playback equalisation's treble corner (Hz) for a tape speed: NAB 50 us at 7.5 and 15 ips,
            CCIR / IEC 35 us at 15 and 70 us at 7.5, AES 17.5 us at 30 ips for both, 90 us at 3.75. */
        static double eqCorner (bool ccir, double tapeIps) noexcept
        {
            const double us = tapeIps >= 22.0 ? 17.5 : tapeIps >= 11.0 ? (ccir ? 35.0 : 50.0) : tapeIps >= 5.5 ? (ccir ? 70.0 : 50.0) : 90.0;
            return 1.0e6 / (juce::MathConstants<double>::twoPi * us);
        }
        /** Hiss as it leaves the playback amplifier: flat through the mids, then rising with the amplifier's
            treble lift (from the equalisation's corner) until the tape's own short-wavelength loss levels it off. */
        static void hissShape (bool ccir, double tapeIps, double sampleRate, Biquad& hp, Biquad& eq)
        {
            hp = Biquad::highpass (sampleRate, ccir ? 50.0 : 80.0, 0.7);     // (NAB playback also cuts the very bottom)
            const double corner = eqCorner (ccir, tapeIps);
            eq = Biquad::shelfAt (sampleRate, corner, juce::jmax (1.2 * corner, 6400.0 * std::sqrt (tapeIps / 15.0)));
        }
        /** A-weighted level of white noise of +-1 through a pair of filters. */
        static double aWeightedNoise (const Biquad& f1, const Biquad& f2, const Biquad& f3, double sampleRate)
        {
            const int steps = 72;
            const double lo = std::log (20.0), hi = std::log (juce::jmin (0.5 * sampleRate, 22000.0));
            double sum = 0.0, last = 0.0, lastF = 0.0;
            for (int i = 0; i <= steps; ++i)
            {
                const double f = std::exp (lo + (hi - lo) * i / steps);
                const double g = f1.gainAt (f, sampleRate) * f2.gainAt (f, sampleRate) * f3.gainAt (f, sampleRate) * aWeight (f);
                if (i > 0) sum += 0.5 * (g * g + last) * (f - lastF);
                last = g * g; lastF = f;
            }
            return std::sqrt (sum / (0.5 * sampleRate) / 3.0);
        }

        bool isClean() const noexcept { return stock == Stock::clean; }

        static inline float sat (float u) noexcept { return u / std::sqrt (1.0f + u * u); }

        /** The curve's own figures, for calibrating it: a sine of amplitude a through sat() with the
            stock's lopsidedness b - the fundamental and third harmonic coming out. */
        static void curveHarmonics (double a, double b, double& h1, double& h3) noexcept
        {
            const int n = 256;
            double c1 = 0, s1 = 0, c3 = 0, s3 = 0;
            const double off = b / std::sqrt (1.0 + b * b);
            for (int i = 0; i < n; ++i)
            {
                const double ph = juce::MathConstants<double>::twoPi * i / n, u = a * std::sin (ph) + b;
                const double y = u / std::sqrt (1.0 + u * u) - off;
                s1 += y * std::sin (ph); c1 += y * std::cos (ph); s3 += y * std::sin (3 * ph); c3 += y * std::cos (3 * ph);
            }
            h1 = 2.0 * std::sqrt (s1 * s1 + c1 * c1) / n;
            h3 = 2.0 * std::sqrt (s3 * s3 + c3 * c3) / n;
        }
        /** The amplitude into sat() that gives 3 % third harmonic (the MOL), and how far the output can
            rise above that before it stops growing - measured, as SOL is, by driving it 30 dB harder (dB). */
        static void molPoint (double b, double& amp, double& ceilingDb, double h3Ratio = 0.03) noexcept
        {
            double lo = 0.05, hi = 3.0, h1 = 0, h3 = 0;
            for (int k = 0; k < 40; ++k)
            {
                const double m = 0.5 * (lo + hi);
                curveHarmonics (m, b, h1, h3);
                (h3 < h3Ratio * h1 ? lo : hi) = m;
            }
            amp = 0.5 * (lo + hi);
            curveHarmonics (amp, b, h1, h3);
            double hTop = 0, hTop3 = 0;
            curveHarmonics (amp * 31.6, b, hTop, hTop3);
            ceilingDb = 20.0 * std::log10 (hTop / h1);
        }

        /** levelOffset / biasOffset: the reel's alignment against what the stock is made for
            (hotter level = less headroom, less hiss; over-bias = darker, smoother; under-bias =
            brighter, grittier, noisier). */
        void set (Stock s, double tapeIps, double sampleRate, const MachineInfo& mc = neutralMachine,
                  double levelOffset = 0.0, double biasOffset = 0.0)
        {
            stock = s;
            ips = juce::jlimit (1.0, 60.0, tapeIps);
            sr = sampleRate;
            const auto& in = info (s);
            if (isClean()) return;

            // Record equalisation, as it meets the tape. The treble is boosted into the tape (and taken
            // out after), so the top saturates first: the boost at 10 kHz is set so each stock's 10 kHz
            // saturation output (SOL10) lands where its datasheet puts it against the 1 kHz MOL: the gap plus
            // how far the curve's ceiling sits over its 3 % point (molPoint). BASF's SM911 / SM900 sheets: the gap is about the same at 30 ips as at 15 (a
            // touch less) and 6 to 7.5 dB wider at 7.5 ips.
            const double d = biasOffset;
            const double biasHeadroom = d < 0.0 ? 2.0 * d : 0.6 * d - 0.15 * d * d;
            // Even harmonics come from the machine, not the tape (its curve is symmetrical): the electronics'
            // own lopsidedness, and a little from the stock for the DC it tends to be left with. Scaled so a
            // valve machine on standard tape gives about 0.3 % second harmonic at 0 VU, under the 1 % third.
            bias = (float) (0.1 * in.asymmetry + 0.15 * mc.asymmetry);
            biasOut = sat (bias);
            double molAmp = 0.5, ceilingDb = 7.0, h3Ratio = 0.03;
            const auto gainAt = [this] (const Biquad& c, double f)
            {
                const std::complex<double> z = std::polar (1.0, juce::MathConstants<double>::twoPi * f / sr);
                const auto zi = 1.0 / z;
                return std::abs ((c.b0 + c.b1 * zi + c.b2 * zi * zi) / (1.0 + c.a1 * zi + c.a2 * zi * zi));
            };
            for (int pass = 0; pass < 3; ++pass)
            {
            // (the MOL is read after the playback EQ, which takes the 3 kHz harmonic down further than
            //  the 1 kHz tone - so at the curve itself it's a little more than 3 %)
            molPoint (bias, molAmp, ceilingDb, h3Ratio);
            {
                const double gap = in.solGapDb + (ips >= 25.0 ? -0.5 : ips >= 12.0 ? 0.0 : ips >= 6.0 ? 7.0 : 11.0);
                // (less 2 dB, found by measuring it as the datasheets do - testTapeDatasheets holds it there)
                const double target = juce::jlimit (3.0, 22.0, gap + ceilingDb - 2.0);
                const double ratio = 10.0, w10 = juce::MathConstants<double>::twoPi * juce::jmin (10000.0, 0.4 * sr);
                auto boostDb = [&] (double t) { const double a = w10 * t; return 10.0 * std::log10 ((1.0 + a * a) / (1.0 + a * a / (ratio * ratio))); };
                double lo = 1.0e-7, hi = 1.0e-3;
                for (int k = 0; k < 60; ++k) { const double m = std::sqrt (lo * hi); (boostDb (m) < target ? lo : hi) = m; }
                const double t1 = std::sqrt (lo * hi);
                preEmph = Biquad::firstOrder (sr, t1, t1 / ratio);
                deEmph  = Biquad::firstOrder (sr, t1 / ratio, t1);     // exact inverse after it
            }
            h3Ratio = 0.03 * gainAt (preEmph, 3000.0) / gainAt (preEmph, 1000.0);
            }
            // NAB (7.5 and 15 ips) also lifts the bass on record by its 3180 us time constant (+3 dB at
            // 50 Hz, more below) and takes it out on playback, so the low end meets the tape hotter than
            // on a CCIR machine. Held to +14 dB at the very bottom.
            if (! in.ccir && ips < 25.0)
            {
                const double ta = 3180e-6, tb = 5.0 * ta;
                lfPre = Biquad::firstOrder (sr, ta, tb).scaled (tb / ta);
                lfDe  = Biquad::firstOrder (sr, tb, ta).scaled (ta / tb);
            }
            else
                lfPre = lfDe = Biquad {};

            // Saturation: 3 % third harmonic at 1 kHz at the stock's headroom above 0 VU (= -18 dBFS)
            const double peakAtHeadroom = std::pow (10.0, (-18.0 + in.headroomDb + mc.headroomDb - levelOffset + biasHeadroom) / 20.0) * std::sqrt (2.0);
            // (the record EQ's own gain at 1 kHz counts: the MOL is measured with it in)
            drive = (float) (molAmp / (peakAtHeadroom * gainAt (preEmph, 1000.0) * gainAt (lfPre, 1000.0)));
            // Modulation noise: the signal's own level wavering as the coating passes the head, so it sits
            // close around each note (a noise band under a kilohertz wide) and is gone when the signal stops.
            {
                const double a = 1.0 - std::exp (-juce::MathConstants<double>::twoPi * 800.0 / sr), r = 1.0 - a;
                modCoef = (float) a;
                const double own = std::sqrt (a * a * a * a * (1.0 + r * r) / std::pow (1.0 - r * r, 3.0) / 3.0);   // (the two smoothings' own rms)
                modNoise = (float) (std::pow (10.0, (in.modNoiseDb + (d < 0.0 ? -2.5 * d : 0.0)) / 20.0) / own);
            }
            biasEq = Biquad::highShelf (sr, juce::jmin (0.4 * sr, 6000.0 * ips / 15.0), -1.5 * d);   // printed: over-bias dulls the top

            // Playback: the head bump moves with the tape speed
            const double fb = 4.0 * ips * mc.bumpFreq;
            const double speedFactor = std::log2 (15.0 / ips);       // slower tape = bigger bump, earlier top loss
            bump   = Biquad::peak (sr, fb, 1.1, in.bumpDb * mc.bumpScale * (1.0 + 0.25 * speedFactor) * (1.0 + 0.04 * d));
            dip    = Biquad::peak (sr, fb * 2.2, 1.4, -0.4 * in.bumpDb * mc.bumpScale);
            // (at 30 ips the bump is up at 100 Hz or so, but the bass holds on below it: 40 Hz within 2 dB)
            lowCut = Biquad::highpass (sr, fb / 2.6 * mc.lowCut * (ips > 15.0 ? std::sqrt (15.0 / ips) : 1.0), 0.65);
            // The top: flat, then the head's loss - within 2 dB at 20 kHz at 15 ips on a modern machine, about
            // 16 kHz at 7.5 ips and past 20 kHz at 30 (Studer A810: 20 Hz - 20 kHz, 20 Hz - 16 kHz and 40 Hz -
            // 22 kHz, each +-2 dB; Ampex 300 / 351: to 15 kHz at 15 ips). The stock's softness is a gentle shelf.
            hfLoss = Biquad::gapLoss (sr, 44000.0 * mc.hfScale * std::pow (ips / 15.0, 0.4));
            hfSoft = Biquad::highShelf (sr, juce::jmin (0.4 * sr, 9000.0 * ips / 15.0), -0.3 * (in.hfSoftDb + mc.hfSoftDb));
            // Hiss: the stock's figure is its A-weighted level at 15 ips (with its own equalisation); other
            // speeds change both how much there is and its colour.
            {
                Biquad refHp, refEq;
                hissShape (in.ccir, 15.0, sr, refHp, refEq);
                hissShape (in.ccir, ips, sr, hissHp, hissEq);
                const double want = std::pow (10.0, (-18.0 + in.hissDb + mc.hissDb + 1.5 * speedFactor - levelOffset) / 20.0);
                hiss = (float) (want / aWeightedNoise (refHp, refEq, Biquad {}, sr));
            }
        }
    };

    /** The record side of one track (printed into the file). */
    struct RecordChannel
    {
        HalfBand os;
        BiquadState pre, de, lfPreSt, lfDeSt, biasSt;
        Noise noise;
        float mod = 0.0f, mod2 = 0.0f;
        std::array<float, 128> line {};      // the tape passing the record head, at the oversampled rate
        int linePos = 0;

        void reset (uint32_t seed) noexcept
        {
            os.reset(); pre.reset(); de.reset(); lfPreSt.reset(); lfDeSt.reset(); biasSt.reset();
            mod = mod2 = 0.0f; line.fill (0.0f); linePos = 0; noise.s = seed | 1u;
        }

        /** flutter: the transport's time error at the record head for each sample, in samples (see Flutter),
            or none for a steady tape. Either way the output is recordLatency samples late. */
        inline void process (const Shared& sh, const float* in, float* out, int n, const float* flutter = nullptr) noexcept
        {
            const float drive = sh.drive, inv = 1.0f / drive;
            for (int i = 0; i < n; ++i)
            {
                const float x = in != nullptr ? in[i] : 0.0f;
                const float e = pre.process (sh.preEmph, lfPreSt.process (sh.lfPre, x));
                float a, b;
                os.upsample (e, a, b);
                a = (Shared::sat (a * drive + sh.bias) - sh.biasOut) * inv;
                b = (Shared::sat (b * drive + sh.bias) - sh.biasOut) * inv;
                // onto the moving tape
                line[(size_t) linePos] = a;
                line[(size_t) ((linePos + 1) & 127)] = b;
                if (flutter == nullptr)
                {
                    a = line[(size_t) ((linePos - 2 * flutterDelay) & 127)];
                    b = line[(size_t) ((linePos + 1 - 2 * flutterDelay) & 127)];
                }
                else
                {
                    const float back = 2.0f * ((float) flutterDelay + juce::jlimit (-14.0f, 14.0f, flutter[i]));
                    const int whole = (int) back;
                    const float f = back - (float) whole;      // read between [whole] and [whole + 1] samples back
                    const float c0 = -f * (f - 1.0f) * (f - 2.0f) / 6.0f, c1 = (f + 1.0f) * (f - 1.0f) * (f - 2.0f) / 2.0f,
                                c2 = -(f + 1.0f) * f * (f - 2.0f) / 2.0f, c3 = (f + 1.0f) * f * (f - 1.0f) / 6.0f;
                    const auto at = [this] (int k) noexcept { return line[(size_t) (k & 127)]; };
                    const int p = linePos - whole;
                    a = c0 * at (p + 1) + c1 * at (p) + c2 * at (p - 1) + c3 * at (p - 2);
                    b = c0 * at (p + 2) + c1 * at (p + 1) + c2 * at (p) + c3 * at (p - 1);
                }
                linePos = (linePos + 2) & 127;
                float y = biasSt.process (sh.biasEq, lfDeSt.process (sh.lfDe, de.process (sh.deEmph, os.downsample (a, b))));
                mod += (noise.next() - mod) * sh.modCoef;
                mod2 += (mod - mod2) * sh.modCoef;
                out[i] = y + y * mod2 * sh.modNoise;
            }
        }
    };

    /** The playback side of one track. */
    struct PlaybackChannel
    {
        BiquadState bump, dip, lowCut, hfLoss, hfSoft, hissHp, hissEq;
        Noise noise;
        float last = 0.0f;

        void reset (uint32_t seed) noexcept
        {
            for (auto* s : { &bump, &dip, &lowCut, &hfLoss, &hfSoft, &hissHp, &hissEq }) s->reset();
            noise.s = seed | 1u;
            last = 0.0f;
        }

        /** scrape: the tape's fast shiver over the head for each sample, as a time error in samples (see
            Flutter) - far under a sample, so it is applied as that much of the signal's slope. */
        inline void process (const Shared& sh, float* io, int n, const float* scrape = nullptr) noexcept
        {
            for (int i = 0; i < n; ++i)
            {
                float y = io[i];
                if (scrape != nullptr) { const float x = y; y += scrape[i] * (x - last); last = x; }
                y = lowCut.process (sh.lowCut, y);
                y = bump.process (sh.bump, y);
                y = dip.process (sh.dip, y);
                y = hfSoft.process (sh.hfSoft, y);
                // (the hiss comes off the tape with the signal, so the head's top-end loss takes both)
                y += hissEq.process (sh.hissEq, hissHp.process (sh.hissHp, noise.next())) * sh.hiss;
                io[i] = hfLoss.process (sh.hfLoss, y);
            }
        }
    };

    /** A transport's wow and flutter. Everything turns with the tape speed, so the rates follow it:
          wow       the reels and the tape path, around 0.6 and 1.1 Hz at 15 ips      (25 % of the power)
          flutter   the pinch roller (2.4 Hz), the capstan (12 Hz) and its second
                    harmonic, and an irregular part from 2 to 30 Hz                   (75 %)
        which together make the machine's published figure (rms, at 15 ips; slower speeds wobble more -
        (15 / ips)^0.6, which fits the specs: Studer A800 0.04 % at 15 and 30 ips, 0.06 % at 7.5).
          scrape    the tape shivering along its length as it drags over the heads: a band of noise around
                    4 kHz, half the published figure again. It is outside what a flutter meter weighs, and
                    is what roughens high notes.
        Playing back, the wow and flutter move the transport itself (next() returns its speed for the end
        of the block) and the scrape goes to each track's repro side. Recording, the transport is held
        on whole samples so tracks stay aligned, and the flutter and scrape are printed by the record
        side as a moving delay (fast). */
    struct Flutter
    {
        double ph[5] = { 0.0, 1.3, 2.9, 0.7, 4.1 };
        double n1 = 0.0, n2 = 0.0, nHp = 0.0, tau = 0.0;
        BiquadState scrapeSt;
        Noise noise;

        void reset (uint32_t seed) noexcept { noise.s = seed | 1u; n1 = n2 = nHp = tau = 0.0; scrapeSt.reset(); }

        /** percent: the machine's wow and flutter figure. fast / scrape (n samples each, either may be
            null): the time error at the heads, in samples. Returns the transport's speed (1 = steady). */
        double next (double ips, double sr, int n, double percent, float* fast, float* scrape) noexcept
        {
            const double k = juce::jlimit (3.0, 30.0, ips) / 15.0;
            const double dev = 0.01 * percent * std::pow (1.0 / k, 0.6);
            static constexpr double hz[5] = { 0.6, 1.1, 2.4, 12.0, 24.0 };
            static constexpr double power[5] = { 0.15, 0.10, 0.10, 0.25, 0.05 };
            double inc[5], amp[5];
            for (int j = 0; j < 5; ++j)
            {
                inc[j] = juce::MathConstants<double>::twoPi * hz[j] * k / sr;
                amp[j] = std::sqrt (2.0 * power[j]);
            }
            // the irregular part: noise held between 2 and 30 Hz (0.35 of the power)
            const double aLp = 1.0 - std::exp (-juce::MathConstants<double>::twoPi * 30.0 / sr);
            const double aHp = 1.0 - std::exp (-juce::MathConstants<double>::twoPi * 2.0 / sr);
            const double noiseGain = std::sqrt (0.35) * 0.2754 * std::sqrt (sr);      // (measured: unit rms out of the filters)
            const Biquad scrapeBand = bandpass (sr, juce::jmin (4000.0, 0.2 * sr), 1.2);
            const double scrapeGain = 0.5 * dev * 4.07 * std::sqrt (sr / 48000.0) * std::sqrt (4000.0 / juce::jmin (4000.0, 0.2 * sr));
            const double leak = 1.0 - juce::MathConstants<double>::twoPi * 0.4 / sr;     // (lets go of anything slower than the pinch roller)
            // the scrape's time error: its speed error, integrated - at 4 kHz that is a division by 2 pi 4000 / sr
            const double scrapeToTime = sr / (juce::MathConstants<double>::twoPi * juce::jmin (4000.0, 0.2 * sr));
            double speed = 0.0;
            for (int i = 0; i < n; ++i)
            {
                n1 += (noise.next() - n1) * aLp;
                n2 += (n1 - n2) * aLp;
                nHp += (n2 - nHp) * aHp;
                const double irregular = (n2 - nHp) * noiseGain;
                double wow = 0.0, fl = irregular;
                for (int j = 0; j < 5; ++j)
                {
                    ph[j] += inc[j];
                    if (ph[j] > juce::MathConstants<double>::twoPi) ph[j] -= juce::MathConstants<double>::twoPi;
                    (j < 2 ? wow : fl) += amp[j] * std::sin (ph[j]);
                }
                const double s = scrapeSt.process (scrapeBand, noise.next()) * scrapeGain * scrapeToTime;
                tau = tau * leak + dev * fl;
                if (fast != nullptr)   fast[i] = (float) (tau + s);
                if (scrape != nullptr) scrape[i] = (float) s;
                speed = dev * (wow + fl);
            }
            return 1.0 + speed;
        }

        static Biquad bandpass (double sr, double f, double q)
        {
            const double w = juce::MathConstants<double>::twoPi * f / sr, alpha = std::sin (w) / (2.0 * q), c = std::cos (w);
            const double a0 = 1.0 + alpha;
            return { alpha / a0, 0.0, -alpha / a0, -2.0 * c / a0, (1.0 - alpha) / a0 };
        }
    };
}

} // namespace hawthorne
