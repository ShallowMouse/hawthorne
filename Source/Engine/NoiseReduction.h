#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <cmath>

namespace hawthorne
{

//==============================================================================
/**
    Noise reduction (Dolby A, Dolby SR, dbx), per reel, like the NR rack next to
    the machine.

    ENCODE (record side, before the tape - printed into the file):
        Dolby: quiet detail in each band is boosted (up to 10-15 dB); loud signals
               pass almost untouched.
        dbx:   high-frequency pre-emphasis and 2:1 compression of everything.
    DECODE (playback side, after the tape and its hiss): the exact inverse, so the
        programme comes back flat while the tape's hiss is pushed down with it.

    Both sides compute their gains from the same signal (the programme before the
    encoder = after the decoder for Dolby, the encoded signal for dbx) using only
    past samples, so an encode -> decode with nothing in between is an exact
    inverse. Tape saturation, speed changes and switching modes per track make it
    mistrack, as on the real thing.

    Per track:  NR (encode + decode), ENC (encode only: the bright "air" trick),
                DEC (decode only), OUT (bypass).
*/
namespace nr
{
    // Saved by number: only ever add to the end.
    enum System { none = 0, dolbyA, dolbySR, dbx };
    constexpr int numSystems = 4;

    enum Mode { normal = 0, encodeOnly, decodeOnly, bypass };
    constexpr int numModes = 4;
    inline bool encodes (int mode) noexcept { return mode == normal || mode == encodeOnly; }
    inline bool decodes (int mode) noexcept { return mode == normal || mode == decodeOnly; }
    inline const char* modeLabel (int mode) { static const char* l[] = { "NR", "ENC", "DEC", "OUT" }; return l[juce::jlimit (0, 3, mode)]; }
    inline const char* modeName (int mode)
    {
        static const char* l[] = { "NR - encode and decode (normal)", "ENC - encode only (bright \"air\" trick)",
                                   "DEC - decode only (dull, expanded)", "OUT - bypass" };
        return l[juce::jlimit (0, 3, mode)];
    }

    struct SystemInfo { const char* name; const char* label; const char* description; };
    inline const SystemInfo& info (int s)
    {
        static const SystemInfo i[numSystems] = {
            { "None", "", "No noise reduction: the tape's hiss is heard as it is." },
            { "Dolby A", "DOLBY A", "The studio standard from 1966: four bands, about 10 dB less hiss (15 dB at the top). "
                                    "Transparent when it tracks. ENC only on a track gives the famous bright, breathy \"Dolby trick\" on vocals." },
            { "Dolby SR", "DOLBY SR", "Spectral Recording (1986): five sliding bands, about 20 dB less hiss - "
                                      "near-digital quiet while keeping the tape sound. The late-80s / 90s professional choice." },
            { "dbx Type I", "DBX", "Wideband 2:1 compression with emphasis: about 30 dB less hiss, but you can hear it "
                                   "breathe and pump on sparse material, and it exaggerates tape wobble. Popular on semi-pro 8 and 16 tracks." },
        };
        return i[juce::jlimit (0, numSystems - 1, s)];
    }

    //==========================================================================
    /** First-order section y = b0 x + z (positive-real, so the decoder's feedback is always stable). */
    struct FirstOrder
    {
        float b0 = 1, b1 = 0, a1 = 0;
        static FirstOrder lowpass (double sr, double f)
        {
            const double k = std::tan (juce::MathConstants<double>::pi * juce::jmin (f, 0.45 * sr) / sr);
            return { (float) (k / (1 + k)), (float) (k / (1 + k)), (float) ((k - 1) / (k + 1)) };
        }
        static FirstOrder highpass (double sr, double f)
        {
            const double k = std::tan (juce::MathConstants<double>::pi * juce::jmin (f, 0.45 * sr) / sr);
            return { (float) (1 / (1 + k)), (float) (-1 / (1 + k)), (float) ((k - 1) / (k + 1)) };
        }
        /** (1 + s t1) / (1 + s t2): emphasis; swap t1, t2 for the exact inverse. */
        static FirstOrder shelf (double sr, double t1, double t2)
        {
            const double k = 2.0 * sr, a0 = 1.0 + k * t2;
            return { (float) ((1.0 + k * t1) / a0), (float) ((1.0 - k * t1) / a0), (float) ((1.0 - k * t2) / a0) };
        }
        static FirstOrder thru() { return {}; }
    };

    struct Band
    {
        FirstOrder a, b;      // in series (a band-pass is a high-pass then a low-pass)
        float boost = 0;      // small-signal side-chain gain (1 + boost = the maximum lift)
        float thresh = 1;     // side-chain limiting
    };

    /** The settings for one system at one sample rate (shared by every track). */
    struct Settings
    {
        int system = none;
        double sr = 0;
        int numBands = 0;
        std::array<Band, 5> bands;
        float attack = 0, release = 0;
        // dbx
        FirstOrder pre, de;
        float rmsCoef = 0, ref = 0.126f, gMax = 31.6f, gMin = 0.1f;

        void set (int sys, double sampleRate)
        {
            system = juce::jlimit (0, numSystems - 1, sys);
            sr = sampleRate;
            numBands = 0;
            attack  = (float) (1.0 - std::exp (-1.0 / (0.0015 * sr)));
            release = (float) (1.0 - std::exp (-1.0 / (0.07 * sr)));

            const float lift10 = 2.162f;                  // +10 dB
            const float zeroVuPeak = 0.178f;              // 0 VU (-18 dBFS rms) as a peak
            auto band = [&] (FirstOrder a, FirstOrder b, float boost, float threshDb)
            {
                bands[(size_t) numBands++] = { a, b, boost, zeroVuPeak * std::pow (10.0f, threshDb / 20.0f) };
            };
            if (system == dolbyA)
            {
                band (FirstOrder::lowpass (sr, 80), FirstOrder::thru(), lift10, -20);
                band (FirstOrder::highpass (sr, 80), FirstOrder::lowpass (sr, 3000), lift10, -20);
                band (FirstOrder::highpass (sr, 3000), FirstOrder::thru(), lift10, -20);
                band (FirstOrder::highpass (sr, 9000), FirstOrder::thru(), 2.46f, -20);    // with band 3: +15 dB at the top
            }
            else if (system == dolbySR)
            {
                band (FirstOrder::lowpass (sr, 200), FirstOrder::thru(), lift10, -24);
                band (FirstOrder::highpass (sr, 200), FirstOrder::lowpass (sr, 3000), 1.5f, -24);
                band (FirstOrder::highpass (sr, 800), FirstOrder::thru(), lift10, -24);
                band (FirstOrder::highpass (sr, 3000), FirstOrder::thru(), 4.0f, -24);
                band (FirstOrder::highpass (sr, 9000), FirstOrder::thru(), 6.0f, -24);     // ~ +24 dB at the top
            }
            else if (system == dbx)
            {
                const double t1 = 1.0 / (juce::MathConstants<double>::twoPi * 1600.0);
                pre = FirstOrder::shelf (sr, t1, t1 / 4.0);     // +12 dB at the top
                de  = FirstOrder::shelf (sr, t1 / 4.0, t1);
                rmsCoef = (float) (1.0 - std::exp (-1.0 / (0.008 * sr)));
            }
        }
    };

    //==========================================================================
    /** One track's encoder or decoder. */
    class Channel
    {
    public:
        void reset() noexcept
        {
            for (auto& s : st) s = {};
            env.fill (0.0f);
            power = 0.0f;
            emph = 0.0f;
        }

        void encode (const Settings& s, const float* in, float* out, int n) noexcept
        {
            if (s.system == dbx)
            {
                for (int i = 0; i < n; ++i)
                {
                    const float e = section (s.pre, emph, in != nullptr ? in[i] : 0.0f);
                    const float y = e * dbxGain (s);           // gain from the output so far (feedback compressor)
                    power += (y * y - power) * s.rmsCoef;
                    out[i] = y;
                }
                return;
            }
            for (int i = 0; i < n; ++i)
            {
                const float x = in != nullptr ? in[i] : 0.0f;
                float y = x;
                for (int b = 0; b < s.numBands; ++b)
                {
                    const auto& bd = s.bands[(size_t) b];
                    const float v = section (bd.b, st[(size_t) b].zb, section (bd.a, st[(size_t) b].za, x));
                    y += gain (bd, env[(size_t) b]) * v;
                    follow (s, env[(size_t) b], v);
                }
                out[i] = y;
            }
        }

        void decode (const Settings& s, float* io, int n) noexcept
        {
            if (s.system == dbx)
            {
                for (int i = 0; i < n; ++i)
                {
                    const float y = io[i];
                    const float e = y / dbxGain (s);
                    power += (y * y - power) * s.rmsCoef;
                    io[i] = section (s.de, emph, e);
                }
                return;
            }
            for (int i = 0; i < n; ++i)
            {
                // y = x + sum g_b (k_b x + c_b)  ->  solve for x
                float num = io[i], den = 1.0f;
                std::array<float, 5> g {};
                for (int b = 0; b < s.numBands; ++b)
                {
                    const auto& bd = s.bands[(size_t) b];
                    g[(size_t) b] = gain (bd, env[(size_t) b]);
                    const float k = bd.b.b0 * bd.a.b0;
                    const float c = bd.b.b0 * st[(size_t) b].za + st[(size_t) b].zb;
                    num -= g[(size_t) b] * c;
                    den += g[(size_t) b] * k;
                }
                const float x = num / den;
                for (int b = 0; b < s.numBands; ++b)
                {
                    const auto& bd = s.bands[(size_t) b];
                    const float v = section (bd.b, st[(size_t) b].zb, section (bd.a, st[(size_t) b].za, x));
                    follow (s, env[(size_t) b], v);
                }
                io[i] = x;
            }
        }

    private:
        struct State { float za = 0, zb = 0; };
        std::array<State, 5> st {};
        std::array<float, 5> env {};
        float power = 0, emph = 0;

        static inline float section (const FirstOrder& f, float& z, float x) noexcept
        {
            const float y = f.b0 * x + z;
            z = f.b1 * x - f.a1 * y;
            return y;
        }
        static inline float gain (const Band& b, float e) noexcept { return b.boost / (1.0f + b.boost * e / b.thresh); }
        static inline void follow (const Settings& s, float& e, float v) noexcept
        {
            const float a = std::abs (v);
            e += (a - e) * (a > e ? s.attack : s.release);
        }
        inline float dbxGain (const Settings& s) const noexcept
        {
            return juce::jlimit (s.gMin, s.gMax, s.ref / std::sqrt (power + 1.0e-12f));
        }
    };
}

} // namespace hawthorne
