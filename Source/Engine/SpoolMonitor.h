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
    A background thread keeps a decimated overview of every track (sample
    rate / 32, so content up to ~700 Hz), and the audio thread plays each one
    back at the spooling speed - through the track's own repro path, so it
    shows on the track meters, the mix and the master, as on a real machine.
    Cheap at any speed, no disk access in wind.

    Audio thread, per block:  beginBlock() ... renderTrack (t) for each track ... endBlock().
*/
class SpoolMonitor : private juce::Thread
{
public:
    static constexpr int decim = 32;
    static constexpr int maxTracks = 96;
    static constexpr int maxBlock = 4096;

    SpoolMonitor() : juce::Thread ("Hawthorne spool overview")
    {
        posBuf.resize ((size_t) maxBlock);
        gainBuf.resize ((size_t) maxBlock);
        state.resize ((size_t) maxTracks);
    }
    ~SpoolMonitor() override { stopThread (10000); }

    /** Message thread: (re)build the overview from these tape files (one per track, a missing file = silent track). */
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

    /** Audio thread: the shared transport motion for this block. Returns false if there's nothing to play. */
    bool beginBlock (int n, bool active, double tapePos, double tapeVel, double sr) noexcept
    {
        blockN = juce::jmin (n, maxBlock);
        if (! active && gain < 1.0e-5)
        {
            gain = 0.0; pos = tapePos; vel = 0.0;
            for (auto& s : state) s = {};
            return false;
        }
        locked = dataLock.tryEnter();
        if (! locked || data.empty())
        {
            if (locked) { dataLock.exit(); locked = false; }
            return false;
        }

        const double velA  = 1.0 - std::exp (-1.0 / (0.03 * sr));     // smooth the 30 Hz speed updates
        const double posA  = 1.0 - std::exp (-1.0 / (0.25 * sr));     // stay locked to the transport
        const double gainA = 1.0 - std::exp (-1.0 / (0.02 * sr));
        const double target = active ? level : 0.0;
        for (int i = 0; i < blockN; ++i)
        {
            vel += (tapeVel - vel) * velA;
            pos += vel / sr;
            pos += (tapePos - pos) * posA;
            gain += (target - gain) * gainA;
            posBuf[(size_t) i] = pos / (double) decim;
            gainBuf[(size_t) i] = (float) gain;
        }
        hpC = std::exp (-juce::MathConstants<double>::twoPi * 350.0 / sr);
        lpC = 1.0 - std::exp (-juce::MathConstants<double>::twoPi * 14000.0 / sr);
        return true;
    }

    /** Audio thread, between beginBlock and endBlock: the chatter on one track (overwrites out). */
    bool renderTrack (int t, float* out, int n) noexcept
    {
        if (! locked || ! juce::isPositiveAndBelow (t, (int) data.size()) || data[(size_t) t].empty())
            return false;
        const auto& d = data[(size_t) t];
        const auto size = (juce::int64) d.size();
        auto& st = state[(size_t) juce::jmin (t, maxTracks - 1)];
        const float scale = 1.0f / 32767.0f;
        n = juce::jmin (n, blockN);
        for (int i = 0; i < n; ++i)
        {
            const double x = posBuf[(size_t) i];
            const auto i0 = (juce::int64) std::floor (x);
            double s = 0.0;
            if (i0 >= 0 && i0 + 1 < size)
            {
                const double f = x - (double) i0;
                s = scale * ((double) d[(size_t) i0] + f * (double) (d[(size_t) i0 + 1] - d[(size_t) i0]));
            }
            // repro head: rising with frequency, top rolled off
            const double h = hpC * (st.hpY + s - st.hpX);
            st.hpX = s; st.hpY = h;
            st.lp += (h - st.lp) * lpC;
            out[i] = (float) (st.lp * gainBuf[(size_t) i]);
        }
        return true;
    }

    void endBlock() noexcept
    {
        if (locked) { dataLock.exit(); locked = false; }
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
                std::vector<std::vector<int16_t>> fresh;
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

    bool build (juce::AudioFormatManager& fm, const juce::Array<juce::File>& files, int id, std::vector<std::vector<int16_t>>& out)
    {
        out.clear();
        out.resize ((size_t) juce::jmin (files.size(), maxTracks));
        juce::AudioBuffer<float> buf (1, 64 * decim * 32);

        for (int t = 0; t < (int) out.size(); ++t)
        {
            const auto& f = files[t];
            if (! f.existsAsFile()) continue;
            std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
            if (r == nullptr || r->lengthInSamples <= 0) continue;

            auto& o = out[(size_t) t];
            o.assign ((size_t) (r->lengthInSamples / decim + 1), 0);
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
                        const double v = 0.5 * (avg + prev);
                        prev = avg;
                        if (v != 0.0) any = true;
                        o[(size_t) ((at + k) / decim)] = (int16_t) juce::jlimit (-32767, 32767, (int) std::lround (v * 32767.0));
                    }
                }
            }
            if (! any) o.clear();
        }
        return true;
    }

    juce::CriticalSection jobLock;
    juce::Array<juce::File> job;
    std::atomic<int> jobId { 0 };
    int builtId = -1;
    std::atomic<bool> ready { false };

    juce::SpinLock dataLock;
    std::vector<std::vector<int16_t>> data;

    // audio thread
    struct TrackState { double hpX = 0.0, hpY = 0.0, lp = 0.0; };
    std::vector<TrackState> state;
    std::vector<double> posBuf;
    std::vector<float> gainBuf;
    double pos = 0.0, vel = 0.0, gain = 0.0, hpC = 0.0, lpC = 0.0;
    int blockN = 0;
    bool locked = false;
    static constexpr double level = 0.7;
};

} // namespace hawthorne
