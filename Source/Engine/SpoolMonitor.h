#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <cmath>
#include <vector>

namespace hawthorne
{

//==============================================================================
/**
    Lifter defeat: hearing the tape while it spools.

    In fast wind the tape passes the repro head at 10-50x speed, so only its
    low-frequency content ends up audible (shifted way up: the "chatter").
    A background thread keeps a decimated mono overview of the reel (sample
    rate / 32, so content up to ~700 Hz), and the audio thread plays that
    back at the spooling speed. Cheap at any speed, no disk access in wind.
*/
class SpoolMonitor : private juce::Thread
{
public:
    static constexpr int decim = 32;

    SpoolMonitor() : juce::Thread ("Hawthorne spool overview") {}
    ~SpoolMonitor() override { stopThread (10000); }

    /** Message thread: (re)build the overview from these tape files. */
    void request (const juce::Array<juce::File>& files)
    {
        {
            const juce::ScopedLock sl (jobLock);
            job = files;
            ++jobId;
        }
        if (! isThreadRunning()) startThread (juce::Thread::Priority::low);
        notify();
    }

    bool isReady() const noexcept { return ready.load(); }

    /** Audio thread. tapePos / tapeVel in tape samples and tape samples per second.
        Adds into out. Fades in / out when active changes. */
    void render (float* out, int n, bool active, double tapePos, double tapeVel, double sr) noexcept
    {
        if (! active && gain < 1.0e-5)
        {
            gain = 0.0; pos = tapePos; vel = 0.0; hpX = hpY = lp = 0.0;
            return;
        }

        const juce::SpinLock::ScopedTryLockType sl (dataLock);
        if (! sl.isLocked() || data.empty())
            return;

        const double velA  = 1.0 - std::exp (-1.0 / (0.03 * sr));     // smooth the 30 Hz speed updates
        const double posA  = 1.0 - std::exp (-1.0 / (0.25 * sr));     // stay locked to the transport
        const double gainA = 1.0 - std::exp (-1.0 / (0.02 * sr));
        const double hpC   = std::exp (-juce::MathConstants<double>::twoPi * 350.0 / sr);
        const double lpC   = 1.0 - std::exp (-juce::MathConstants<double>::twoPi * 14000.0 / sr);
        const double target = active ? level : 0.0;
        const auto size = (juce::int64) data.size();

        for (int i = 0; i < n; ++i)
        {
            vel += (tapeVel - vel) * velA;
            pos += vel / sr;
            pos += (tapePos - pos) * posA;
            gain += (target - gain) * gainA;

            const double x = pos / (double) decim;
            const auto i0 = (juce::int64) std::floor (x);
            double s = 0.0;
            if (i0 >= 0 && i0 + 1 < size)
            {
                const double f = x - (double) i0;
                s = data[(size_t) i0] + f * (data[(size_t) i0 + 1] - data[(size_t) i0]);
            }

            // repro head: rising with frequency, top rolled off
            const double h = hpC * (hpY + s - hpX);
            hpX = s; hpY = h;
            lp += (h - lp) * lpC;

            out[i] += (float) (lp * gain);
        }
    }

private:
    void run() override
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();

        while (! threadShouldExit())
        {
            juce::Array<juce::File> files;
            int id;
            {
                const juce::ScopedLock sl (jobLock);
                files = job;
                id = jobId;
            }

            if (id != builtId)
            {
                std::vector<float> fresh;
                if (build (fm, files, id, fresh))
                {
                    {
                        const juce::SpinLock::ScopedLockType sl (dataLock);
                        std::swap (data, fresh);
                    }
                    builtId = id;
                    ready = true;
                }
                continue;      // old data freed here, off the audio thread
            }

            wait (-1);
        }
    }

    bool build (juce::AudioFormatManager& fm, const juce::Array<juce::File>& files, int id, std::vector<float>& outData)
    {
        int used = 0;
        std::vector<double> acc;
        juce::AudioBuffer<float> buf (1, 64 * decim * 32);

        for (auto& f : files)
        {
            std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
            if (r == nullptr || r->lengthInSamples <= 0) continue;

            const auto outLen = (size_t) (r->lengthInSamples / decim + 1);
            if (acc.size() < outLen) acc.resize (outLen, 0.0);
            bool any = false;

            // two cascaded box averages: a cheap anti-alias for the decimation
            double box = 0.0, prev = 0.0;
            for (juce::int64 at = 0; at < r->lengthInSamples; at += buf.getNumSamples())
            {
                if (threadShouldExit() || id != jobId.load()) return false;
                const int len = (int) juce::jmin ((juce::int64) buf.getNumSamples(), r->lengthInSamples - at);
                r->read (&buf, 0, len, at, true, false);
                const float* d = buf.getReadPointer (0);
                for (int k = 0; k < len; ++k)
                {
                    box += d[k];
                    if (((at + k + 1) % decim) == 0)
                    {
                        const double avg = box / decim;
                        box = 0.0;
                        const size_t o = (size_t) ((at + k) / decim);
                        const double v = 0.5 * (avg + prev);
                        prev = avg;
                        if (v != 0.0) any = true;
                        acc[o] += v;
                    }
                }
            }
            if (any) ++used;
        }

        const double norm = used > 0 ? 1.0 / std::sqrt ((double) used) : 0.0;
        outData.resize (acc.size());
        for (size_t k = 0; k < acc.size(); ++k) outData[k] = (float) (acc[k] * norm);
        return true;
    }

    juce::CriticalSection jobLock;
    juce::Array<juce::File> job;
    std::atomic<int> jobId { 0 };
    int builtId = -1;
    std::atomic<bool> ready { false };

    juce::SpinLock dataLock;
    std::vector<float> data;

    // audio thread
    double pos = 0.0, vel = 0.0, gain = 0.0, hpX = 0.0, hpY = 0.0, lp = 0.0;
    static constexpr double level = 0.7;
};

} // namespace hawthorne
