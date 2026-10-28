#pragma once

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
    enum class Stock { clean = 0, us456, highOutput, vintage, european, emitape, mastering499, sm900 };
    constexpr int numStocks = 8;
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
    };

    inline const StockInfo& info (Stock s)
    {
        static const StockInfo stocks[numStocks] = {
            { "Clean (no tape)",          "CLEAN",         "No tape sound at all. Records bit-for-bit what comes in: no saturation, no EQ change, no hiss, no wow or flutter.",
              99.0, 0.0,   -200.0, -200.0, 0.0, 0.0, false, forMultitrack | forMaster },
            { "US mastering (456-type)",  "456-TYPE +6",   "Warm, fat and punchy. A strong low-end bump (around 60 Hz at 15 ips) and a slightly rounded top. Saturates fairly early and musically - push the levels for glue and thickness on drums and bass. Moderate hiss. The classic 70s / 80s rock sound.",
              12.0, 0.025, -58.0,  -63.0,  2.0, 0.5, false, forMultitrack },
            { "High output (GP9-type)",   "GP9-TYPE +9",   "Clean, tight and modern. The most headroom: you can hit it hard before it gets gritty, so it colours the least. Smaller low bump, full top end, low hiss. Tape glue without obvious distortion.",
              15.0, 0.015, -62.0,  -66.0,  1.5, 0.0, false, forMultitrack },
            { "Vintage 60s (250-type)",   "250-TYPE STD",  "Lo-fi and characterful. Breaks up soonest, with the most even-harmonic grit. Darker, softer top end, the biggest woolly low bump, and the most hiss and modulation noise. The 60s / garage sound.",
              8.0,  0.07,  -52.0,  -58.0,  2.5, 2.0, false, forMultitrack },
            { "European (911-type)",      "911-TYPE +9",   "Smooth and polished. Lots of headroom, a firm but restrained low end and a silky top (European CCIR EQ). The quietest tape. Refined rather than vibey.",
              14.0, 0.02,  -63.0,  -67.0,  1.2, 0.3, true, forMultitrack },
            { "EMI 60s (Emitape-type)",   "EMITAPE",       "The tape the Beatles used at Abbey Road. Standard level: breaks up early and sweetly with plenty of even harmonics, a soft top, a round low end and audible hiss (CCIR EQ). Pair it with the J37 or the 3M M23 - and the BTR2 for the master.",
              7.0,  0.06,  -53.0,  -58.5,  2.3, 1.6, true, forMultitrack | forMaster },
            { "Mastering (480/499-type)", "499-TYPE +9",   "Mastering-grade US tape for the 2-track. Big headroom with a warm, solid low end and a clean top: glues the mix without obvious distortion. Low noise.",
              14.0, 0.02,  -62.0,  -67.0,  2.0, 0.3, false, forMaster },
            { "Mastering (SM900-type)",   "SM900 +12",     "Very high output mastering tape. Huge headroom and the lowest noise: the cleanest tape print, a subtle firm low end and a full top. For loud, modern masters.",
              17.0, 0.012, -65.0,  -70.0,  1.6, 0.0, true, forMaster },
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
            case Stock::highOutput:   return 9;
            case Stock::european:     return 9;
            case Stock::mastering499: return 9;
            case Stock::sm900:        return 12;
            default:                  return 0;     // standard-level tapes (250, Emitape) and clean
        }
    }
    inline double recommendedBias (Stock s)
    {
        switch (s)
        {
            case Stock::vintage:      return 2.5;
            case Stock::emitape:      return 2.0;
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
        double wowFlutter;      // transport wobble multiplier
    };

    inline constexpr MachineInfo neutralMachine { "None", "", "", 0, 0, 1, 1, 1, 0, 1, 0, 1 };

    constexpr int numMultitrackMachines = 7;     // the last is "None": bit-perfect
    constexpr int noMultitrackMachine = 6;
    inline const MachineInfo& multitrackMachine (int i)
    {
        static const MachineInfo m[numMultitrackMachines] = {
            { "Studer A800 / A827", "STUDER A800", "Clean, punchy and tight: a firm, controlled low end, an extended top and a rock-steady transport. The modern studio standard.",
              0.0, 0.0,   1.0,  1.0,  1.1,  0.0,  0.9,  0.0, 0.7 },
            { "MCI JH-24", "MCI JH-24", "Bright and aggressive: forward upper mids, a lighter low end and a bit more grit. The 70s / 80s rock workhorse.",
              -1.0, 0.01, 0.8,  1.1,  1.15, -0.6, 1.1,  1.0, 1.2 },
            { "Ampex MM1200", "AMPEX MM1200", "Big and warm: the fattest low end and a soft, rounded top. Thick and forgiving.",
              -0.5, 0.015, 1.35, 0.9, 0.85, 0.8,  0.85, 1.5, 1.3 },
            { "Otari MTR-90", "OTARI MTR-90", "Neutral and solid: modern, honest and quiet, with a dependable transport. Colours the least.",
              1.0, -0.005, 0.9, 1.0,  1.0,  0.0,  1.0, -1.0, 0.8 },
            { "Studer J37 (EMI, valve 4-track)", "STUDER J37", "The Beatles' 4-track at Abbey Road (1965-67, Revolver, Sgt. Pepper). Valve electronics: thick, warm and slightly compressed, with soft valve grit, a gentle top and more hiss and wobble.",
              -3.0, 0.05, 1.3,  1.0,  0.8,  1.5,  1.1,  3.0, 1.6 },
            { "3M M23 (8-track)", "3M M23", "The Beatles' first 8-track at Abbey Road (1968-69, the White Album, Abbey Road). Early transistor: punchy and solid, a little darker and grainier than later machines.",
              -1.5, 0.025, 1.2, 1.0,  0.95, 0.6,  1.0,  1.5, 1.2 },
            { "None (bit-perfect)", "NO MACHINE", "No machine: nothing between you and the files. No tape sound, no hiss, no wow or flutter - bit-for-bit, whatever the tape on the reel.",
              0, 0, 1, 1, 1, 0, 1, 0, 0 },
        };
        return m[juce::jlimit (0, numMultitrackMachines - 1, i)];
    }

    constexpr int numMasterMachines = 6;         // the last is "None": bit-perfect
    constexpr int noMasterMachine = 5;
    inline const MachineInfo& masterMachine (int i)
    {
        static const MachineInfo m[numMasterMachines] = {
            { "Ampex ATR-102", "AMPEX ATR-102", "The classic mastering deck: a huge, deep low end, an open top and very low noise.",
              1.0, 0.005, 1.4,  0.85, 1.15, -0.2, 0.75, -1.5, 0.5 },
            { "Studer A80 / A820", "STUDER A80", "Smooth and detailed with a slightly silky top. Polished and refined.",
              0.5, 0.0,   1.0,  1.0,  1.05, 0.3,  0.9, -1.0, 0.6 },
            { "Otari MTR-12", "OTARI MTR-12", "Clean and neutral: an honest print of the mix.",
              1.0, 0.0,   0.85, 1.0,  1.0,  0.0,  1.0, -1.0, 0.7 },
            { "Ampex 351 (valve)", "AMPEX 351", "Vintage valve electronics: thick even-harmonic warmth, a softer top and more noise.",
              -2.5, 0.05, 1.2,  0.95, 0.75, 1.5,  0.9,  3.0, 1.5 },
            { "EMI BTR2 (valve)", "EMI BTR2", "EMI's valve 2-track: the Beatles' mono and stereo masters. Warm and round with gentle valve saturation and a soft top.",
              -2.0, 0.04, 1.25, 0.95, 0.85, 1.0,  0.9,  2.0, 1.2 },
            { "None (bit-perfect)", "NO MACHINE", "No machine: the master is recorded bit-for-bit, whatever its tape.",
              0, 0, 1, 1, 1, 0, 1, 0, 0 },
        };
        return m[juce::jlimit (0, numMasterMachines - 1, i)];
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
        Biquad preEmph, deEmph, biasEq;
        float drive = 1.0f, bias = 0.0f, biasOut = 0.0f, modNoise = 0.0f;
        // playback
        Biquad bump, dip, lowCut, hfLoss, hfSoft;
        float hiss = 0.0f;
        Biquad hissHp, hissLp;

        bool isClean() const noexcept { return stock == Stock::clean; }

        static inline float sat (float u) noexcept { return u / std::sqrt (1.0f + u * u); }

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

            // Equalisation: NAB 50 us at 7.5 / 15 ips, CCIR 35 us at 15 (70 at 7.5), AES 17.5 us at 30
            double tau = ips >= 25.0 ? 17.5e-6 : (in.ccir ? (ips >= 12.0 ? 35e-6 : 70e-6) : 50e-6);
            preEmph = Biquad::firstOrder (sr, tau, tau / 6.0);    // up to +15.6 dB at the top, before the tape
            deEmph  = Biquad::firstOrder (sr, tau / 6.0, tau);    // exact inverse after it

            // Saturation: ~3 % third harmonic at the stock's headroom above 0 VU (= -18 dBFS)
            const double d = biasOffset;
            const double biasHeadroom = d < 0.0 ? 2.0 * d : 0.6 * d - 0.15 * d * d;
            const double peakAtHeadroom = std::pow (10.0, (-18.0 + in.headroomDb + mc.headroomDb - levelOffset + biasHeadroom) / 20.0) * std::sqrt (2.0);
            drive = (float) (0.49 / peakAtHeadroom);
            bias = (float) (in.asymmetry + mc.asymmetry);
            biasOut = sat (bias);
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
        BiquadState pre, de, biasSt;
        Noise noise;
        float env = 0.0f;

        void reset (uint32_t seed) noexcept { os.reset(); pre.reset(); de.reset(); biasSt.reset(); env = 0.0f; noise.s = seed | 1u; }

        inline void process (const Shared& sh, const float* in, float* out, int n) noexcept
        {
            const float drive = sh.drive, inv = 1.0f / drive;
            for (int i = 0; i < n; ++i)
            {
                const float x = in != nullptr ? in[i] : 0.0f;
                const float e = pre.process (sh.preEmph, x);
                float a, b;
                os.upsample (e, a, b);
                a = (Shared::sat (a * drive + sh.bias) - sh.biasOut) * inv;
                b = (Shared::sat (b * drive + sh.bias) - sh.biasOut) * inv;
                float y = biasSt.process (sh.biasEq, de.process (sh.deEmph, os.downsample (a, b)));
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

    /** Transport wow (slow) and flutter (fast, irregular) as a speed multiplier. */
    struct WowFlutter
    {
        double phase = 0.0, fl = 0.0, flState = 0.0;
        Noise noise;

        double next (double ips, double sr, int n) noexcept
        {
            const double scale = juce::jlimit (0.5, 3.0, 15.0 / ips);
            phase += juce::MathConstants<double>::twoPi * 0.55 * n / sr;
            if (phase > juce::MathConstants<double>::twoPi) phase -= juce::MathConstants<double>::twoPi;
            // flutter: noise band-limited to a few Hz around 8 Hz, per block
            const double a = 1.0 - std::exp (-juce::MathConstants<double>::twoPi * 8.0 * n / sr);
            flState += (noise.next() - flState) * a;
            fl += (flState - fl) * a;
            return 1.0 + scale * (0.00025 * std::sin (phase) + 0.0006 * fl);
        }
    };
}

} // namespace hawthorne
