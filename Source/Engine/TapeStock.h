#pragma once

#include <complex>

#include <juce_core/juce_core.h>
#include <array>
#include <cmath>

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

    TRANSPORT: wow and flutter (a tiny speed wobble) while playing back.
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
        double headroomDb;      // added to the stock's
        double asymmetry;       // added (valve electronics: more even harmonics)
        double bumpScale;       // head bump size
        double bumpFreq;        // head bump frequency
        double hfScale;         // high-frequency extension (corner multiplier)
        double hfSoftDb;        // added top-end softening (negative = brighter)
        double lowCut;          // low roll-off frequency multiplier
        double hissDb;          // added hiss
        double wowFlutter;      // wow and flutter, rms % at 15 ips (published specs; DIN peak-weighted figures / 1.5)
    };

    inline constexpr MachineInfo neutralMachine { "None", "", "", 0, 0, 1, 1, 1, 0, 1, 0, 0.03 };

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
              -3.0, 0.05, 1.3,  1.0,  0.8,  1.5,  1.1,  3.0, 0.06 },
            { "3M M23 (8-track)", "3M M23", "The Beatles' first 8-track at Abbey Road (1968-69, the White Album, Abbey Road). Early transistor: punchy and solid, a little darker and grainier than later machines.",
              -1.5, 0.025, 1.2, 1.0,  0.95, 0.6,  1.0,  1.5, 0.04 },
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
              -3.5, 0.055, 1.35, 0.95, 0.75, 1.8,  1.1,  3.5, 0.12 },
            { "Ampex 300-8 (Sel-Sync 8-track)", "AMPEX 300-8", "The first Sel-Sync 8-track, on 1-inch tape (1957): valve electronics, warm and dense, soft on top with plenty of hiss and wobble.",
              -3.0, 0.05,  1.3,  0.95, 0.78, 1.6,  1.1,  3.0, 0.12 },
        };
        return m[juce::jlimit (0, numMultitrackMachines - 1, i)];
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
              -2.5, 0.05, 1.2,  0.95, 0.75, 1.5,  0.9,  3.0, 0.12 },
            { "EMI BTR2 (valve)", "EMI BTR2", "EMI's valve 2-track: the Beatles' mono and stereo masters. Warm and round with gentle valve saturation and a soft top.",
              -2.0, 0.04, 1.25, 0.95, 0.85, 1.0,  0.9,  2.0, 0.1 },
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
              -3.0, 0.055, 1.25, 0.95, 0.72, 1.8,  0.95, 3.5, 0.1 },
            { "Telefunken M5 (valve)", "TELEFUNKEN M5", "The German valve studio deck of 1954. Smoother and quieter than the American valve machines: a gentle, silky saturation and a soft top.",
              -2.0, 0.04,  1.1,  1.0,  0.85, 1.0,  1.0,  2.0, 0.08 },
        };
        return m[juce::jlimit (0, numMasterMachines - 1, i)];
    }


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

    //==========================================================================
    /** Settings shared by every track for the current stock and speed. */
    struct Shared
    {
        Stock stock = Stock::clean;
        double ips = 15.0, sr = 48000.0;
        // record
        Biquad preEmph, deEmph, lfPre, lfDe, biasEq;
        float drive = 1.0f, bias = 0.0f, biasOut = 0.0f, modNoise = 0.0f;
        // playback
        Biquad bump, dip, lowCut, hfLoss, hfSoft;
        float hiss = 0.0f;
        Biquad hissHp, hissLp;

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
            bias = (float) (in.asymmetry + mc.asymmetry);
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
            modNoise = (float) std::pow (10.0, (in.modNoiseDb + (d < 0.0 ? -2.5 * d : 0.0)) / 20.0);
            biasEq = Biquad::highShelf (sr, juce::jmin (0.4 * sr, 6000.0 * ips / 15.0), -1.5 * d);   // printed: over-bias dulls the top

            // Playback: the head bump moves with the tape speed
            const double fb = 4.0 * ips * mc.bumpFreq;
            const double speedFactor = std::log2 (15.0 / ips);       // slower tape = bigger bump, earlier top loss
            bump   = Biquad::peak (sr, fb, 1.1, in.bumpDb * mc.bumpScale * (1.0 + 0.25 * speedFactor) * (1.0 + 0.04 * d));
            dip    = Biquad::peak (sr, fb * 2.2, 1.4, -0.4 * in.bumpDb * mc.bumpScale);
            lowCut = Biquad::highpass (sr, fb / 2.6 * mc.lowCut, 0.65);
            const double hfCorner = juce::jmin (0.45 * sr, 18000.0 * ips / 15.0 * mc.hfScale);
            hfLoss = Biquad::firstOrder (sr, 1.0 / (juce::MathConstants<double>::twoPi * hfCorner * 3.0), 1.0 / (juce::MathConstants<double>::twoPi * hfCorner));
            hfSoft = Biquad::highShelf (sr, juce::jmin (0.4 * sr, 9000.0 * ips / 15.0), -(in.hfSoftDb + mc.hfSoftDb));
            hiss = (float) (std::pow (10.0, (-18.0 + in.hissDb + mc.hissDb + 3.0 * speedFactor - levelOffset) / 20.0) * 1.7);
            hissHp = Biquad::highpass (sr, 250.0, 0.7);
            hissLp = Biquad::firstOrder (sr, 0.0, 1.0 / (juce::MathConstants<double>::twoPi * juce::jmin (0.45 * sr, 11000.0)));
        }
    };

    /** The record side of one track (printed into the file). */
    struct RecordChannel
    {
        HalfBand os;
        BiquadState pre, de, lfPreSt, lfDeSt, biasSt;
        Noise noise;
        float env = 0.0f;

        void reset (uint32_t seed) noexcept { os.reset(); pre.reset(); de.reset(); lfPreSt.reset(); lfDeSt.reset(); biasSt.reset(); env = 0.0f; noise.s = seed | 1u; }

        inline void process (const Shared& sh, const float* in, float* out, int n) noexcept
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
                float y = biasSt.process (sh.biasEq, lfDeSt.process (sh.lfDe, de.process (sh.deEmph, os.downsample (a, b))));
                env += (std::abs (y) - env) * 0.002f;
                y += noise.next() * env * sh.modNoise;
                out[i] = y;
            }
        }
    };

    /** The playback side of one track. */
    struct PlaybackChannel
    {
        BiquadState bump, dip, lowCut, hfLoss, hfSoft, hissHp, hissLp;
        Noise noise;

        void reset (uint32_t seed) noexcept
        {
            for (auto* s : { &bump, &dip, &lowCut, &hfLoss, &hfSoft, &hissHp, &hissLp }) s->reset();
            noise.s = seed | 1u;
        }

        inline void process (const Shared& sh, float* io, int n) noexcept
        {
            for (int i = 0; i < n; ++i)
            {
                float y = io[i];
                y = lowCut.process (sh.lowCut, y);
                y = bump.process (sh.bump, y);
                y = dip.process (sh.dip, y);
                y = hfLoss.process (sh.hfLoss, y);
                y = hfSoft.process (sh.hfSoft, y);
                const float h = hissLp.process (sh.hissLp, hissHp.process (sh.hissHp, noise.next()));
                io[i] = y + h * sh.hiss;
            }
        }
    };

    /** Transport wow (slow) and flutter (fast, irregular) as a speed multiplier: 1 % rms at 15 ips,
        half wow and half flutter by power, for the engine to scale by the machine's spec. Slower
        speeds wobble more - (15 / ips)^0.6, which fits the specs (Studer A800 0.04 % at 15 and 30 ips,
        0.06 % at 7.5; MCI JH-24 0.04 % at 15, 0.03 % at 30). */
    struct WowFlutter
    {
        double phase = 0.0, fl = 0.0, flState = 0.0;
        Noise noise;

        double next (double ips, double sr, int n) noexcept
        {
            const double scale = std::pow (15.0 / juce::jlimit (3.0, 30.0, ips), 0.6);
            phase += juce::MathConstants<double>::twoPi * 0.55 * n / sr;
            if (phase > juce::MathConstants<double>::twoPi) phase -= juce::MathConstants<double>::twoPi;
            // flutter: noise band-limited to a few Hz around 8 Hz, per block
            const double a = 1.0 - std::exp (-juce::MathConstants<double>::twoPi * 8.0 * n / sr);
            flState += (noise.next() - flState) * a;
            fl += (flState - fl) * a;
            return 1.0 + scale * (0.01 * std::sin (phase) + 0.00707 / 0.2152 * fl);   // (0.2152: the flutter noise's own rms)
        }
    };
}

} // namespace hawthorne
