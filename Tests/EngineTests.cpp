// Headless torture tests for the Hawthorne tape engine.
// A fake device thread drives TapeEngine::process() in (accelerated) real time
// with an input signal that encodes the device sample clock, so every sample
// on tape can be checked for exact value and position.

#include "../Source/Engine/TapeEngine.h"
#include <cstdio>
#include <random>
#if JUCE_LINUX || JUCE_MAC
 #include <sys/wait.h>
 #include <unistd.h>
#endif

using namespace hawthorne;
using juce::int64;
using Tape = std::vector<float>;

static int failures = 0;
#define CHECK(cond, ...) do { if (! (cond)) { ++failures; std::printf ("  FAIL %s:%d: ", __FILE__, __LINE__); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

static constexpr double kSR = 48000.0;
static constexpr int kInLat = 100, kOutLat = 200, kLat = kInLat + kOutLat;
static constexpr int64 kCodeRange = 1 << 22;
static double testSpeed() { auto v = juce::SystemStats::getEnvironmentVariable ("HAWTHORNE_TEST_SPEED", "8").getDoubleValue(); return v > 0 ? v : 8.0; }

// Input value = device clock encoded in 2^-22 steps (survives 24-bit PCM exactly).
static float encode (int track, int64 clock) { return (float) ((clock + track * 100000) % kCodeRange) / (float) kCodeRange - 0.5f; }
static int64 decode (int track, float v)
{
    auto c = (int64) std::llround (((double) v + 0.5) * (double) kCodeRange) - track * 100000;
    return ((c % kCodeRange) + kCodeRange) % kCodeRange;
}
static bool matches (int track, float v, int64 clock) { return std::abs (v - encode (track, clock)) < 1.0e-6f; }

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

static void pump (TapeEngine& e, int ms)
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
    while (juce::Time::getMillisecondCounter() < end) { e.tick(); juce::Thread::sleep (20); }
    e.tick();
}

static Tape readTape (const Session& s, int t)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (s.getTapeFile (t)));
    if (r == nullptr) return {};
    juce::AudioBuffer<float> b (1, (int) r->lengthInSamples);
    r->read (&b, 0, (int) r->lengthInSamples, 0, true, false);
    return Tape (b.getReadPointer (0), b.getReadPointer (0) + b.getNumSamples());
}

/** Length of a contiguous recorded run starting at `from` (zeros allowed if allowSilence). */
static int64 contiguousRun (int track, const Tape& d, int64 from, bool allowSilence, int64& silent)
{
    silent = 0;
    if (from >= (int64) d.size()) return 0;
    int64 base = -1;
    int64 k = from;
    for (; k < (int64) d.size(); ++k)
    {
        if (base < 0)
        {
            if (allowSilence && d[(size_t) k] == 0.0f) { ++silent; continue; }
            base = decode (track, d[(size_t) k]) - k;
        }
        if (matches (track, d[(size_t) k], base + k)) continue;
        if (allowSilence && d[(size_t) k] == 0.0f) { ++silent; continue; }
        break;
    }
    return k - from;
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
    juce::BigInteger outs;
    outs.setRange (0, 4, true);
    e.prepare (kSR, block, inputs, names, outs, { "Out 1", "Out 2", "Out 3", "Out 4" }, kInLat, kOutLat);
}

//==============================================================================
static void testTapeMachine()
{
    std::printf ("[1] record 24 tracks from zero, %.0fx realtime, variable blocks\n", testSpeed());
    juce::String err;
    auto dir = makeTempDir ("tape");
    auto session = Session::create (dir, "Tape", kSR, err);
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
    CHECK (e.record().isEmpty(), "record");
    pump (e, 5000);
    const auto recordedTimeline = e.getPosition();
    e.stop();

    CHECK (e.getDroppedSamples() == 0, "dropped %lld", (long long) e.getDroppedSamples());
    CHECK (alerts.isEmpty(), "alerts: %s", alerts.joinIntoString (" | ").toRawUTF8());

    std::array<Tape, kNumTracks> tapes;
    int64 firstBase = -1;
    for (int t = 0; t < kNumTracks; ++t)
    {
        tapes[(size_t) t] = readTape (*session, t);
        auto& d = tapes[(size_t) t];
        // Latency compensation: the first kLat captured samples fall before tape start.
        CHECK (std::llabs ((int64) d.size() - (recordedTimeline - kLat)) < 4096, "track %d tape %zu, expected ~%lld",
               t + 1, d.size(), (long long) (recordedTimeline - kLat));
        int64 silent;
        CHECK (contiguousRun (t, d, 0, false, silent) == (int64) d.size(), "track %d tape not contiguous", t + 1);
        const auto base = d.empty() ? -1 : decode (t, d[0]);
        if (firstBase < 0) firstBase = base;
        CHECK (base == firstBase, "track %d misaligned vs track 1", t + 1);
        CHECK (e.getTapeLength (t) == (int64) d.size(), "engine tape length");
    }
    std::printf ("    %.2f s on all 24 tracks, sample-aligned\n", (double) tapes[0].size() / kSR);

    //--------------------------------------------------------------------------
    auto checkPlayback = [&] (const char* label)
    {
        for (int t = 0; t < kNumTracks; ++t) { tapes[(size_t) t] = readTape (*session, t); e.setArmed (t, false); }
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
            for (auto& tp : tapes)
                if (pos < (int64) tp.size()) expected += tp[(size_t) pos];
            ++checked;
            if (std::abs (value - expected) > 2.0e-5f && wrong++ < 3)
                std::printf ("    pos %lld: got %.7f expected %.7f\n", (long long) pos, value, expected);
        }
        CHECK (checked > kSR * 2.0 * testSpeed(), "%s: only %lld samples", label, (long long) checked);
        CHECK (wrong == 0, "%s: %lld wrong samples", label, (long long) wrong);
        CHECK (e.getPlaybackUnderruns() == 0, "%s: %lld underruns", label, (long long) e.getPlaybackUnderruns());
        std::printf ("    %s: %lld samples exactly match tape\n", label, (long long) checked);
    };

    std::printf ("[2] playback = tape\n");
    checkPlayback ("playback");

    //--------------------------------------------------------------------------
    std::printf ("[3] punch-in overwrites tape in place\n");
    const auto before = readTape (*session, 4);
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
    e.setArmed (4, false);

    const auto after = readTape (*session, 4);
    CHECK (after.size() == before.size(), "punch inside tape changed its length %zu -> %zu", before.size(), after.size());
    int64 k0 = 0;
    while (k0 < (int64) after.size() && after[(size_t) k0] == before[(size_t) k0]) ++k0;
    int64 silent;
    const auto runLen = contiguousRun (4, after, k0, false, silent);
    const auto k1 = k0 + runLen;
    int64 tailBad = 0;
    for (int64 k = k1; k < (int64) after.size(); ++k) tailBad += after[(size_t) k] != before[(size_t) k] ? 1 : 0;
    CHECK (std::llabs (k0 - (punchAt - kLat)) < 48000, "punch starts at %lld, pressed at %lld", (long long) k0, (long long) punchAt);
    CHECK (runLen > kSR, "punch run only %lld samples", (long long) runLen);
    CHECK (tailBad == 0, "%lld samples after the punch were altered", (long long) tailBad);
    std::printf ("    overwrote %.2f s at %.2f s; tape before and after untouched\n", (double) runLen / kSR, (double) k0 / kSR);
    checkPlayback ("playback after punch");

    //--------------------------------------------------------------------------
    std::printf ("[4] recording past the end extends the tape\n");
    const auto orig6 = readTape (*session, 5);
    const auto gapStart = (int64) orig6.size();
    e.setArmed (5, true);
    e.locate (gapStart + (int64) (kSR * 2));
    CHECK (e.record().isEmpty(), "record past end");
    pump (e, 500);
    e.stop();
    e.setArmed (5, false);

    const auto ext = readTape (*session, 5);
    int64 changed = 0, nonZeroGap = 0;
    for (int64 k = 0; k < gapStart; ++k) changed += ext[(size_t) k] != orig6[(size_t) k] ? 1 : 0;
    const auto newStart = gapStart + (int64) (kSR * 2) - kLat;
    for (int64 k = gapStart; k < newStart && k < (int64) ext.size(); ++k) nonZeroGap += ext[(size_t) k] != 0.0f ? 1 : 0;
    const auto extRun = contiguousRun (5, ext, newStart, false, silent);
    CHECK (changed == 0, "existing tape altered (%lld)", (long long) changed);
    CHECK (nonZeroGap == 0, "gap not silent (%lld)", (long long) nonZeroGap);
    CHECK (newStart + extRun == (int64) ext.size() && extRun > kSR, "new audio run %lld, tape %zu", (long long) extRun, ext.size());
    std::printf ("    tape %.2f s -> %.2f s, 2 s silent gap, new audio exact\n", (double) gapStart / kSR, (double) ext.size() / kSR);

    //--------------------------------------------------------------------------
    std::printf ("[5] session reload\n");
    auto reloaded = Session::load (session->getFile(), err);
    CHECK (reloaded != nullptr && reloaded->recoveryLog.isEmpty(), "reload: %s", err.toRawUTF8());

    dev.stopThread (2000);
    e.setSession (nullptr);
}

//==============================================================================
static void testDiskOverflowKeepsAlignment()
{
    std::printf ("[6] disk overflow: tiny buffer, 60x realtime\n");
    juce::String err;
    auto session = Session::create (makeTempDir ("overflow"), "Overflow", kSR, err);
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
    const auto timeline = e.getPosition();
    e.stop();
    dev.stopThread (2000);

    CHECK (e.getDroppedSamples() > 0, "expected drops with a 1024-sample buffer");
    CHECK (alerts.contains ("Disk could not keep up"), "user was not warned");

    for (int t = 0; t < 8; ++t)
    {
        auto d = readTape (*session, t);
        CHECK (std::llabs ((int64) d.size() - (timeline - kLat)) < 4096, "track %d length %zu vs %lld", t + 1, d.size(), (long long) (timeline - kLat));
        int64 silent;
        CHECK (contiguousRun (t, d, 0, true, silent) == (int64) d.size(), "track %d: audio after a drop is misplaced", t + 1);
        if (t == 0)
            std::printf ("    %.1f s on tape, %.0f ms lost to silence, every surviving sample in place\n",
                         (double) d.size() / kSR, (double) silent / kSR * 1000.0);
    }
    e.setSession (nullptr);
}

//==============================================================================
static void testDeviceLossDuringRecording()
{
    std::printf ("[7] audio device dies mid-take\n");
    juce::String err;
    auto session = Session::create (makeTempDir ("devloss"), "DevLoss", kSR, err);
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
    dev.reset();
    e.audioDeviceStopped();
    pump (e, 100);

    CHECK (! e.isRecording() && ! e.isPlaying(), "transport should stop");
    CHECK (alerts.contains ("Audio device stopped"), "user was not told");
    CHECK (readTape (*session, 0).size() > (size_t) kSR, "tape not kept");
    e.setSession (nullptr);
}

//==============================================================================
#if JUCE_LINUX || JUCE_MAC
static void testCrashRecovery()
{
    std::printf ("[8] hard crash while overwriting existing tape, then reopen\n");
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
        FakeDevice dev (e, 2.0, 256, false);
        dev.startThread (juce::Thread::Priority::highest);
        for (int t = 0; t < 4; ++t) e.setArmed (t, true);
        e.record();          // first pass: ~6 s of audio
        pump (e, 3000);
        e.stop();
        e.locate (0);
        e.record();          // overwrite pass - crash ~2 s of audio in
        pump (e, 1000);
        std::_Exit (0);      // no destructors, no header update - like a crash
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
        auto d = readTape (*session, t);
        int64 silent;
        const auto newRun = contiguousRun (t, d, 0, false, silent);           // overwrite pass
        const auto oldRun = contiguousRun (t, d, newRun, false, silent);      // original take resumes
        CHECK (d.size() > (size_t) (kSR * 5.5), "track %d tape %.2f s, expected ~6 s", t + 1, (double) d.size() / kSR);
        CHECK (newRun > (int64) (kSR * 1.5), "track %d kept only %.2f s of the interrupted overwrite", t + 1, (double) newRun / kSR);
        CHECK (newRun + oldRun == (int64) d.size(), "track %d: tape corrupt after crash", t + 1);
        if (t == 0)
            std::printf ("    tape: %.2f s of new take, then original take intact to %.2f s\n",
                         (double) newRun / kSR, (double) d.size() / kSR);
    }
}
#endif

//==============================================================================
static void testRouting()
{
    std::printf ("[9] routing matrix: mix, direct outs, master in/out\n");
    juce::String err;
    auto session = Session::create (makeTempDir ("routing"), "Routing", kSR, err);
    if (session == nullptr) { CHECK (false, "%s", err.toRawUTF8()); return; }

    TapeEngine e;
    e.setSession (session.get());
    prepareEngine (e, 256);

    for (int t = 0; t < 3; ++t) e.setArmed (t, true);       // stopped: armed tracks monitor input
    e.setTrackToMix (2, false);                              // track 3 out of the mix...
    e.setTrackOutputs (2, (ChannelMask) 1 << 2);             // ...direct out to output 3
    e.setMasterInputs (0, (ChannelMask) 1 << 23);            // input 24 summed into master L
    e.setMasterOutputs (1, ((ChannelMask) 1 << 1) | ((ChannelMask) 1 << 3)); // master R -> outs 2 and 4

    const int n = 256;
    juce::AudioBuffer<float> in (kNumTracks, n), out (4, n);
    for (int t = 0; t < kNumTracks; ++t)
        for (int k = 0; k < n; ++k)
            in.setSample (t, k, encode (t, k) * 0.25f);

    e.process (in.getArrayOfReadPointers(), kNumTracks, out.getArrayOfWritePointers(), 4, n);

    int64 wrong = 0;
    for (int k = 0; k < n; ++k)
    {
        const float mix = in.getSample (0, k) + in.getSample (1, k);
        const float expect[4] = { mix + in.getSample (23, k), mix, in.getSample (2, k), mix };
        for (int o = 0; o < 4; ++o)
            if (std::abs (out.getSample (o, k) - expect[o]) > 1.0e-6f && wrong++ < 4)
                std::printf ("    out %d sample %d: got %.6f expected %.6f\n", o + 1, k, out.getSample (o, k), expect[o]);
    }
    CHECK (wrong == 0, "%lld routed samples wrong", (long long) wrong);

    auto reloaded = Session::load (session->getFile(), err);
    CHECK (reloaded != nullptr, "reload");
    if (reloaded != nullptr)
    {
        CHECK (! reloaded->tracks[2].toMix && reloaded->tracks[2].outputs == ((ChannelMask) 1 << 2), "track routing not saved");
        CHECK (reloaded->master.inputs[0] == ((ChannelMask) 1 << 23), "master in not saved");
        CHECK (reloaded->master.outputs[0] == 1 && reloaded->master.outputs[1] == (((ChannelMask) 1 << 1) | ((ChannelMask) 1 << 3)),
               "master out not saved");
        std::printf ("    all 4 outputs exact, routing saved and reloaded\n");
    }
    e.setSession (nullptr);
}

//==============================================================================
int main()
{
    juce::ScopedJuceInitialiser_GUI init;

    testTapeMachine();
    testDiskOverflowKeepsAlignment();
    testDeviceLossDuringRecording();
    testRouting();
   #if JUCE_LINUX || JUCE_MAC
    testCrashRecovery();
   #endif

    std::printf (failures == 0 ? "\nALL TESTS PASSED\n" : "\n%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
