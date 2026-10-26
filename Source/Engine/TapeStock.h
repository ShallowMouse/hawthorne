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
    enum class Stock { clean = 0, us456, highOutput, vintage, european };
    constexpr int numStocks = 5;

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
    };

    inline const StockInfo& info (Stock s)
    {
        static const StockInfo stocks[numStocks] = {
            { "Clean (no tape)",          "CLEAN",         "No tape sound at all. Records bit-for-bit what comes in: no saturation, no EQ change, no hiss, no wow or flutter.",
              99.0, 0.0,   -200.0, -200.0, 0.0, 0.0, false },
            { "US mastering (456-type)",  "456-TYPE +6",   "Warm, fat and punchy. A strong low-end bump (around 60 Hz at 15 ips) and a slightly rounded top. Saturates fairly early and musically - push the levels for glue and thickness on drums and bass. Moderate hiss. The classic 70s / 80s rock sound.",
              12.0, 0.025, -58.0,  -63.0,  2.0, 0.5, false },
            { "High output (GP9-type)",   "GP9-TYPE +9",   "Clean, tight and modern. The most headroom: you can hit it hard before it gets gritty, so it colours the least. Smaller low bump, full top end, low hiss. Tape glue without obvious distortion.",
              15.0, 0.015, -62.0,  -66.0,  1.5, 0.0, false },
            { "Vintage 60s (250-type)",   "250-TYPE STD",  "Lo-fi and characterful. Breaks up soonest, with the most even-harmonic grit. Darker, softer top end, the biggest woolly low bump, and the most hiss and modulation noise. The 60s / garage sound.",
              8.0,  0.07,  -52.0,  -58.0,  2.5, 2.0, false },
            { "European (911-type)",      "911-TYPE +9",   "Smooth and polished. Lots of headroom, a firm but restrained low end and a silky top (European CCIR EQ). The quietest tape. Refined rather than vibey.",
              14.0, 0.02,  -63.0,  -67.0,  1.2, 0.3, true },
        };
        return stocks[juce::jlimit (0, numStocks - 1, (int) s)];
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
        Biquad preEmph, deEmph;
        float drive = 1.0f, bias = 0.0f, biasOut = 0.0f, modNoise = 0.0f;
        // playback
        Biquad bump, dip, lowCut, hfLoss, hfSoft;
        float hiss = 0.0f;
        Biquad hissHp, hissLp;

        bool isClean() const noexcept { return stock == Stock::clean; }

        static inline float sat (float u) noexcept { return u / std::sqrt (1.0f + u * u); }

        void set (Stock s, double tapeIps, double sampleRate)
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
            const double peakAtHeadroom = std::pow (10.0, (-18.0 + in.headroomDb) / 20.0) * std::sqrt (2.0);
            drive = (float) (0.49 / peakAtHeadroom);
            bias = (float) in.asymmetry;
            biasOut = sat (bias);
            modNoise = (float) std::pow (10.0, in.modNoiseDb / 20.0);

            // Playback: the head bump moves with the tape speed
            const double fb = 4.0 * ips;
            const double speedFactor = std::log2 (15.0 / ips);       // slower tape = bigger bump, earlier top loss
            bump   = Biquad::peak (sr, fb, 1.1, in.bumpDb * (1.0 + 0.25 * speedFactor));
            dip    = Biquad::peak (sr, fb * 2.2, 1.4, -0.4 * in.bumpDb);
            lowCut = Biquad::highpass (sr, fb / 2.6, 0.65);
            const double hfCorner = juce::jmin (0.45 * sr, 18000.0 * ips / 15.0);
            hfLoss = Biquad::firstOrder (sr, 1.0 / (juce::MathConstants<double>::twoPi * hfCorner * 3.0), 1.0 / (juce::MathConstants<double>::twoPi * hfCorner));
            hfSoft = Biquad::highShelf (sr, juce::jmin (0.4 * sr, 9000.0 * ips / 15.0), -in.hfSoftDb);
            hiss = (float) (std::pow (10.0, (-18.0 + in.hissDb + 3.0 * speedFactor) / 20.0) * 1.7);
            hissHp = Biquad::highpass (sr, 250.0, 0.7);
            hissLp = Biquad::firstOrder (sr, 0.0, 1.0 / (juce::MathConstants<double>::twoPi * juce::jmin (0.45 * sr, 11000.0)));
        }
    };

    /** The record side of one track (printed into the file). */
    struct RecordChannel
    {
        HalfBand os;
        BiquadState pre, de;
        Noise noise;
        float env = 0.0f;

        void reset (uint32_t seed) noexcept { os.reset(); pre.reset(); de.reset(); env = 0.0f; noise.s = seed | 1u; }

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
                float y = de.process (sh.deEmph, os.downsample (a, b));
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
