#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace hawthorne
{

//==============================================================================
/**
    Reverse play: the tape running backwards past the repro head.

    The normal tape readers buffer ahead of the tape, so they can't feed a tape
    running the other way. This keeps a few seconds of every track (and the
    master) in memory *behind* the play position, read by a background thread
    with its own file readers, and refilled before the tape gets to the start
    of it. Three windows rotate, so the one the audio thread is reading is
    never the one being filled.

    Message thread: setFiles(), prime().  Audio thread: setPosition(), read(), setActive().
*/
class ReverseFeeder : private juce::Thread
{
public:
    ReverseFeeder() : juce::Thread ("Hawthorne reverse play") { formats.registerBasicFormats(); }
    ~ReverseFeeder() override { stopThread (4000); }

    /** The tape: one file per channel (tracks, then the master L / R); an empty File = nothing on it. */
    void setFiles (const std::vector<juce::File>& perChannel, double sampleRate)
    {
        const juce::ScopedLock sl (fillLock);
        files = perChannel;
        sr = juce::jmax (1000.0, sampleRate);
        readers.clear();
        for (auto& f : files)
            readers.emplace_back (f.existsAsFile() ? formats.createReaderFor (f) : nullptr);
        current.store (-1);          // nothing valid until the next fill
    }

    /** Fill a window around `pos` now (message thread, before rolling backwards). */
    void prime (juce::int64 pos)
    {
        playPos.store (pos);
        fill (pos);
        if (! isThreadRunning()) startThread (juce::Thread::Priority::high);
    }

    void setActive (bool on) noexcept       { active.store (on); if (on) notify(); }
    bool isActive() const noexcept          { return active.load(); }
    void setPosition (juce::int64 p) noexcept { playPos.store (p, std::memory_order_relaxed); }

    /** Audio thread: tape samples [start, start + len) of a channel. False if nothing is on it. */
    bool read (int ch, juce::int64 start, int len, float* dest) noexcept
    {
        const int c = current.load (std::memory_order_acquire);
        if (c < 0) return false;
        const auto& w = win[(size_t) c];
        if (! juce::isPositiveAndBelow (ch, (int) w.has.size()) || ! w.has[(size_t) ch]) return false;

        juce::FloatVectorOperations::clear (dest, len);
        const auto s0 = juce::jmax (start, w.start), s1 = juce::jmin (start + len, w.end);
        if (s1 > s0)
            juce::FloatVectorOperations::copy (dest + (s0 - start), w.ch[(size_t) ch].data() + (s0 - w.start), (int) (s1 - s0));
        if (s0 != start || s1 != start + len) underruns.fetch_add (1, std::memory_order_relaxed);
        return true;
    }

    std::atomic<int> underruns { 0 };

private:
    struct Window
    {
        juce::int64 start = 0, end = 0;
        std::vector<std::vector<float>> ch;
        std::vector<char> has;
    };

    static constexpr double backSeconds = 4.0, aheadSeconds = 0.6, refillBehind = 1.5, refillAhead = 0.3;

    void run() override
    {
        while (! threadShouldExit())
        {
            if (active.load())
            {
                const auto p = playPos.load();
                const int c = current.load();
                const bool need = c < 0
                    || p - win[(size_t) c].start < (juce::int64) (refillBehind * sr)
                    || win[(size_t) c].end - p < (juce::int64) (refillAhead * sr);
                if (need) fill (p);
            }
            wait (active.load() ? 10 : 250);
        }
    }

    void fill (juce::int64 p)
    {
        const juce::ScopedLock sl (fillLock);
        const int c = current.load();
        const int next = (c + 1) % 3;      // (never the window being read, nor the one before it)
        auto& w = win[(size_t) next];

        const auto len = (juce::int64) ((backSeconds + aheadSeconds) * sr);
        w.start = juce::jmax ((juce::int64) 0, p - (juce::int64) (backSeconds * sr));
        w.end = w.start + len;
        w.ch.resize (readers.size());
        w.has.assign (readers.size(), 0);

        for (size_t i = 0; i < readers.size(); ++i)
        {
            auto* r = readers[i].get();
            if (r == nullptr) continue;
            auto& buf = w.ch[i];
            buf.resize ((size_t) len);
            float* d = buf.data();
            if (w.start < r->lengthInSamples)
                r->read (&d, 1, w.start, (int) len);        // (past the end of the file reads silence)
            else
                std::fill (buf.begin(), buf.end(), 0.0f);
            w.has[i] = 1;
        }
        current.store (next, std::memory_order_release);
    }

    juce::AudioFormatManager formats;
    juce::CriticalSection fillLock;
    std::vector<juce::File> files;
    std::vector<std::unique_ptr<juce::AudioFormatReader>> readers;
    double sr = 48000.0;
    std::array<Window, 3> win;
    std::atomic<int> current { -1 };
    std::atomic<juce::int64> playPos { 0 };
    std::atomic<bool> active { false };
};

} // namespace hawthorne
