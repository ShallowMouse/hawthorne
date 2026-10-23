// Headless torture tests for the Hawthorne tape engine.
// A fake device thread drives TapeEngine::process() in (accelerated) real time
// with an input signal that encodes the device sample clock, so every recorded
// sample can be checked for exact value and alignment.

#include "../Source/Engine/TapeEngine.h"
#include <cstdio>
#include <random>
#if JUCE_LINUX || JUCE_MAC
 #include <sys/wait.h>
 #include <unistd.h>
#endif

using namespace hawthorne;
using juce::int64;

static int failures = 0;
#define CHECK(cond, ...) do { if (! (cond)) { ++failures; std::printf ("  FAIL %s:%d: ", __FILE__, __LINE__); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

static constexpr double kSR = 48000.0;
static double testSpeed() { auto v = juce::SystemStats::getEnvironmentVariable ("HAWTHORNE_TEST_SPEED", "8").getDoubleValue(); return v > 0 ? v : 8.0; }
static constexpr int kInLat = 100, kOutLat = 200, kLat = kInLat + kOutLat;
static constexpr int64 kCodeRange = 1 << 22;

// Input value = device clock encoded in 2^-22 steps (exact in 24-bit PCM).
static float encode (int track, int64 clock) { return (float) ((clock + track * 100000) % kCodeRange) / (float) kCodeRange - 0.5f; }
static int64 decode (int track, float v)
{
    auto c = (int64) std::llround (((double) v + 0.5) * (double) kCodeRange) - track * 100000;
    return ((c % kCodeRange) + kCodeRange) % kCodeRange;
}

//==============================================================================
struct FakeDevice : juce::Thread
{
    FakeDevice (TapeEngine& e, double speedFactor, int maxBlockSize, bool variable)
        : juce::Thread ("FakeAudio"), engine (e), speed (speedFactor), maxBlock (maxBlockSize), variableBlocks (variable) {}
    ~FakeDevice() override { stopThread (5000); }

    void run() override
    {
        juce::AudioBuffer<float> in (kNumTracks, maxBlock), out (2, maxBlock);
        std::mt19937 rng (1234);
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        int64 rendered = 0;

        while (! threadShouldExit())
        {
            const int n = variableBlocks ? 1 + (int) (rng() % (unsigned) maxBlock) : maxBlock;

            for (int t = 0; t < kNumTracks; ++t)
                for (int k = 0; k < n; ++k)
                    in.setSample (t, k, encode (t, clock + k));

            const auto posBefore = engine.getPosition();
            const bool rolling = engine.isPlaying();
            engine.process (in.getArrayOfReadPointers(), kNumTracks, out.getArrayOfWritePointers(), 2, n);

            if (capture.load() && rolling)
            {
                const juce::ScopedLock sl (captureLock);
                for (int k = 0; k < n; ++k)
                    captured.push_back ({ posBefore + k, out.getSample (0, k) });
            }

            clock += n;
            rendered += n;

            const double due = t0 + (double) rendered / kSR * 1000.0 / speed;
            const double now = juce::Time::getMillisecondCounterHiRes();
            if (due > now)
                juce::Thread::sleep ((int) (due - now));
        }
    }

    TapeEngine& engine;
    const double speed;
    const int maxBlock;
    const bool variableBlocks;
    std::atomic<int64> clock { 0 };
    std::atomic<bool> capture { false };
    juce::CriticalSection captureLock;
    std::vector<std::pair<int64, float>> captured;
};

// Message-thread stand-in: wait while ticking the engine like the UI timer does.
static void pump (TapeEngine& e, int ms)
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
    while (juce::Time::getMillisecondCounter() < end)
    {
        e.tick();
        juce::Thread::sleep (20);
    }
    e.tick();
}

static std::vector<float> readWav (const juce::File& f)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
    if (r == nullptr) return {};
    juce::AudioBuffer<float> b (1, (int) r->lengthInSamples);
    r->read (&b, 0, (int) r->lengthInSamples, 0, true, false);
    return std::vector<float> (b.getReadPointer (0), b.getReadPointer (0) + b.getNumSamples());
}

/** Checks a take is contiguous input starting at some clock; returns that clock (or -1). */
static int64 verifyContiguousTake (int track, const std::vector<float>& data, bool allowSilentGaps, int64& silentSamples)
{
    silentSamples = 0;
    if (data.empty()) return -1;
    const auto c0 = decode (track, data[0]);
    int64 bad = 0;

    for (size_t k = 0; k < data.size(); ++k)
    {
        const float expected = encode (track, c0 + (int64) k);
        if (std::abs (data[k] - expected) < 1.0e-6f) continue;
        if (allowSilentGaps && data[k] == 0.0f) { ++silentSamples; continue; }
        if (bad++ < 3) std::printf ("    track %d sample %zu: got %.8f expected %.8f\n", track + 1, k, data[k], expected);
    }

    return bad == 0 ? c0 : -1;
}

static juce::File makeTempDir (const juce::String& name)
{
    auto d = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("hawthorne-tests").getChildFile (name);
    d.deleteRecursively();
    d.createDirectory();
    return d;
}

static void prepareEngine (TapeEngine& e, int block)
{
    juce::BigInteger inputs;
    inputs.setRange (0, kNumTracks, true);
    juce::StringArray names;
    for (int i = 0; i < kNumTracks; ++i) names.add ("In " + juce::String (i + 1));
    e.prepare (kSR, block, inputs, names, kInLat, kOutLat);
}

//==============================================================================
static void testFullRecordPlaybackPunch()
{
    std::printf ("[1] 24 tracks, %.0f s of audio, variable blocks, %.0fx realtime\n", 5.0 * testSpeed(), testSpeed());
    juce::String err;
    auto dir = makeTempDir ("full");
    auto session = Session::create (dir, "Full", kSR, err);
    CHECK (session != nullptr, "create: %s", err.toRawUTF8());
    if (session == nullptr) return;

    TapeEngine e;
    juce::StringArray alerts;
    e.onAlert = [&] (auto& t, auto& m) { alerts.add (t + ": " + m); };
    e.setSession (session.get());
    prepareEngine (e, 512);

    FakeDevice dev (e, testSpeed(), 512, true);
    dev.startThread (juce::Thread::Priority::highest);

    for (int t = 0; t < kNumTracks; ++t) e.setArmed (t, true);
    err = e.record();
    CHECK (err.isEmpty(), "record: %s", err.toRawUTF8());
    pump (e, 5000);
    e.stop();

    CHECK (e.getDroppedSamples() == 0, "dropped %lld", (long long) e.getDroppedSamples());
    CHECK (alerts.isEmpty(), "alerts: %s", alerts.joinIntoString (" | ").toRawUTF8());

    int64 firstClock = -2;
    int64 takeLen = 0;
    for (int t = 0; t < kNumTracks; ++t)
    {
        auto& regs = session->tracks[(size_t) t].regions;
        CHECK (regs.size() == 1, "track %d regions %zu", t + 1, regs.size());
        if (regs.size() != 1) continue;
        auto& r = regs[0];
        CHECK (! r.pending && r.start == 0 && r.offset == kLat, "track %d region start %lld offset %lld", t + 1, (long long) r.start, (long long) r.offset);

        auto data = readWav (session->resolve (r));
        CHECK ((int64) data.size() == r.length + r.offset, "track %d file %zu vs region %lld", t + 1, data.size(), (long long) (r.length + r.offset));
        int64 silent;
        auto c0 = verifyContiguousTake (t, data, false, silent);
        CHECK (c0 >= 0, "track %d data not contiguous", t + 1);
        if (firstClock == -2) firstClock = c0;
        CHECK (c0 == firstClock, "track %d starts at clock %lld, track 1 at %lld (misaligned)", t + 1, (long long) c0, (long long) firstClock);
        takeLen = (int64) data.size();
    }
    std::printf ("    take length %.2f s, all 24 tracks sample-aligned\n", (double) takeLen / kSR);

    // ---- Playback: output must be the exact sum of the takes, latency-compensated
    std::printf ("[2] playback accuracy\n");
    auto checkPlayback = [&] (const char* label)
    {
        for (int t = 0; t < kNumTracks; ++t) e.setArmed (t, false);
        e.locate (0);
        { const juce::ScopedLock sl (dev.captureLock); dev.captured.clear(); }
        dev.capture = true;
        CHECK (e.play().isEmpty(), "play");
        pump (e, 2500);
        e.stop();
        dev.capture = false;

        int64 checked = 0, wrong = 0;
        const juce::ScopedLock sl (dev.captureLock);
        for (auto& [pos, value] : dev.captured)
        {
            float expected = 0.0f;
            for (int t = 0; t < kNumTracks; ++t)
            {
                // newest region covering pos wins
                const auto& regs = session->tracks[(size_t) t].regions;
                for (auto it = regs.rbegin(); it != regs.rend(); ++it)
                    if (pos >= it->start && pos < it->start + it->length)
                    {
                        // encode() of the clock the file sample came from
                        static std::map<juce::String, int64> c0cache;
                        auto key = it->file;
                        if (c0cache.find (key) == c0cache.end())
                        {
                            auto d = readWav (session->resolve (*it));
                            c0cache[key] = d.empty() ? 0 : decode (t, d[0]);
                        }
                        expected += encode (t, c0cache[key] + it->offset + (pos - it->start));
                        break;
                    }
            }
            ++checked;
            if (std::abs (value - expected) > 2.0e-5f && wrong++ < 3)
                std::printf ("    pos %lld: got %.7f expected %.7f\n", (long long) pos, value, expected);
        }
        CHECK (checked > kSR * 2.0 * testSpeed(), "%s: only %lld samples captured", label, (long long) checked);
        CHECK (wrong == 0, "%s: %lld wrong samples", label, (long long) wrong);
        CHECK (e.getPlaybackUnderruns() == 0, "%s: %lld underruns", label, (long long) e.getPlaybackUnderruns());
        std::printf ("    %s: %lld samples exact\n", label, (long long) checked);
    };
    checkPlayback ("playback");

    // ---- Punch in on track 5 while rolling
    std::printf ("[3] punch-in while playing\n");
    e.setArmed (4, true);
    e.locate (0);
    CHECK (e.play().isEmpty(), "play");
    pump (e, 600);
    const auto punchAt = e.getPosition();
    CHECK (e.record().isEmpty(), "punch in");
    pump (e, 700);
    e.punchOut();
    CHECK (e.isPlaying() && ! e.isRecording(), "should keep rolling after punch-out");
    pump (e, 300);
    e.stop();

    auto& regs5 = session->tracks[4].regions;
    CHECK (regs5.size() == 2, "track 5 regions %zu", regs5.size());
    if (regs5.size() == 2)
    {
        auto& r = regs5[1];
        CHECK (std::llabs (r.start - (punchAt - kLat)) < 48000, "punch start %lld vs press %lld", (long long) r.start, (long long) punchAt);
        int64 silent;
        CHECK (verifyContiguousTake (4, readWav (session->resolve (r)), false, silent) >= 0, "punch take data");
        std::printf ("    punch take %.2f s at %.2f s\n", (double) r.length / kSR, (double) r.start / kSR);
    }
    checkPlayback ("playback after punch");

    // ---- Reload from disk must be identical
    std::printf ("[4] session reload\n");
    auto reloaded = Session::load (session->getFile(), err);
    CHECK (reloaded != nullptr, "reload: %s", err.toRawUTF8());
    if (reloaded != nullptr)
        for (int t = 0; t < kNumTracks; ++t)
        {
            auto& a = session->tracks[(size_t) t].regions;
            auto& b = reloaded->tracks[(size_t) t].regions;
            CHECK (a.size() == b.size(), "track %d region count", t + 1);
            for (size_t i = 0; i < juce::jmin (a.size(), b.size()); ++i)
                CHECK (a[i].file == b[i].file && a[i].start == b[i].start && a[i].length == b[i].length && a[i].offset == b[i].offset,
                       "track %d region %zu differs", t + 1, i);
        }

    dev.stopThread (2000);
    e.setSession (nullptr);
}

//==============================================================================
static void testDiskOverflowKeepsAlignment()
{
    std::printf ("[5] disk overflow: tiny buffer, 60x realtime\n");
    juce::String err;
    auto dir = makeTempDir ("overflow");
    auto session = Session::create (dir, "Overflow", kSR, err);
    if (session == nullptr) { CHECK (false, "%s", err.toRawUTF8()); return; }

    TapeEngine e;
    juce::StringArray alerts;
    e.onAlert = [&] (auto& t, auto&) { alerts.add (t); };
    e.fifoSizeOverride = 1024;
    e.setSession (session.get());
    prepareEngine (e, 256);

    FakeDevice dev (e, 60.0, 256, false);
    dev.startThread (juce::Thread::Priority::highest);
    for (int t = 0; t < 8; ++t) e.setArmed (t, true);
    CHECK (e.record().isEmpty(), "record");
    pump (e, 1000);
    e.stop();
    dev.stopThread (2000);

    CHECK (e.getDroppedSamples() > 0, "expected drops with a 1024-sample buffer");
    CHECK (alerts.contains ("Disk could not keep up"), "user was not warned");

    for (int t = 0; t < 8; ++t)
    {
        auto& r = session->tracks[(size_t) t].regions[0];
        auto data = readWav (session->resolve (r));
        CHECK ((int64) data.size() == r.length + r.offset, "track %d length mismatch", t + 1);
        int64 silent;
        auto c0 = verifyContiguousTake (t, data, true, silent);
        CHECK (c0 >= 0, "track %d: audio after a drop is misaligned", t + 1);
        if (t == 0)
            std::printf ("    %.1f s recorded, %.0f ms dropped -> silence, every surviving sample at its correct position\n",
                         (double) data.size() / kSR, (double) silent / kSR * 1000.0);
    }
    e.setSession (nullptr);
}

//==============================================================================
static void testDeviceLossDuringRecording()
{
    std::printf ("[6] audio device dies mid-take\n");
    juce::String err;
    auto dir = makeTempDir ("devloss");
    auto session = Session::create (dir, "DevLoss", kSR, err);
    if (session == nullptr) { CHECK (false, "%s", err.toRawUTF8()); return; }

    TapeEngine e;
    juce::StringArray alerts;
    e.onAlert = [&] (auto& t, auto&) { alerts.add (t); };
    e.setSession (session.get());
    prepareEngine (e, 128);

    auto dev = std::make_unique<FakeDevice> (e, testSpeed() / 2, 128, false);
    dev->startThread (juce::Thread::Priority::highest);
    e.setArmed (0, true);
    CHECK (e.record().isEmpty(), "record");
    pump (e, 800);
    dev.reset();               // callbacks stop
    e.audioDeviceStopped();    // driver reports it
    pump (e, 100);

    CHECK (! e.isRecording() && ! e.isPlaying(), "transport should stop");
    CHECK (alerts.contains ("Audio device stopped"), "user was not told");
    auto& regs = session->tracks[0].regions;
    CHECK (regs.size() == 1 && ! regs[0].pending && regs[0].length > kSR, "take not kept");
    e.setSession (nullptr);
}

//==============================================================================
#if JUCE_LINUX || JUCE_MAC
static void testCrashRecovery()
{
    std::printf ("[7] hard crash mid-take (process killed), then recover\n");
    auto dir = makeTempDir ("crash");
    auto sessionFile = dir.getChildFile ("Crash").getChildFile ("Crash.hawthorne");

    const pid_t pid = fork();
    if (pid == 0)
    {
        juce::String err;
        auto session = Session::create (dir, "Crash", kSR, err);
        TapeEngine e;
        e.setSession (session.get());
        prepareEngine (e, 256);
        FakeDevice dev (e, 1.0, 256, false);
        dev.startThread (juce::Thread::Priority::highest);
        for (int t = 0; t < 4; ++t) e.setArmed (t, true);
        e.record();
        pump (e, 3000);
        std::_Exit (0); // no destructors, no header finalisation - like a crash
    }

    int status = 0;
    waitpid (pid, &status, 0);

    juce::String err;
    auto session = Session::load (sessionFile, err);
    CHECK (session != nullptr, "load after crash: %s", err.toRawUTF8());
    if (session == nullptr) return;

    for (auto& line : session->recoveryLog)
        std::printf ("    %s\n", line.toRawUTF8());

    for (int t = 0; t < 4; ++t)
    {
        auto& regs = session->tracks[(size_t) t].regions;
        CHECK (regs.size() == 1, "track %d: take not recovered", t + 1);
        if (regs.size() != 1) continue;
        CHECK (! regs[0].pending, "still pending");
        CHECK (regs[0].length > (int64) (kSR * 2.7), "track %d recovered only %.2f s of ~3 s", t + 1, (double) regs[0].length / kSR);
        int64 silent;
        CHECK (verifyContiguousTake (t, readWav (session->resolve (regs[0])), false, silent) >= 0, "recovered data corrupt");
    }
}
#endif

//==============================================================================
int main()
{
    juce::ScopedJuceInitialiser_GUI init;

    testFullRecordPlaybackPunch();
    testDiskOverflowKeepsAlignment();
    testDeviceLossDuringRecording();
   #if JUCE_LINUX || JUCE_MAC
    testCrashRecovery();
   #endif

    std::printf (failures == 0 ? "\nALL TESTS PASSED\n" : "\n%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
