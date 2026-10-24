#pragma once

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include <cmath>

namespace hawthorne
{

//==============================================================================
/**
    Varispeed resampling.

    One Kaiser-windowed sinc kernel (64 zero crossings each side, ~100 dB
    stopband, flat to ~0.9 of Nyquist) is used for both directions:
      - playback: tape -> device rate, read at fractional tape positions
      - recording: device -> tape rate, so overdubs made at varispeed sit on
        the tape exactly like they would on a real machine.

    When the tape runs faster than the device rate the kernel is stretched so
    the cutoff follows (no aliasing). Weights are computed once per output
    sample and shared by every track, so the cost per track is one dot product.
    Tape position is kept as an integer sample plus a double fraction, so there
    is no drift however long the reel runs.
*/
class SincKernel
{
public:
    static constexpr int    halfWidth  = 64;      // zero crossings each side at unity ratio
    static constexpr int    resolution = 512;     // table points per zero crossing
    static constexpr double cutoff     = 0.945;   // fraction of Nyquist
    static constexpr double beta       = 10.0;    // Kaiser window (~100 dB)

    static const SincKernel& get()
    {
        static const SincKernel k;
        return k;
    }

    /** Taps either side of the read point for a given cutoff scale (<= 1). */
    static int halfSpan (double scale) noexcept   { return (int) std::ceil ((double) halfWidth / scale - 1.0e-9); }

    /** Weights for samples (floor(t) - hs + 1) ... (floor(t) + hs), where frac = t - floor(t).
        Normalised to unity gain at DC. */
    void weights (double frac, double scale, int hs, float* w) const noexcept
    {
        const int taps = 2 * hs;
        const double step = scale * (double) resolution;
        const float* tab = table.data();
        const double limit = (double) (halfWidth * resolution);
        double sum = 0.0;

        for (int i = 0; i < taps; ++i)
        {
            const double x = std::abs ((double) (i - hs + 1) - frac) * step;
            float v = 0.0f;
            if (x < limit)
            {
                const int idx = (int) x;
                const float f = (float) (x - (double) idx);
                v = tab[idx] + f * (tab[idx + 1] - tab[idx]);
            }
            w[i] = v;
            sum += v;
        }

        const float norm = sum != 0.0 ? (float) (1.0 / sum) : 0.0f;
        for (int i = 0; i < taps; ++i)
            w[i] *= norm;
    }

private:
    SincKernel()
    {
        const int n = halfWidth * resolution;
        table.resize ((size_t) n + 2, 0.0f);

        auto i0 = [] (double x)
        {
            double sum = 1.0, term = 1.0;
            for (int k = 1; k < 64; ++k)
            {
                term *= (x / (2.0 * k)) * (x / (2.0 * k));
                sum += term;
                if (term < sum * 1.0e-17) break;
            }
            return sum;
        };

        const double i0b = i0 (beta);
        for (int i = 0; i <= n; ++i)
        {
            const double x = (double) i / resolution;                 // in zero crossings
            const double z = x / halfWidth;
            const double sx = juce::MathConstants<double>::pi * cutoff * x;
            const double sinc = i == 0 ? 1.0 : std::sin (sx) / sx;
            const double win = z < 1.0 ? i0 (beta * std::sqrt (1.0 - z * z)) / i0b : 0.0;
            table[(size_t) i] = (float) (sinc * win);
        }
    }

    std::vector<float> table;
};

/** Dot product, vectorised where the CPU allows. */
float dotProduct (const float* a, const float* b, int n) noexcept;

//==============================================================================
/**
    Converts device-rate input into tape-rate samples for one record pass at a
    constant tape speed. Every channel shares the same timing (and weights).

    Input sample u (device samples since the pass began) belongs on the tape at
        P0 + rate * (u - latency)
    i.e. where the tape was when the sound being played along to left the
    machine. Tape samples are produced at whole tape positions from
    firstTapeSample() onwards, ready to append to a RecordingStream.
*/
class RecordResampler
{
public:
    /** Message thread: allocates everything. */
    void prepare (int numChannels, double tapeRate, int maxBlock)
    {
        channels = juce::jmax (1, numChannels);
        rate = tapeRate;
        scale = juce::jmin (1.0, rate);                 // tape slower than the device = cutoff falls
        hs = SincKernel::halfSpan (scale);
        ringSize = 2 * maxBlock + 4 * hs + 16;
        ring.setSize (channels, 2 * ringSize);
        ring.clear();
        maxOut = (int) std::ceil (maxBlock * rate) + 4;
        out.setSize (channels, maxOut);
        w.resize ((size_t) (2 * hs));
        K = 0;
        produced = 0;
    }

    /** Audio thread, first block of the pass. p0 = exact tape position of that block's first sample. */
    void start (double p0, int latencySamples) noexcept
    {
        P0 = p0;
        latency = latencySamples;
        mNext = juce::jmax ((juce::int64) 0, (juce::int64) std::ceil (P0 - rate * latency - 1.0e-9));
        first = mNext;
    }

    juce::int64 firstTapeSample() const noexcept { return first; }

    /** Audio thread: one block for one channel (every channel gets the same n each block). */
    void push (int ch, const float* src, int n) noexcept
    {
        float* r = ring.getWritePointer (ch);
        for (int i = 0; i < n; ++i)
        {
            const int idx = (int) ((K + i) % ringSize);
            const float v = src != nullptr ? src[i] : 0.0f;
            r[idx] = v;
            r[idx + ringSize] = v;
        }
    }

    /** Audio thread: after every channel was pushed. Returns how many tape samples are ready. */
    int produce (int n) noexcept
    {
        K += n;
        int count = 0;
        const auto& kernel = SincKernel::get();

        while (count < maxOut)
        {
            const double u = ((double) mNext - P0) / rate + (double) latency;
            const double fu = std::floor (u);
            const auto iu = (juce::int64) fu;
            if (iu + hs > K - 1)
                break;

            kernel.weights (u - fu, scale, hs, w.data());
            const auto base = iu - hs + 1;
            const int idx = (int) (((base % ringSize) + ringSize) % ringSize);

            for (int ch = 0; ch < channels; ++ch)
                out.getWritePointer (ch)[count] = dotProduct (w.data(), ring.getReadPointer (ch) + idx, 2 * hs);

            ++mNext;
            ++count;
        }

        produced += count;
        return count;
    }

    const float* output (int ch) const noexcept { return out.getReadPointer (ch); }
    int getNumChannels() const noexcept         { return channels; }

private:
    int channels = 1, hs = 1, ringSize = 1, maxOut = 1, latency = 0;
    double rate = 1.0, scale = 1.0, P0 = 0.0;
    juce::int64 K = 0, mNext = 0, first = 0, produced = 0;
    juce::AudioBuffer<float> ring, out;
    std::vector<float> w;
};

} // namespace hawthorne
