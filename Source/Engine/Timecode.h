#pragma once

#include <juce_core/juce_core.h>
#include <cmath>
#include <array>

namespace hawthorne
{

//==============================================================================
/**
    SMPTE timecode: frame rates, drop-frame maths, LTC (audio) encoding and
    decoding. Everything here is allocation-free and audio-thread safe.
*/
namespace timecode
{
    enum class Rate { fps24 = 0, fps25, fps2997drop, fps2997, fps30 };

    inline double fps (Rate r) noexcept
    {
        switch (r)
        {
            case Rate::fps24:       return 24.0;
            case Rate::fps25:       return 25.0;
            case Rate::fps2997drop:
            case Rate::fps2997:     return 30000.0 / 1001.0;
            case Rate::fps30:       return 30.0;
        }
        return 25.0;
    }

    inline int nominal (Rate r) noexcept    { return r == Rate::fps24 ? 24 : (r == Rate::fps25 ? 25 : 30); }
    inline bool isDrop (Rate r) noexcept    { return r == Rate::fps2997drop; }
    inline juce::String name (Rate r)
    {
        switch (r)
        {
            case Rate::fps24:       return "24";
            case Rate::fps25:       return "25";
            case Rate::fps2997drop: return "29.97 drop";
            case Rate::fps2997:     return "29.97";
            case Rate::fps30:       return "30";
        }
        return {};
    }

    struct Time
    {
        int hours = 0, minutes = 0, seconds = 0, frames = 0;
        juce::String toString (bool drop) const
        {
            auto two = [] (int v) { return juce::String (v).paddedLeft ('0', 2); };
            return two (hours) + ":" + two (minutes) + ":" + two (seconds) + (drop ? ";" : ":") + two (frames);
        }
    };

    /** Frame count (from 00:00:00:00) -> hh:mm:ss:ff, with drop-frame numbering where needed. Wraps at 24 h. */
    inline Time fromFrames (juce::int64 frame, Rate r) noexcept
    {
        const int n = nominal (r);
        if (isDrop (r))
        {
            // 29.97 drop: frames 00 and 01 are skipped every minute except every tenth.
            const juce::int64 perTen = 17982, perMin = 1798;
            const juce::int64 d = frame / perTen, m = frame % perTen;
            frame += 18 * d + (m > 1 ? 2 * ((m - 2) / perMin) : 0);
        }
        const juce::int64 perDay = (juce::int64) n * 86400;
        frame = ((frame % perDay) + perDay) % perDay;
        Time t;
        t.frames  = (int) (frame % n);
        t.seconds = (int) ((frame / n) % 60);
        t.minutes = (int) ((frame / (n * 60)) % 60);
        t.hours   = (int) (frame / ((juce::int64) n * 3600));
        return t;
    }

    /** hh:mm:ss:ff -> frame count (inverse of fromFrames). */
    inline juce::int64 toFrames (const Time& t, Rate r) noexcept
    {
        const int n = nominal (r);
        juce::int64 f = (((juce::int64) t.hours * 60 + t.minutes) * 60 + t.seconds) * n + t.frames;
        if (isDrop (r))
        {
            const juce::int64 totalMinutes = (juce::int64) t.hours * 60 + t.minutes;
            f -= 2 * (totalMinutes - totalMinutes / 10);
        }
        return f;
    }

    /** Parses "hh:mm:ss:ff" (any separators). */
    inline bool parse (const juce::String& s, Time& t)
    {
        auto parts = juce::StringArray::fromTokens (s.trim(), ":;.", {});
        parts.removeEmptyStrings();
        if (parts.size() != 4) return false;
        t = { parts[0].getIntValue(), parts[1].getIntValue(), parts[2].getIntValue(), parts[3].getIntValue() };
        return t.hours >= 0 && t.hours < 24 && t.minutes >= 0 && t.minutes < 60 && t.seconds >= 0 && t.seconds < 60 && t.frames >= 0;
    }

    //==========================================================================
    /** The 80 bits of one LTC frame (SMPTE 12M), bit 0 first. */
    inline std::array<uint8_t, 80> ltcBits (const Time& t, Rate r) noexcept
    {
        std::array<uint8_t, 80> b {};
        auto put = [&b] (int first, int count, int value) { for (int i = 0; i < count; ++i) b[(size_t) (first + i)] = (uint8_t) ((value >> i) & 1); };
        put (0, 4, t.frames % 10);   put (8, 2, t.frames / 10);
        b[10] = isDrop (r) ? 1 : 0;
        put (16, 4, t.seconds % 10); put (24, 3, t.seconds / 10);
        put (32, 4, t.minutes % 10); put (40, 3, t.minutes / 10);
        put (48, 4, t.hours % 10);   put (56, 2, t.hours / 10);
        static const uint8_t sync[16] = { 0,0,1,1,1,1,1,1,1,1,1,1,1,1,0,1 };
        for (int i = 0; i < 16; ++i) b[(size_t) (64 + i)] = sync[i];

        // polarity correction: an even number of zeros, so every frame starts the same way
        const int pc = r == Rate::fps25 ? 59 : 27;
        int zeros = 0;
        for (int i = 0; i < 80; ++i) if (i != pc && b[(size_t) i] == 0) ++zeros;
        b[(size_t) pc] = (zeros % 2 == 0) ? 1 : 0;   // a 1 keeps the count even, a 0 makes an odd count even
        return b;
    }

    /**
        LTC generator. Stateless per sample: the level at any point of a frame is
        known from the bits, so it follows the tape exactly (varispeed, locates).
        A gentle low-pass gives the rise time the standard asks for.
    */
    class LtcEncoder
    {
    public:
        void prepare (double sampleRate) noexcept
        {
            // ~40 us rise time
            smooth = 1.0f - (float) std::exp (-1.0 / (sampleRate * 15.0e-6));
            state = 0.0f;
            lastFrame = -1;
        }

        /** One output sample at a timecode position, in frames (fractional) since 00:00:00:00. */
        float sample (double framePos, Rate r, float level) noexcept
        {
            const auto frame = (juce::int64) std::floor (framePos);
            if (frame != lastFrame)
            {
                bits = ltcBits (fromFrames (frame, r), r);
                // transitions before each bit (one at every bit start, one mid-bit for a 1)
                int count = 0;
                for (int i = 0; i < 80; ++i) { before[(size_t) i] = count; count += 1 + bits[(size_t) i]; }
                lastFrame = frame;
            }
            const double phase = (framePos - (double) frame) * 80.0;
            const int bit = juce::jlimit (0, 79, (int) phase);
            int transitions = before[(size_t) bit] + 1;
            if (bits[(size_t) bit] && phase - (double) bit >= 0.5) ++transitions;
            const float target = (transitions & 1) ? level : -level;
            state += (target - state) * smooth;
            return state;
        }

        void reset() noexcept { state = 0.0f; }

    private:
        std::array<uint8_t, 80> bits {};
        std::array<int, 80> before {};
        juce::int64 lastFrame = -1;
        float smooth = 1.0f, state = 0.0f;
    };

    //==========================================================================
    /**
        LTC reader. Finds the transitions, sorts them into half- and whole-bit
        intervals (adapting to the speed), shifts bits in and recognises the
        sync word. Works forwards, at any speed from about 0.1x to 4x.
    */
    class LtcDecoder
    {
    public:
        struct Frame { Time time; bool drop = false; juce::int64 endSample = 0; };

        void prepare (double sampleRate) noexcept
        {
            sr = sampleRate;
            bitLen = sr / (25.0 * 80.0);
            last = 0.0f;
            lastEdge = -1;
            halfPending = false;
            reg.fill (0);
            filled = 0;
            sampleCount = 0;
            hysteresis = 0.02f;
            peak = 0.0f;
        }

        /** Feed input; for every complete frame calls onFrame (Frame). */
        template <typename OnFrame>
        void process (const float* in, int n, OnFrame&& onFrame) noexcept
        {
            for (int i = 0; i < n; ++i, ++sampleCount)
            {
                const float x = in[i];
                peak = juce::jmax (peak * 0.99995f, std::abs (x));
                const float th = juce::jmax (0.004f, peak * 0.25f);

                // Schmitt trigger on the sign
                const bool high = level ? x > -th : x > th;
                if (high == level) continue;
                level = high;

                if (lastEdge >= 0)
                    edge ((double) (sampleCount - lastEdge), onFrame);
                lastEdge = sampleCount;
            }
        }

    private:
        template <typename OnFrame>
        void edge (double interval, OnFrame& onFrame) noexcept
        {
            // a whole bit (0) or half a bit (1 comes as two halves)
            if (interval > bitLen * 1.6 || interval < bitLen * 0.2)
            {
                halfPending = false;     // a gap or noise: start again
                if (interval > bitLen * 1.6 && interval < bitLen * 4.0)
                    bitLen = interval * 0.9;   // slow tape: adapt
                return;
            }

            if (interval > bitLen * 0.75)
            {
                halfPending = false;
                bitLen += (interval - bitLen) * 0.1;
                shiftIn (0, onFrame);
            }
            else if (halfPending)
            {
                halfPending = false;
                bitLen += (2.0 * interval - bitLen) * 0.1;
                shiftIn (1, onFrame);
            }
            else
            {
                halfPending = true;
            }
        }

        template <typename OnFrame>
        void shiftIn (uint8_t bit, OnFrame& onFrame) noexcept
        {
            for (int i = 0; i < 79; ++i) reg[(size_t) i] = reg[(size_t) i + 1];
            reg[79] = bit;
            if (filled < 80) { ++filled; if (filled < 80) return; }

            static const uint8_t sync[16] = { 0,0,1,1,1,1,1,1,1,1,1,1,1,1,0,1 };
            for (int i = 0; i < 16; ++i)
                if (reg[(size_t) (64 + i)] != sync[i]) return;

            auto get = [this] (int first, int count) { int v = 0; for (int i = 0; i < count; ++i) v |= reg[(size_t) (first + i)] << i; return v; };
            Frame f;
            f.time.frames  = get (0, 4)  + 10 * get (8, 2);
            f.time.seconds = get (16, 4) + 10 * get (24, 3);
            f.time.minutes = get (32, 4) + 10 * get (40, 3);
            f.time.hours   = get (48, 4) + 10 * get (56, 2);
            f.drop = reg[10] != 0;
            f.endSample = sampleCount;
            if (f.time.frames < 30 && f.time.seconds < 60 && f.time.minutes < 60 && f.time.hours < 24)
                onFrame (f);
        }

        double sr = 48000.0, bitLen = 24.0;
        float last = 0.0f, peak = 0.0f, hysteresis = 0.02f;
        bool level = false, halfPending = false;
        juce::int64 lastEdge = -1, sampleCount = 0;
        std::array<uint8_t, 80> reg {};
        int filled = 0;
    };
}

} // namespace hawthorne
