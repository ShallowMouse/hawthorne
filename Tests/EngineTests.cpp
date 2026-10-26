// Headless torture tests for the Hawthorne tape engine.
// A fake device thread drives TapeEngine::process() in (accelerated) real time
// with an input signal that encodes the device sample clock, so every sample
// on tape can be checked for exact value and position.

#include "../Source/Engine/TapeEngine.h"
#include "../Source/Engine/Library.h"
#include "../Source/Network/NetSender.h"
#include "../Source/Engine/Mtc.h"
#include <deque>
#include <thread>
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
    FakeDevice (TapeEngine& e, double speedFactor, int maxBlockSize, bool variable, int inputs = kDefaultTracks)
        : juce::Thread ("FakeAudio"), engine (e), speed (speedFactor), maxBlock (maxBlockSize), variableBlocks (variable), numIn (inputs) {}
    ~FakeDevice() override { stopThread (5000); }

    void run() override
    {
        juce::AudioBuffer<float> in (numIn, maxBlock), out (2, maxBlock);
        std::mt19937 rng (1234);
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        int64 rendered = 0;

        while (! threadShouldExit())
        {
            const int n = variableBlocks ? 1 + (int) (rng() % (unsigned) maxBlock) : maxBlock;

            for (int t = 0; t < numIn; ++t)
                for (int k = 0; k < n; ++k)
                    in.setSample (t, k, encode (t, clock + k));

            const auto posBefore = engine.getPosition();
            const bool rolling = engine.isPlaying();
            engine.process (in.getArrayOfReadPointers(), numIn, out.getArrayOfWritePointers(), 2, n);

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
    const int numIn;
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

static Tape readTape (const Reel& s, int t)
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

static void prepareEngine (TapeEngine& e, int block, int numIn = kDefaultTracks)
{
    juce::BigInteger inputs;
    inputs.setRange (0, numIn, true);
    juce::StringArray names;
    for (int i = 0; i < numIn; ++i) names.add ("In " + juce::String (i + 1));
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
    auto reel = Reel::create (dir, "Tape", kSR, kDefaultTracks, err);
    CHECK (reel != nullptr, "create: %s", err.toRawUTF8());
    if (reel == nullptr) return;

    TapeEngine e;
    juce::StringArray alerts;
    e.onAlert = [&] (auto& t, auto& m) { alerts.add (t + ": " + m); };
    e.setReel (reel.get());
    prepareEngine (e, 512);

    FakeDevice dev (e, testSpeed(), 512, true);
    dev.startThread (juce::Thread::Priority::highest);

    for (int t = 0; t < kDefaultTracks; ++t) e.setArmed (t, true);
    CHECK (e.record().isEmpty(), "record");
    pump (e, 5000);
    const auto recordedTimeline = e.getPosition();
    e.stop();

    CHECK (e.getDroppedSamples() == 0, "dropped %lld", (long long) e.getDroppedSamples());
    CHECK (alerts.isEmpty(), "alerts: %s", alerts.joinIntoString (" | ").toRawUTF8());

    std::array<Tape, kDefaultTracks> tapes;
    int64 firstBase = -1;
    for (int t = 0; t < kDefaultTracks; ++t)
    {
        tapes[(size_t) t] = readTape (*reel, t);
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
        for (int t = 0; t < kDefaultTracks; ++t) { tapes[(size_t) t] = readTape (*reel, t); e.setArmed (t, false); }
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
    const auto before = readTape (*reel, 4);
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

    const auto after = readTape (*reel, 4);
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
    const auto orig6 = readTape (*reel, 5);
    const auto gapStart = (int64) orig6.size();
    e.setArmed (5, true);
    e.locate (gapStart + (int64) (kSR * 2));
    CHECK (e.record().isEmpty(), "record past end");
    pump (e, 500);
    e.stop();
    e.setArmed (5, false);

    const auto ext = readTape (*reel, 5);
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
    auto reloaded = Reel::load (reel->getFile(), err);
    CHECK (reloaded != nullptr && reloaded->recoveryLog.isEmpty(), "reload: %s", err.toRawUTF8());

    dev.stopThread (2000);
    e.setReel (nullptr);
}

//==============================================================================
static void testDiskOverflowKeepsAlignment()
{
    std::printf ("[6] disk overflow: tiny buffer, 60x realtime\n");
    juce::String err;
    auto reel = Reel::create (makeTempDir ("overflow"), "Overflow", kSR, kDefaultTracks, err);
    if (reel == nullptr) { CHECK (false, "%s", err.toRawUTF8()); return; }

    TapeEngine e;
    juce::StringArray alerts;
    e.onAlert = [&] (auto& t, auto&) { alerts.add (t); };
    e.fifoSizeOverride = 1024;
    e.setReel (reel.get());
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
        auto d = readTape (*reel, t);
        CHECK (std::llabs ((int64) d.size() - (timeline - kLat)) < 4096, "track %d length %zu vs %lld", t + 1, d.size(), (long long) (timeline - kLat));
        int64 silent;
        CHECK (contiguousRun (t, d, 0, true, silent) == (int64) d.size(), "track %d: audio after a drop is misplaced", t + 1);
        if (t == 0)
            std::printf ("    %.1f s on tape, %.0f ms lost to silence, every surviving sample in place\n",
                         (double) d.size() / kSR, (double) silent / kSR * 1000.0);
    }
    e.setReel (nullptr);
}

//==============================================================================
static void testDeviceLossDuringRecording()
{
    std::printf ("[7] audio device dies mid-take\n");
    juce::String err;
    auto reel = Reel::create (makeTempDir ("devloss"), "DevLoss", kSR, kDefaultTracks, err);
    if (reel == nullptr) { CHECK (false, "%s", err.toRawUTF8()); return; }

    TapeEngine e;
    juce::StringArray alerts;
    e.onAlert = [&] (auto& t, auto&) { alerts.add (t); };
    e.setReel (reel.get());
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
    CHECK (readTape (*reel, 0).size() > (size_t) kSR, "tape not kept");
    e.setReel (nullptr);
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
        auto reel = Reel::create (dir, "Crash", kSR, kDefaultTracks, err);
        TapeEngine e;
        e.setReel (reel.get());
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
    auto reel = Reel::load (sessionFile, err);
    CHECK (reel != nullptr, "load after crash: %s", err.toRawUTF8());
    if (reel == nullptr) return;

    for (auto& line : reel->recoveryLog)
        std::printf ("    %s\n", line.toRawUTF8());

    for (int t = 0; t < 4; ++t)
    {
        auto d = readTape (*reel, t);
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
    auto reel = Reel::create (makeTempDir ("routing"), "Routing", kSR, kDefaultTracks, err);
    if (reel == nullptr) { CHECK (false, "%s", err.toRawUTF8()); return; }

    TapeEngine e;
    e.setReel (reel.get());
    prepareEngine (e, 256);

    for (int t = 0; t < 3; ++t) e.setArmed (t, true);       // stopped: armed tracks monitor input
    e.setTrackToMix (2, false);                              // track 3 out of the mix...
    e.setTrackOutputs (2, ChannelList { 2 });                // ...direct out to output 3
    e.setMasterInputs (0, ChannelList { 23 });               // input 24 summed into master L
    e.setMasterOutputs (1, ChannelList { 1, 3 });            // master R -> outs 2 and 4

    const int n = 1024;
    juce::AudioBuffer<float> in (kDefaultTracks, n), out (4, n);
    for (int t = 0; t < kDefaultTracks; ++t)
        for (int k = 0; k < n; ++k)
            in.setSample (t, k, encode (t, k) * 0.25f);

    e.process (in.getArrayOfReadPointers(), kDefaultTracks, out.getArrayOfWritePointers(), 4, n);

    int64 wrong = 0;
    for (int k = 0; k < n; ++k)
    {
        // With an input patched to the master, the internal mix waits one
        // round trip so the returns line up with it.
        const float mix = k < kLat ? 0.0f : in.getSample (0, k - kLat) + in.getSample (1, k - kLat);
        const float expect[4] = { mix + in.getSample (23, k), mix, in.getSample (2, k), mix };
        for (int o = 0; o < 4; ++o)
            if (std::abs (out.getSample (o, k) - expect[o]) > 1.0e-6f && wrong++ < 4)
                std::printf ("    out %d sample %d: got %.6f expected %.6f\n", o + 1, k, out.getSample (o, k), expect[o]);
    }
    CHECK (wrong == 0, "%lld routed samples wrong", (long long) wrong);

    auto reloaded = Reel::load (reel->getFile(), err);
    CHECK (reloaded != nullptr, "reload");
    if (reloaded != nullptr)
    {
        CHECK (! reloaded->tracks[2].toMix && reloaded->tracks[2].outputs == ChannelList { 2 }, "track routing not saved");
        CHECK (reloaded->master.inputs[0] == ChannelList { 23 }, "master in not saved");
        CHECK (reloaded->master.outputs[0] == ChannelList { 0 } && reloaded->master.outputs[1] == ChannelList (1, 3),
               "master out not saved");
        std::printf ("    all 4 outputs exact, routing saved and reloaded\n");
    }
    e.setReel (nullptr);
}

//==============================================================================
static void testEqualLengthsAndPaperwork()
{
    std::printf ("[10] every recorded track the same length; markers, sheets, take log\n");
    juce::String err;
    auto reel = Reel::create (makeTempDir ("equal"), "Equal", kSR, kDefaultTracks, err);
    if (reel == nullptr) { CHECK (false, "%s", err.toRawUTF8()); return; }

    TapeEngine e;
    std::vector<TakeEntry> logged;
    e.onTakeLogged = [&] (const TakeEntry& t) { logged.push_back (t); };
    e.setReel (reel.get());
    prepareEngine (e, 256);
    FakeDevice dev (e, testSpeed(), 256, false);
    dev.startThread (juce::Thread::Priority::highest);

    // Take 1: tracks 1-3 and 5 from zero
    for (int t : { 0, 1, 2, 4 }) e.setArmed (t, true);
    CHECK (e.record().isEmpty(), "record take 1");
    pump (e, 300);
    e.stop();
    for (int t : { 0, 1, 2, 4 }) e.setArmed (t, false);

    // Take 2: track 9 only, starting later and running past the end of the reel
    e.setArmed (8, true);
    e.locate ((int64) (kSR * 3));
    CHECK (e.record().isEmpty(), "record take 2");
    pump (e, 600);
    e.stop();
    dev.stopThread (2000);

    const auto reelLen = (int64) readTape (*reel, 8).size();
    for (int t : { 0, 1, 2, 4, 8 })
        CHECK ((int64) readTape (*reel, t).size() == reelLen, "track %d is %zu samples, reel is %lld",
               t + 1, readTape (*reel, t).size(), (long long) reelLen);
    CHECK (! reel->getTapeFile (3).existsAsFile(), "an unrecorded track got a file");

    const auto t1 = readTape (*reel, 0);
    int64 nonZeroTail = 0;
    for (int64 k = (int64) (kSR * 3); k < (int64) t1.size(); ++k) nonZeroTail += t1[(size_t) k] != 0.0f ? 1 : 0;
    CHECK (nonZeroTail == 0, "padding on track 1 is not silent");

    CHECK (logged.size() == 2 && reel->takeLog.size() == 2, "take log has %zu entries", reel->takeLog.size());
    if (reel->takeLog.size() == 2)
    {
        CHECK (reel->takeLog[0].tracks == "1-3, 5", "take 1 tracks '%s'", reel->takeLog[0].tracks.toRawUTF8());
        CHECK (reel->takeLog[1].tracks == "9" && reel->takeLog[1].take == 2, "take 2 entry");
        CHECK (std::llabs (reel->takeLog[1].start - ((int64) (kSR * 3) - kLat)) < 4096, "take 2 start");
    }

    // Markers stay in time order; sheet fields persist
    reel->addMarker ((int64) (kSR * 30), "Chorus");
    reel->addMarker ((int64) (kSR * 5), "Verse");
    reel->addMarker (0, "Count-in");
    reel->info.set ("Producer", "George");
    reel->tracks[0].instrument = "Kick";
    reel->tracks[0].mic = "D112";
    reel->takeLog[0].status = "Keeper";
    CHECK (reel->save (err), "save");

    auto re = Reel::load (reel->getFile(), err);
    CHECK (re != nullptr, "reload");
    if (re != nullptr)
    {
        CHECK (re->markers.size() == 3 && re->markers[0].name == "Count-in" && re->markers[1].name == "Verse"
               && re->markers[2].name == "Chorus", "markers not in time order");
        CHECK (re->info["Producer"] == "George" && re->tracks[0].instrument == "Kick" && re->tracks[0].mic == "D112",
               "sheet fields lost");
        CHECK (re->takeLog.size() == 2 && re->takeLog[0].status == "Keeper" && re->nextTake == 3, "take log lost");
        std::printf ("    reel %.2f s: 5 tracks identical length, padding silent; markers sorted; paperwork saved\n",
                     (double) reelLen / kSR);
    }
    e.setReel (nullptr);
}

static void test96Tracks()
{
    std::printf ("[11] 96-track session: record all 96 at once\n");
    juce::String err;
    auto reel = Reel::create (makeTempDir ("t96"), "Ninety-six", kSR, kMaxTracks, err);
    if (reel == nullptr) { CHECK (false, "%s", err.toRawUTF8()); return; }

    TapeEngine e;
    juce::StringArray alerts;
    e.onAlert = [&] (auto& t, auto&) { alerts.add (t); };
    e.setReel (reel.get());
    prepareEngine (e, 512, kMaxTracks);
    CHECK (e.getNumTracks() == kMaxTracks, "engine has %d tracks", e.getNumTracks());

    FakeDevice dev (e, juce::jmax (1.0, testSpeed() / 4), 512, true, kMaxTracks);
    dev.startThread (juce::Thread::Priority::highest);
    for (int t = 0; t < kMaxTracks; ++t) e.setArmed (t, true);
    CHECK (e.record().isEmpty(), "record");
    pump (e, 2000);
    e.stop();
    dev.stopThread (2000);

    CHECK (e.getDroppedSamples() == 0 && alerts.isEmpty(), "drops/alerts with 96 tracks");
    int64 firstBase = -1, len = -1;
    int bad = 0;
    for (int t = 0; t < kMaxTracks; ++t)
    {
        auto d = readTape (*reel, t);
        int64 silent;
        if (len < 0) len = (int64) d.size();
        const auto base = d.empty() ? -1 : decode (t, d[0]);
        if (firstBase < 0) firstBase = base;
        if ((int64) d.size() != len || base != firstBase || contiguousRun (t, d, 0, false, silent) != (int64) d.size())
            ++bad;
    }
    CHECK (bad == 0, "%d of 96 tracks wrong", bad);
    std::printf ("    96 tracks x %.2f s, all aligned and identical length\n", (double) len / kSR);
    e.setReel (nullptr);
}

//==============================================================================
static void testLibrary()
{
    std::printf ("[12] tape library: sessions of reels, archive, converting old sessions\n");
    auto lib = makeTempDir ("library");
    juce::String err;

    auto album = Session::create (lib, "Album", err);
    CHECK (album != nullptr, "create session: %s", err.toRawUTF8());
    if (album == nullptr) return;
    album->info.set ("Artist", "The Band");
    CHECK (album->save (err), "save session");

    auto r1 = album->createReel (16, kSR, err);
    auto r2 = album->createReel (24, kSR, err);
    CHECK (r1 != nullptr && r2 != nullptr, "create reels: %s", err.toRawUTF8());
    if (r1 == nullptr || r2 == nullptr) return;
    CHECK (r1->getName() == "Reel 01" && r2->getName() == "Reel 02", "reel names %s %s", r1->getName().toRawUTF8(), r2->getName().toRawUTF8());
    CHECK (r1->numTracks == 16 && r2->numTracks == 24, "track counts");
    r2->tracks[0].name = "Kick";
    CHECK (r2->save (err), "save reel");

    // Something on tape so the shelf shows a length
    {
        TapeFile tape;
        CHECK (TapeFile::createEmpty (r2->getTapeFile (0), kSR, err) && tape.open (r2->getTapeFile (0), kSR, err)
               && tape.extendTo ((int64) (kSR * 90)) && tape.close(), "write tape");
    }

    // An old (v1.5) single-reel session sitting in the library
    const auto old = lib.getChildFile ("Old Song");
    old.getChildFile ("Audio").createDirectory();
    old.getChildFile ("Old Song.hawthorne").replaceWithText (
        "<?xml version=\"1.0\"?>\n<HawthorneSession formatVersion=\"3\" sampleRate=\"48000\" bitDepth=\"24\" numTracks=\"8\">"
        "<Track index=\"0\" name=\"Vox\" input=\"0\"/><Info><Field name=\"Artist\" value=\"Old Artist\"/>"
        "<Field name=\"Tape Stock\" value=\"456\"/></Info></HawthorneSession>");
    {
        TapeFile tape;
        CHECK (TapeFile::createEmpty (old.getChildFile ("Audio/Track01.wav"), kSR, err)
               && tape.open (old.getChildFile ("Audio/Track01.wav"), kSR, err) && tape.extendTo ((int64) kSR * 10) && tape.close(),
               "write old tape");
    }

    juce::StringArray log;
    auto shelves = Library::scan (lib, log);
    CHECK (shelves.size() == 2, "found %zu sessions", shelves.size());

    const SessionSummary* a = nullptr;
    const SessionSummary* o = nullptr;
    for (auto& s : shelves) { if (s.name == "Album") a = &s; if (s.name == "Old Song") o = &s; }

    CHECK (a != nullptr && a->reels.size() == 2 && a->info["Artist"] == "The Band", "album summary");
    if (a != nullptr && a->reels.size() == 2)
    {
        CHECK (a->reels[1].numTracks == 24 && a->reels[1].lengthSamples == (int64) (kSR * 90), "reel 2 summary");
        CHECK (a->reels[1].trackNames.size() == 1 && a->reels[1].trackNames[0].contains ("Kick"), "track names on box");
    }

    CHECK (o != nullptr && o->reels.size() == 1, "old session not converted");
    if (o != nullptr && o->reels.size() == 1)
    {
        CHECK (o->info["Artist"] == "Old Artist", "artist not carried to the session");
        CHECK (o->reels[0].name == "Reel 01" && o->reels[0].numTracks == 8 && o->reels[0].info["Tape Stock"] == "456", "converted reel");
        CHECK (o->reels[0].lengthSamples == (int64) kSR * 10, "converted audio length %lld", (long long) o->reels[0].lengthSamples);
        CHECK (log.size() == 1 && log[0].contains ("Converted"), "conversion not reported");
        auto loaded = Reel::load (o->reels[0].file, err);
        CHECK (loaded != nullptr && loaded->tracks[0].name == "Vox", "converted reel won't load: %s", err.toRawUTF8());
    }

    CHECK (Library::setArchived (r1->getFile(), true, err), "archive");
    shelves = Library::scan (lib, log);
    for (auto& s : shelves)
        if (s.name == "Album")
            CHECK (s.reels[0].archived && ! s.reels[1].archived && s.hasVisibleReels(), "archive flag");

    std::printf ("    2 sessions, 3 reels; old session became Reel 01 with its audio; archive works\n");
}


//==============================================================================
// A device whose outputs 3/4 are patched back into inputs 23/24 through D
// samples of "outboard" delay. Inputs 1-2 carry the clock code.
struct LoopbackDevice : juce::Thread
{
    LoopbackDevice (TapeEngine& e, int delaySamples) : juce::Thread ("Loopback"), engine (e), D (delaySamples) {}
    ~LoopbackDevice() override { stopThread (5000); }

    void run() override
    {
        constexpr int n = 256, numIn = kDefaultTracks;
        juce::AudioBuffer<float> in (numIn, n), out (4, n);
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        int64 clock = 0;

        while (! threadShouldExit())
        {
            in.clear();
            for (int k = 0; k < n; ++k)
            {
                in.setSample (0, k, encode (0, clock + k));
                in.setSample (1, k, encode (1, clock + k) * 0.5f);
                const int64 back = clock + k - D;
                if (back >= 0)
                {
                    in.setSample (22, k, hist[0][(size_t) back]);
                    in.setSample (23, k, hist[1][(size_t) back]);
                }
            }

            engine.process (in.getArrayOfReadPointers(), numIn, out.getArrayOfWritePointers(), 4, n);

            for (int k = 0; k < n; ++k)
            {
                hist[0].push_back (out.getSample (2, k));
                hist[1].push_back (out.getSample (3, k));
            }
            clock += n;

            const double due = t0 + (double) clock / kSR * 1000.0 / testSpeed();
            const double now = juce::Time::getMillisecondCounterHiRes();
            if (due > now) juce::Thread::sleep ((int) (due - now));
        }
    }

    TapeEngine& engine;
    const int D;
    std::array<std::vector<float>, 2> hist;
};

static Tape readFile (const juce::File& f)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
    if (r == nullptr) return {};
    juce::AudioBuffer<float> b (1, (int) r->lengthInSamples);
    r->read (&b, 0, (int) r->lengthInSamples, 0, true, false);
    return Tape (b.getReadPointer (0), b.getReadPointer (0) + b.getNumSamples());
}

/** Master L must be track1 + track2, sample for sample, wherever it was recorded. */
static void checkMasterAligned (const Reel& reel, int m, const char* what)
{
    const auto t1 = readTape (reel, 0), t2 = readTape (reel, 1);
    const auto ml = readFile (reel.getMasterFile (m, 0));
    int64 matched = 0, wrong = 0;
    for (size_t k = 0; k < ml.size() && k < t1.size() && k < t2.size(); ++k)
    {
        if (ml[k] == 0.0f) continue;
        if (std::abs (ml[k] - (t1[k] + t2[k])) < 1.0e-5f) ++matched;
        else if (wrong++ < 3)
            std::printf ("    %s @%lld: master %.6f, tracks %.6f (track 1 here is clock %lld)\n", what, (long long) k,
                         ml[k], t1[k] + t2[k], (long long) decode (0, t1[k]));
    }
    CHECK (wrong == 0 && matched > (int64) kSR, "%s: %lld aligned, %lld wrong", what, (long long) matched, (long long) wrong);
    if (wrong == 0) std::printf ("    %s: %lld samples sample-exact with the tracks\n", what, (long long) matched);
}

static void testMasters()
{
    std::printf ("[13] masters on the reel: internal mix, console returns, ping\n");
    juce::String err;
    auto reel = Reel::create (makeTempDir ("masters"), "Masters", kSR, kDefaultTracks, err);
    if (reel == nullptr) { CHECK (false, "%s", err.toRawUTF8()); return; }

    constexpr int D = 777;   // outboard + converters
    TapeEngine e;
    e.setReel (reel.get());
    prepareEngine (e, 256);
    for (int t = 0; t < kDefaultTracks; ++t) { e.setTrackToMix (t, t < 2); e.setTrackOutputs (t, {}); }
    e.setMasterOutputs (0, ChannelList { 0 });
    e.setMasterOutputs (1, ChannelList { 1 });

    LoopbackDevice dev (e, D);
    dev.startThread();

    // Ping through the loop
    for (int run = 0; run < 3; ++run)
    {
        e.startPing (2, 22);
        for (int i = 0; i < 200 && e.getPingState() == TapeEngine::PingState::running; ++i) juce::Thread::sleep (5);
        CHECK (e.getPingState() == TapeEngine::PingState::done && e.getPingResult() == D,
               "ping: state %d, measured %d, expected %d", (int) e.getPingState(), e.getPingResult(), D);
    }
    std::printf ("    ping measured %d samples (loop is %d)\n", e.getPingResult(), D);
    e.setMeasuredRoundTrip (D);

    // Two tracks of clock code
    e.setArmed (0, true); e.setArmed (1, true);
    CHECK (e.record().isEmpty(), "record tracks");
    pump (e, (int) (3000 / testSpeed()));
    e.stop();
    e.setArmed (0, false); e.setArmed (1, false);

    auto recordMaster = [&] (const char* name)
    {
        e.locate (0);
        const int m = reel->addMaster (name);
        e.mastersChanged();
        e.setActiveMaster (m);
        e.setMasterArmed (true);
        CHECK (e.isMasterArmed(), "master arm");
        auto r = e.record();
        CHECK (r.isEmpty(), "record master: %s", r.toRawUTF8());
        pump (e, (int) (2000 / testSpeed()));
        e.stop();
        e.setMasterArmed (false);
        return m;
    };

    // 1. Internal mix only
    checkMasterAligned (*reel, recordMaster ("Mix A"), "internal mix");


    // 3. Console returns: track 1 leaves the internal mix, goes out of 3,
    //    through a console, and comes back on master input 23.
    e.setTrackToMix (0, false);
    e.setTrackOutputs (0, ChannelList { 2 });
    e.setMasterInputs (0, ChannelList { 22 });
    e.setMasterInputs (1, {});
    CHECK (e.getMasterBusLatency() == D, "returns latency %d", e.getMasterBusLatency());
    checkMasterAligned (*reel, recordMaster ("Mix B"), "console returns");

    dev.stopThread (5000);

    // Every tape on the reel is the same length, and the masters reload.
    juce::String lerr;
    auto reloaded = Reel::load (reel->getFile(), lerr);
    CHECK (reloaded != nullptr && reloaded->masters.size() == 2, "masters not saved");
    if (reloaded != nullptr)
    {
        const auto len = readFile (reloaded->getMasterFile (0, 0)).size();
        bool same = len > 0;
        for (auto& f : reloaded->getAllTapeFiles()) same = same && readFile (f).size() == len;
        CHECK (same, "tape lengths differ");
        CHECK (reloaded->takeLog.back().tracks == "Mix B", "take log '%s'", reloaded->takeLog.back().tracks.toRawUTF8());
        std::printf ("    2 masters saved, all %d tape files the same length\n", reloaded->getAllTapeFiles().size());
    }
    e.setReel (nullptr);
}


//==============================================================================
static void writeSineTape (const juce::File& f, double freq, double seconds, float amp = 0.5f)
{
    f.getParentDirectory().createDirectory();
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream().release());
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), kSR, 1, 24, {}, 0));
    os.release();
    const int n = (int) (seconds * kSR);
    juce::AudioBuffer<float> b (1, n);
    for (int i = 0; i < n; ++i)
        b.setSample (0, i, amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi * freq * i / kSR));
    w->writeFromAudioSampleBuffer (b, 0, n);
}

/** Plays track 1 at `rate` and returns the worst error against the ideal
    (mathematically resampled) sine, in dB relative to the sine's level. */
static double varispeedPlaybackError (double freq, double deckIps, double pct, double& outLevelDb)
{
    juce::String err;
    auto reel = Reel::create (makeTempDir ("vsplay"), "VS", kSR, kDefaultTracks, err);
    writeSineTape (reel->getTapeFile (0), freq, 8.0);

    TapeEngine e;
    e.setReel (reel.get());
    prepareEngine (e, 256);
    for (int t = 0; t < kDefaultTracks; ++t) e.setTrackToMix (t, t == 0);
    e.setSpeed (deckIps, pct != 0.0, pct);
    const double rate = e.getTargetSpeed();

    const int n = 256;
    juce::AudioBuffer<float> in (kDefaultTracks, n), out (4, n);
    in.clear();
    const int64 startPos = 24000;
    e.locate (startPos);
    e.process (in.getArrayOfReadPointers(), kDefaultTracks, out.getArrayOfWritePointers(), 4, n);  // parked
    e.play();

    double errSq = 0.0, sigSq = 0.0, outSq = 0.0;
    int64 k = 0;
    const int64 total = (int64) (kSR * 1.5);
    const double maxPos = 7.5 * kSR;
    while (k < total)
    {
        e.process (in.getArrayOfReadPointers(), kDefaultTracks, out.getArrayOfWritePointers(), 4, n);
        for (int i = 0; i < n; ++i, ++k)
        {
            const double tpos = (double) startPos + (double) k * rate;
            if (tpos > maxPos) continue;
            const double ideal = (freq * rate < kSR * 0.45) ? 0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * freq * tpos / kSR) : 0.0;
            const double d = out.getSample (0, i) - ideal;
            errSq += d * d;
            sigSq += 0.125;                     // (0.5 amplitude sine)^2 mean
            outSq += (double) out.getSample (0, i) * out.getSample (0, i);
        }
        juce::Thread::sleep (1);
    }
    CHECK (e.getPlaybackUnderruns() == 0, "playback underruns %lld", (long long) e.getPlaybackUnderruns());
    e.stop();
    e.setReel (nullptr);
    outLevelDb = 10.0 * std::log10 (juce::jmax (1.0e-30, outSq / sigSq));
    return 10.0 * std::log10 (juce::jmax (1.0e-30, errSq / sigSq));
}

static void testVarispeed()
{
    std::printf ("[14] varispeed and tape speed: accuracy, anti-aliasing, overdubs at varispeed\n");

    struct Case { const char* name; double freq, deckIps, pct; };
    const Case cases[] = { { "+1 semitone (+5.946 %)", 1000.0, 15.0, (std::pow (2.0, 1.0 / 12.0) - 1.0) * 100.0 },
                           { "-50 %",                   1000.0, 15.0, -50.0 },
                           { "15 ips reel at 30 ips",   1000.0, 30.0, 0.0 },
                           { "30 ips +50 % (x3)",       1000.0, 30.0, 50.0 },
                           { "7.5 ips -13.7 % (x0.43)", 9000.0, 7.5, -13.7 },
                           { "-0.01 %",                 15000.0, 15.0, -0.01 } };
    for (auto& c : cases)
    {
        double level = 0.0;
        const double e = varispeedPlaybackError (c.freq, c.deckIps, c.pct, level);
        CHECK (e < -90.0, "%s: error %.1f dB", c.name, e);
        std::printf ("    %-26s %5.0f Hz on tape: error %6.1f dB\n", c.name, c.freq, e);
    }

    // Anti-aliasing: 20 kHz played faster lands above Nyquist and must vanish
    for (double pct : { 30.0, 50.0 })
    {
        double level = 0.0;
        varispeedPlaybackError (20000.0, 15.0, pct, level);
        CHECK (level < -90.0, "20 kHz at +%.0f %%: alias %.1f dB", pct, level);
        std::printf ("    20 kHz at +%.0f %% (above Nyquist): alias %6.1f dB\n", pct, level);
    }

    // Overdub at varispeed through a real round trip: track 1 out of output 3,
    // back on input 23 into track 2. On tape, track 2 must equal track 1.
    juce::String err;
    auto reel = Reel::create (makeTempDir ("vsrec"), "VSRec", kSR, kDefaultTracks, err);
    writeSineTape (reel->getTapeFile (0), 997.0, 10.0);
    constexpr int D = 531;
    TapeEngine e;
    e.setReel (reel.get());
    prepareEngine (e, 256);
    for (int t = 0; t < kDefaultTracks; ++t) { e.setTrackToMix (t, t == 0); e.setTrackOutputs (t, {}); }
    e.setTrackOutputs (0, ChannelList { 2 });
    e.setInput (1, 22);
    e.setArmed (1, true);
    e.setMeasuredRoundTrip (D);
    const int m = reel->addMaster ("Vari Mix");
    e.mastersChanged();
    e.setActiveMaster (m);
    e.setMasterArmed (true);
    e.setSpeed (15.0, true, -7.0);

    LoopbackDevice dev (e, D);
    dev.startThread();
    juce::Thread::sleep (50);
    e.locate (4800);
    CHECK (e.record().isEmpty(), "record at varispeed");
    CHECK (e.setSpeed (15.0, true, 3.0).isNotEmpty(), "speed change allowed while recording");
    pump (e, (int) (3000 / testSpeed()));
    e.stop();
    dev.stopThread (5000);

    const auto t1 = readTape (*reel, 0), t2 = readTape (*reel, 1);
    const auto ml = readFile (reel->getMasterFile (m, 0));
    auto compare = [&] (const Tape& got, const char* what)
    {
        int64 first = -1, last = -1;
        for (size_t k = 0; k < got.size(); ++k) if (got[k] != 0.0f) { if (first < 0) first = (int64) k; last = (int64) k; }
        double errSq = 0.0; int64 count = 0;
        for (int64 k = first + 300; k < last - 300 && k < (int64) t1.size(); ++k, ++count)
        {
            const double d = got[(size_t) k] - t1[(size_t) k];
            errSq += d * d;
        }
        const double db = count > 0 ? 10.0 * std::log10 (juce::jmax (1.0e-30, errSq / (count * 0.125))) : 0.0;
        CHECK (count > (int64) kSR && db < -80.0, "%s: %lld samples, error %.1f dB", what, (long long) count, db);
        std::printf ("    %s at -7 %%: %.2f s on tape, error vs track 1 %6.1f dB\n", what, (double) count / kSR, db);
    };
    compare (t2, "overdub via round trip");
    compare (ml, "master");
    e.setReel (nullptr);
}

//==============================================================================
// Tape stock: printed on record. Must stay sample-aligned (the oversampler's
// delay is compensated), near unity at low level, saturate when hit hard,
// and the repro side adds hiss. Clean reels are covered bit-exact above.
struct SignalDevice : juce::Thread
{
    SignalDevice (TapeEngine& e, std::function<float (int64)> f) : juce::Thread ("SigAudio"), engine (e), sig (std::move (f)) {}
    ~SignalDevice() override { stopThread (5000); }
    void run() override
    {
        const int n = 256;
        juce::AudioBuffer<float> in (kDefaultTracks, n), out (2, n);
        in.clear();
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        int64 rendered = 0;
        while (! threadShouldExit())
        {
            for (int k = 0; k < n; ++k) in.setSample (0, k, sig (clock + k));
            const auto pos = engine.getPosition();
            const bool rolling = engine.isPlaying();
            engine.process (in.getArrayOfReadPointers(), kDefaultTracks, out.getArrayOfWritePointers(), 2, n);
            if (rolling && engine.isRecording() && ! mapped.load()) { posAt = pos; clockAt = clock; mapped = true; }
            if (rolling && capture.load())
                for (int k = 0; k < n; ++k) captured.push_back (out.getSample (0, k));
            clock += n; rendered += n;
            const double due = t0 + (double) rendered / kSR * 1000.0 / testSpeed();
            const double now = juce::Time::getMillisecondCounterHiRes();
            if (due > now) juce::Thread::sleep ((int) (due - now));
        }
    }
    TapeEngine& engine;
    std::function<float (int64)> sig;
    int64 clock = 0;
    std::atomic<bool> mapped { false }, capture { false };
    std::atomic<int64> posAt { 0 }, clockAt { 0 };
    std::vector<float> captured;
};

static double toneLevel (const Tape& d, size_t from, size_t len, double f)
{
    double re = 0, im = 0;
    for (size_t k = 0; k < len; ++k)
    {
        const double w = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * (double) k / (double) len);
        const double ph = juce::MathConstants<double>::twoPi * f * (double) k / kSR;
        re += w * d[from + k] * std::cos (ph); im += w * d[from + k] * std::sin (ph);
    }
    return 20.0 * std::log10 (juce::jmax (1.0e-12, 4.0 * std::sqrt (re * re + im * im) / (double) len));
}

static void testTapeStock()
{
    std::printf ("[18] tape stock: printed on record, aligned, saturates, hiss\n");
    for (int stock = 1; stock <= 4; ++stock)
    {
        const auto& si = tape::info ((tape::Stock) stock);
        for (double amp : { 0.0316, 0.5 })      // -30 dBFS and -6 dBFS
        {
            juce::String err;
            auto reel = Reel::create (makeTempDir ("stock"), "Stock", kSR, kDefaultTracks, err, 15.0, stock);
            CHECK (reel != nullptr && reel->tapeStock == stock, "reel keeps its stock");
            auto sig = [amp] (int64 c) { const double t = (double) c / kSR; return (float) (amp * (0.6 * std::sin (6.2831853 * 997.0 * t) + 0.25 * std::sin (6.2831853 * 211.3 * t) + 0.15 * std::sin (6.2831853 * 3001.0 * t))); };
            auto pure = [amp] (int64 c) { return (float) (amp * std::sin (6.2831853 * 997.0 * (double) c / kSR)); };
            const bool hard = amp > 0.1;
            TapeEngine e;
            e.setReel (reel.get());
            prepareEngine (e, 256);
            e.setInput (0, 0);
            e.setArmed (0, true);
            SignalDevice dev (e, hard ? std::function<float (int64)> (pure) : std::function<float (int64)> (sig));
            dev.startThread();
            juce::Thread::sleep (50);
            e.locate (4800);
            CHECK (e.record().isEmpty(), "record on stock");
            pump (e, (int) (2500 / testSpeed()));
            e.stop();
            dev.stopThread (5000);

            const auto got = readTape (*reel, 0);
            const int64 offset = dev.clockAt.load() - dev.posAt.load() + kLat;   // tape Q <- input clock Q + offset
            int64 first = -1, last = -1;
            for (size_t k = 0; k < got.size(); ++k) if (got[k] != 0.0f) { if (first < 0) first = (int64) k; last = (int64) k; }
            const auto& f = hard ? std::function<float (int64)> (pure) : std::function<float (int64)> (sig);
            double best = 1e9; int bestLag = 99;
            for (int lag = -3; lag <= 3; ++lag)
            {
                double es = 0, ss = 0;
                for (int64 q = first + 2000; q < last - 2000; ++q)
                {
                    const double r = f (q + offset + lag);
                    es += (got[(size_t) q] - r) * (got[(size_t) q] - r); ss += r * r;
                }
                const double db = 10.0 * std::log10 (juce::jmax (1.0e-30, es / juce::jmax (1.0e-30, ss)));
                if (db < best) { best = db; bestLag = lag; }
            }
            if (! hard)
            {
                CHECK (bestLag == 0 && best < -30.0, "%s -30 dBFS: lag %d, residual %.1f dB", si.name, bestLag, best);
                std::printf ("    %-20s -30 dBFS: aligned (lag %d), residual vs input %6.1f dB\n", si.name, bestLag, best);
            }
            else
            {
                const size_t from = (size_t) first + 4000, len = 16384;
                const double h1 = toneLevel (got, from, len, 997.0), h2 = toneLevel (got, from, len, 1994.0), h3 = toneLevel (got, from, len, 2991.0);
                CHECK (bestLag == 0 && h3 - h1 > -50.0 && h3 - h1 < -10.0 && std::abs (h1 + 6.0) < 1.5,
                       "%s -6 dBFS: lag %d, fund %.1f, H2 %.1f, H3 %.1f", si.name, bestLag, h1, h2 - h1, h3 - h1);
                std::printf ("    %-20s  -6 dBFS: aligned (lag %d), H2 %6.1f dB  H3 %6.1f dB\n", si.name, bestLag, h2 - h1, h3 - h1);
            }
            e.setReel (nullptr);
        }
    }

    // Repro: hiss on a stock reel, silence on a clean one.
    for (int stock : { 0, 1 })
    {
        juce::String err;
        auto reel = Reel::create (makeTempDir ("hiss"), "Hiss", kSR, kDefaultTracks, err, 15.0, stock);
        writeSineTape (reel->getTapeFile (0), 1000.0, 5.0, 0.0f);
        TapeEngine e;
        e.setReel (reel.get());
        prepareEngine (e, 256);
        for (int t = 0; t < kDefaultTracks; ++t) e.setTrackToMix (t, t == 0);
        SignalDevice dev (e, [] (int64) { return 0.0f; });
        dev.startThread();
        e.locate (0);
        dev.capture = true;
        e.play();
        pump (e, (int) (2000 / testSpeed()));
        e.stop();
        dev.stopThread (5000);
        double ss = 0; for (auto v : dev.captured) ss += (double) v * v;
        const double db = 10.0 * std::log10 (juce::jmax (1.0e-30, ss / juce::jmax<size_t> (1, dev.captured.size())));
        if (stock == 0) CHECK (ss == 0.0, "clean reel: no hiss (%.1f dB)", db);
        else            CHECK (db > -95.0 && db < -50.0, "stock hiss %.1f dB", db);
        std::printf ("    playback of blank tape, %-12s %s\n", stock == 0 ? "clean:" : "US mastering:", stock == 0 ? "digital silence" : juce::String (db, 1).toRawUTF8());
        e.setReel (nullptr);
    }
}

//==============================================================================
// Hawthorne Link: a simulated network between a sender and the receiver,
// with its own sample clock, bursty sends, jitter and packet loss.
struct LinkSim
{
    struct Options { double ppm = 0.0; double jitterMs = 2.0; double loss = 0.0; bool redundant = true;
                     int senderBlock = 128; double senderRate = kSR; uint32_t session = 0x1234; double seconds = 40.0;
                     int receiverBlock = 256; double minBufferMs = 3.0; };

    explicit LinkSim (Options o) : opt (o) {}

    double signal (int ch, int64 frame) const
    {
        return 0.4 * std::sin (2.0 * juce::MathConstants<double>::pi * (997.0 + 31.0 * ch) * (double) frame / kSR);
    }

    // Runs; calls check (output block, receiver) after every receiver block.
    template <typename Check>
    void run (NetReceiver& rx, Check&& check)
    {
        const int ch = 2;
        Packetizer pk;
        pk.prepare (0xabcd, opt.session, opt.senderRate, ch, opt.redundant);
        rx.connectToSenderId (0, 0xabcd, "Sim");
        rx.setTargetLatencyMs (opt.minBufferMs);
        rx.prepare (kSR);

        std::mt19937 rng (99);
        std::uniform_real_distribution<double> uni (0.0, 1.0);
        struct InFlight { double arrive; std::vector<uint8_t> data; };
        std::deque<InFlight> flight;

        const double senderPerRx = (1.0 + opt.ppm * 1.0e-6);    // sender frames per receiver frame
        int64 sent = 0;
        std::vector<float> l ((size_t) opt.senderBlock), r ((size_t) opt.senderBlock);
        const int block = opt.receiverBlock;     // our audio callback size (processed in chunks of 256)
        for (int64 T = 0; T < (int64) (opt.seconds * kSR); T += block)
        {
            // sender produces in bursts of its own block size, at its own clock
            while ((double) (sent + opt.senderBlock) <= (double) (T + block) * senderPerRx)
            {
                for (int i = 0; i < opt.senderBlock; ++i) { l[(size_t) i] = (float) signal (0, sent + i); r[(size_t) i] = (float) signal (1, sent + i); }
                const float* ptrs[2] = { l.data(), r.data() };
                const double now = (double) (sent + opt.senderBlock) / senderPerRx;
                pk.push (ptrs, 2, opt.senderBlock, [&] (const uint8_t* d, int size)
                {
                    if (uni (rng) < opt.loss) return;
                    flight.push_back ({ now + uni (rng) * opt.jitterMs * kSR / 1000.0, std::vector<uint8_t> (d, d + size) });
                });
                sent += opt.senderBlock;
            }

            std::sort (flight.begin(), flight.end(), [] (auto& a, auto& b) { return a.arrive < b.arrive; });
            while (! flight.empty() && flight.front().arrive <= (double) T)
            {
                rx.handleDatagram (flight.front().data.data(), (int) flight.front().data.size(), "sim");
                flight.pop_front();
            }

            for (int done = 0; done < block; done += 256)
            {
                rx.process (256);
                check (T + done, 256);
            }
        }
    }

    Options opt;
};

static void testNetworkLink()
{
    std::printf ("[15] Hawthorne Link: word-clock lock, drift, jitter, packet loss\n");

    // 1. Same clock (word clock): must lock and pass audio bit-perfect.
    {
        NetReceiver rx;
        LinkSim::Options o; o.ppm = 0.0; o.jitterMs = 3.0; o.seconds = 130.0;
        LinkSim sim (o);
        int64 lockedAt = -1, compared = 0, wrong = 0, offset = INT64_MIN;
        sim.run (rx, [&] (int64 T, int n)
        {
            if (rx.getStatus (0).state != NetReceiver::State::locked) return;
            if (lockedAt < 0) lockedAt = T;
            const float* out = rx.getChannel (0, 1);
            if (out == nullptr) { ++wrong; return; }
            double p0, rt;
            rx.getReadPosition (0, p0, rt);
            CHECK (rt == 1.0 && p0 == std::floor (p0), "locked but reading at %.6f x%.9f", p0, rt);
            offset = (int64) p0 - T;
            for (int k = 0; k < n; ++k, ++compared)
            {
                uint8_t b[3];
                net::writeInt24 (b, (float) sim.signal (1, T + offset + k));
                if (out[k] != net::readInt24 (b) && wrong++ < 3)
                {
                    double p0, rt; rx.getReadPosition (0, p0, rt);
                    std::printf ("    wrong at T=%lld k=%d (locked since %lld): got %.7f want %.7f  pos %.6f rate %.9f frame %lld\n", (long long) T, k, (long long) lockedAt, out[k], net::readInt24 (b), p0, rt, (long long) (T + offset + k));
                }
            }
        });
        const auto st = rx.getStatus (0);
        CHECK (lockedAt > 0 && lockedAt < (int64) (110 * kSR), "never locked (%s)", NetReceiver::describe (st.state).toRawUTF8());
        CHECK (compared > (int64) (5 * kSR) && wrong == 0, "bit-perfect: %lld compared, %lld wrong", (long long) compared, (long long) wrong);
        CHECK (st.dropouts == 0, "dropouts %lld", (long long) st.dropouts);
        std::printf ("    shared clock: locked after %.1f s, %.1f s bit-perfect, buffer %.1f ms, 0 dropouts\n",
                     (double) lockedAt / kSR, (double) compared / kSR, st.bufferMs);
    }

    // 2. Independent clocks (+43 ppm and -87 ppm): drift corrected, clean, no dropouts.
    for (double ppm : { 43.0, -87.0 })
    {
        NetReceiver rx;
        LinkSim::Options o; o.ppm = ppm; o.jitterMs = 3.0; o.seconds = 120.0;
        LinkSim sim (o);
        // Every output sample against the signal at the exact sender position it was read from
        // (interpolation quality), plus how steady the rate is once settled.
        double err = 0, sig = 0, rmin = 2, rmax = 0;
        sim.run (rx, [&] (int64 T, int n)
        {
            if (T < (int64) (100 * kSR)) return;
            const float* out = rx.getChannel (0, 0);
            double p0, rate;
            rx.getReadPosition (0, p0, rate);
            rmin = juce::jmin (rmin, rate); rmax = juce::jmax (rmax, rate);
            for (int k = 0; k < n; ++k)
            {
                const double pos = p0 + rate * k;
                const double ideal = 0.4 * std::sin (2.0 * juce::MathConstants<double>::pi * 997.0 * pos / kSR);
                const double d = (out != nullptr ? out[k] : 0.0f) - ideal;
                err += d * d; sig += ideal * ideal;
            }
        });
        const double db = 10.0 * std::log10 (err / sig);
        const double wobble = (rmax - rmin) * 1.0e6;
        const auto st = rx.getStatus (0);
        CHECK (st.state == NetReceiver::State::corrected, "state %s", NetReceiver::describe (st.state).toRawUTF8());
        CHECK (std::abs (st.ppm - ppm) < 3.0, "measured %.2f ppm, actual %.0f", st.ppm, ppm);
        CHECK (db < -100.0 && st.dropouts == 0, "drift %.0f ppm: error %.1f dB, dropouts %lld", ppm, db, (long long) st.dropouts);
        CHECK (wobble < 15.0, "rate wobble %.2f ppm", wobble);   // 15 ppm = 0.026 cents
        std::printf ("    %+.0f ppm clocks: measured %+.2f ppm, rate steady to %.2f ppm, error %.1f dB, 0 dropouts\n", ppm, st.ppm, wobble, db);
    }

    // 3. Packet loss: redundancy covers it; without it, losses are silenced and counted.
    for (bool red : { true, false })
    {
        NetReceiver rx;
        LinkSim::Options o; o.loss = 0.002; o.redundant = red; o.seconds = 20.0;
        LinkSim sim (o);
        sim.run (rx, [] (int64, int) {});
        const auto st = rx.getStatus (0);
        if (red) CHECK (st.dropouts == 0, "0.2%% loss with redundancy: %lld dropouts", (long long) st.dropouts);
        else     CHECK (st.dropouts > 0, "0.2%% loss without redundancy should be reported");
        std::printf ("    0.2%% packet loss, redundancy %s: %lld dropouts\n", red ? "on " : "off", (long long) st.dropouts);
    }

    // 3b. Big audio buffers on both ends (1024 sent in bursts, 512 here) and 5 ms of
    //     jitter: the buffer sizes itself, no dropouts, and stays as small as is safe.
    {
        NetReceiver rx;
        LinkSim::Options o; o.senderBlock = 1024; o.receiverBlock = 512; o.jitterMs = 5.0; o.seconds = 30.0;
        LinkSim sim (o);
        sim.run (rx, [] (int64, int) {});
        const auto st = rx.getStatus (0);
        CHECK (st.dropouts == 0 && rx.isLive (0), "bursty: %lld dropouts", (long long) st.dropouts);
        CHECK (st.targetMs < 60.0, "bursty: buffer grew to %.1f ms", st.targetMs);
        std::printf ("    1024-sample sender, 512-sample receiver, 5 ms jitter: buffer %.1f ms, 0 dropouts\n", st.targetMs);
    }

    // 4. Wrong sample rate is refused, not played at the wrong speed.
    {
        NetReceiver rx;
        LinkSim::Options o; o.senderRate = 44100.0; o.seconds = 2.0;
        LinkSim sim (o);
        bool everLive = false;
        sim.run (rx, [&] (int64, int) { everLive = everLive || rx.isLive (0); });
        CHECK (! everLive && rx.getStatus (0).state == NetReceiver::State::rateMismatch, "rate mismatch not refused");
    }

    // 5. Real sockets on this machine: sender found by address, subscribed, streaming.
    {
        NetSender tx;
        NetReceiver rx;
        juce::String err;
        CHECK (tx.start ("Loopback Sender", err), "sender start: %s", err.toRawUTF8());
        CHECK (rx.start (err), "receiver start: %s", err.toRawUTF8());
        tx.prepare (kSR, 4);
        rx.prepare (kSR);
        rx.setManualAddresses ({ "127.0.0.1" });
        rx.connect (0, "Loopback Sender");

        std::atomic<bool> stopAudio { false };
        std::thread audio ([&]
        {
            std::vector<float> buf (4 * 256);
            const float* ptrs[4] = { buf.data(), buf.data() + 256, buf.data() + 512, buf.data() + 768 };
            int64 f = 0;
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            while (! stopAudio)
            {
                for (int c = 0; c < 4; ++c)
                    for (int k = 0; k < 256; ++k) buf[(size_t) (c * 256 + k)] = 0.3f * (float) std::sin (0.05 * (double) (f + k) + c);
                tx.push (ptrs, 4, 256);
                rx.process (256);
                f += 256;
                const double due = t0 + (double) f / kSR * 1000.0;
                const double now = juce::Time::getMillisecondCounterHiRes();
                if (due > now) juce::Thread::sleep ((int) (due - now));
            }
        });

        bool live = false;
        for (int i = 0; i < 60 && ! live; ++i) { juce::Thread::sleep (100); live = rx.isLive (0); }
        juce::Thread::sleep (500);
        const auto st = rx.getStatus (0);
        stopAudio = true;
        audio.join();
        CHECK (live && st.channels == 4, "loopback: live %d, %s, %d ch", (int) live, NetReceiver::describe (st.state).toRawUTF8(), st.channels);
        CHECK (tx.getSubscribers().size() == 1, "sender has %d subscribers", (int) tx.getSubscribers().size());
        std::printf ("    real sockets: found by address, subscribed, %d ch live, %lld packets\n", st.channels, (long long) st.packets);
        rx.stop();
        tx.stop();
    }
}

//==============================================================================
// Hawthorne Sync: a simulated drum machine on its own clock, linked over a simulated
// network, schedules a band-limited click on every beat using only the POS messages
// (delivered late, as over a real network). Recorded onto tape through the network
// input, every click must land on its beat to within a fraction of a sample - with
// latency compensation, clock drift and varispeed.
static void testTapeSync()
{
    std::printf ("[16] Hawthorne Sync: drum machine clicks land on the beat on tape\n");

    struct Case { const char* name; double ppm; bool vari; double variPct; };
    for (auto c : { Case { "same clock, 15 ips", 0.0, false, 0.0 },
                    Case { "+41 ppm drift, varispeed +6 %", 41.0, true, 6.0 },
                    Case { "-63 ppm drift, varispeed -9 %", -63.0, true, -9.0 } })
    {
        juce::String err;
        auto reel = Reel::create (makeTempDir ("sync"), "Sync", kSR, kDefaultTracks, err);
        CHECK (reel != nullptr, "create: %s", err.toRawUTF8());
        if (reel == nullptr) return;

        NetReceiver rx;
        TapeEngine e;
        e.setNetworkReceiver (&rx);
        e.setReel (reel.get());
        const int block = 256;
        prepareEngine (e, block);
        rx.connectToSenderId (0, 0xd2u, "Drums");
        rx.setReelSync (true, "Sync", 120.0, 4, 1.25 * kSR);
        e.setInput (0, NetReceiver::inputId (0, 0));
        e.setArmed (0, true);
        e.setSpeed (15.0, c.vari, c.variPct);

        const double bpm = 120.0, barOne = 1.25 * kSR, spb = kSR * 60.0 / bpm;  // tape samples per beat
        Packetizer pk;
        const uint32_t session = 0x51u;
        pk.prepare (0xd2u, session, kSR, 1, true);

        // POS messages in flight (built from the receiver's own getTapeSync), 4-9 ms late
        std::mt19937 rng (7);
        std::uniform_real_distribution<double> uni (0.0, 1.0);
        struct Msg { double arrive; net::TapeSync ts; };
        std::deque<Msg> posFlight;
        std::deque<std::pair<double, std::vector<uint8_t>>> audioFlight;
        bool haveClock = false;
        net::TapeSync clock;

        const double senderPerRx = 1.0 + c.ppm * 1.0e-6;
        const int sBlock = 128;
        int64 sent = 0;
        std::vector<float> buf ((size_t) sBlock);
        juce::AudioBuffer<float> in (kDefaultTracks, block), out (4, block);
        in.clear();

        // band-limited click centred on a fractional frame
        auto pulse = [] (double x) -> double
        {
            if (std::abs (x) >= 24.0) return 0.0;
            const double w = 0.5 + 0.5 * std::cos (juce::MathConstants<double>::pi * x / 24.0);
            const double a = juce::MathConstants<double>::pi * 0.4 * x;
            return w * 0.4 * (x == 0.0 ? 1.0 : std::sin (a) / a);
        };

        const double recStartAt = 3.0, recEndAt = 9.0;   // seconds of our clock
        bool recording = false, stopped = false;
        double lastSent = -1.0;
        for (int64 T = 0; T < (int64) (10.0 * kSR); T += block)
        {
            const double now = (double) T;

            // --- drum machine: render its blocks (its own clock), using the newest POS it has
            while (! posFlight.empty() && posFlight.front().arrive <= now)
            {
                if (posFlight.front().ts.session == session) { clock = posFlight.front().ts; haveClock = true; }
                posFlight.pop_front();
            }
            while ((double) (sent + sBlock) <= (now + block) * senderPerRx)
            {
                std::fill (buf.begin(), buf.end(), 0.0f);
                if (haveClock && clock.isRolling() && clock.tapePerFrame > 0.0)
                {
                    const double t0 = clock.tapePos + ((double) sent - clock.senderFrame) * clock.tapePerFrame;
                    const double t1 = t0 + sBlock * clock.tapePerFrame;
                    const int64 b0 = (int64) std::floor ((t0 - 30.0 - barOne) / spb), b1 = (int64) std::ceil ((t1 + 30.0 - barOne) / spb);
                    for (int64 b = juce::jmax ((int64) 0, b0); b <= b1; ++b)
                    {
                        const double fB = clock.senderFrame + (barOne + (double) b * spb - clock.tapePos) / clock.tapePerFrame;
                        for (int i = 0; i < sBlock; ++i)
                            buf[(size_t) i] += (float) pulse ((double) (sent + i) - fB);
                    }
                }
                const float* ptr[1] = { buf.data() };
                const double at = (double) (sent + sBlock) / senderPerRx;
                pk.push (ptr, 1, sBlock, [&] (const uint8_t* d, int size)
                {
                    audioFlight.push_back ({ at + 1.0e-3 * kSR * (1.0 + 2.0 * uni (rng)), std::vector<uint8_t> (d, d + size) });
                });
                sent += sBlock;
            }

            std::sort (audioFlight.begin(), audioFlight.end(), [] (auto& a, auto& b) { return a.first < b.first; });
            while (! audioFlight.empty() && audioFlight.front().first <= now)
            {
                rx.handleDatagram (audioFlight.front().second.data(), (int) audioFlight.front().second.size(), "sim");
                audioFlight.pop_front();
            }

            // --- Hawthorne
            if (! recording && T >= (int64) (recStartAt * kSR))
            {
                e.locate ((int64) (0.5 * kSR));
                e.process (in.getArrayOfReadPointers(), kDefaultTracks, out.getArrayOfWritePointers(), 4, block);
                CHECK (e.record().isEmpty(), "record");
                recording = true;
            }
            if (recording && ! stopped && T >= (int64) (recEndAt * kSR)) { e.stop(); stopped = true; }

            e.process (in.getArrayOfReadPointers(), kDefaultTracks, out.getArrayOfWritePointers(), 4, block);
            if ((T / block) % 16 == 0) e.tick();

            // Hawthorne sends POS ~ every 20 ms (and the drum machine gets it 4-9 ms later)
            net::TapeSync ts;
            if (rx.getTapeSync (0, ts) && (lastSent < 0 || now - lastSent >= 0.02 * kSR || ts.isRolling() != clock.isRolling()))
            {
                lastSent = now;
                posFlight.push_back ({ now + (4.0 + 5.0 * uni (rng)) * 1.0e-3 * kSR, ts });
            }
        }
        for (int i = 0; i < 20; ++i) { e.tick(); juce::Thread::sleep (20); }

        auto tape = readTape (*reel, 0);
        int found = 0, missing = 0;
        double worst = 0.0;
        for (int64 b = 0;; ++b)
        {
            const double B = barOne + (double) b * spb;
            if (B + 64 >= (double) tape.size()) break;
            if (B < 0.5 * kSR + 0.25 * kSR) continue;       // first moments of the take: the drum machine is still catching up
            int64 best = -1; float bv = 0;
            for (int64 k = (int64) B - 40; k <= (int64) B + 40; ++k)
                if (k > 0 && k + 1 < (int64) tape.size() && std::abs (tape[(size_t) k]) > bv) { bv = std::abs (tape[(size_t) k]); best = k; }
            if (best < 0 || bv < 0.2f) { ++missing; continue; }
            const double y0 = tape[(size_t) best - 1], y1 = tape[(size_t) best], y2 = tape[(size_t) best + 1];
            const double peak = (double) best + 0.5 * (y0 - y2) / (y0 - 2.0 * y1 + y2);
            worst = juce::jmax (worst, std::abs (peak - B));
            ++found;
        }
        CHECK (found >= 8 && missing == 0, "%s: %d clicks found, %d missing", c.name, found, missing);
        CHECK (worst < 0.25, "%s: worst click %.3f samples off the beat", c.name, worst);
        std::printf ("    %-32s %d beats on tape, worst %.3f samples off\n", c.name, found, worst);
        e.setReel (nullptr);
        e.setNetworkReceiver (nullptr);
    }

    // Tempo kept with the reel, and the messages survive a round trip through text.
    {
        juce::String err;
        auto dir = makeTempDir ("synctempo");
        auto reel = Reel::create (dir, "Tempo", kSR, kDefaultTracks, err);
        reel->tempoBpm = 97.5; reel->beatsPerBar = 3; reel->barOne = 12345.0;
        CHECK (reel->save (err), "save");
        auto again = Reel::load (reel->getFile(), err);
        CHECK (again != nullptr && again->tempoBpm == 97.5 && again->beatsPerBar == 3 && again->barOne == 12345.0, "tempo not kept with the reel");
        int bar = 0, beat = 0;
        CHECK (again != nullptr && again->barBeatAt (12345 + (int64) (kSR * 60.0 / 97.5 * 4.5), bar, beat) && bar == 2 && beat == 2, "bar/beat %d.%d", bar, beat);
        CHECK (again != nullptr && again->barBeatAt (12345 - 10, bar, beat) && bar == -1 && beat == 3, "count-in bar/beat %d.%d", bar, beat);

        net::TapeSync a; a.senderId = 0xabc; a.session = 0x77; a.senderFrame = 123456.5; a.tapePos = 987654.25;
        a.tapePerFrame = 1.0599999; a.flags = 7; a.bpm = 133.3; a.beatsPerBar = 7; a.barOne = 4800.5; a.tempoRev = 9; a.reelName = "Song 2 take";
        net::TapeSync b;
        CHECK (net::parsePos (net::makePos (a), b) && b.senderFrame == a.senderFrame && b.tapePos == a.tapePos
               && std::abs (b.tapePerFrame - a.tapePerFrame) < 1e-12 && b.flags == 7 && b.beatsPerBar == 7 && b.reelName == a.reelName, "POS round trip");

        NetSender tx;
        a.senderId = tx.getSenderId();
        tx.handleControlForTest (net::makePos (a));
        NetSender::TapeClock tc;
        CHECK (tx.getTapeClock (tc) && tc.tapePos == a.tapePos && tc.bpm == a.bpm && tx.getReelName() == a.reelName, "sender didn't take POS");

        NetReceiver rx;
        rx.connectToSenderId (2, 0x5eu, "Drums");
        net::TempoRequest tr; tr.senderId = 0x5eu; tr.bpm = 88.0; tr.beatsPerBar = 4; tr.barOne = 100.0;
        const auto m = net::makeTempo (tr);
        rx.handleDatagram (m.toRawUTF8(), (int) m.getNumBytesAsUTF8(), "x");
        int slot = -1; net::TempoRequest got;
        CHECK (rx.popTempoRequest (slot, got) && slot == 2 && got.bpm == 88.0 && got.barOne == 100.0, "tempo request not received");
        std::printf ("    tempo kept with the reel; POS / TMP messages round-trip\n");
    }
}

//==============================================================================
static void testTempoClickTimecode()
{
    std::printf ("[17] tempo, click, timecode (LTC / MTC) and chase\n");
    using namespace timecode;

    // Drop-frame numbering
    CHECK (fromFrames (1800, Rate::fps2997drop).toString (true) == "00:01:00;02", "DF 1800 -> %s", fromFrames (1800, Rate::fps2997drop).toString (true).toRawUTF8());
    CHECK (fromFrames (17982, Rate::fps2997drop).toString (true) == "00:10:00;00", "DF 17982");
    int bad = 0;
    for (juce::int64 f = 0; f < 2000000; f += 37)    // under 24 h at every rate
        for (auto r : { Rate::fps24, Rate::fps25, Rate::fps2997drop, Rate::fps2997, Rate::fps30 })
            if (toFrames (fromFrames (f, r), r) != f) ++bad;
    CHECK (bad == 0, "%d timecode round trips wrong", bad);

    // LTC: encode -> decode, at normal speed and 10 % fast, 25 and 29.97 drop
    for (auto r : { Rate::fps25, Rate::fps2997drop })
        for (double speed : { 1.0, 1.1 })
        {
            LtcEncoder enc; enc.prepare (kSR);
            LtcDecoder dec; dec.prepare (kSR);
            const double startFrames = (double) toFrames ({ 1, 0, 59, 20 }, r);
            std::vector<float> buf (kSR * 3);
            for (size_t i = 0; i < buf.size(); ++i)
                buf[i] = enc.sample (startFrames + (double) i * speed * fps (r) / kSR, r, 0.3f);
            int frames = 0, wrongSeq = 0;
            juce::int64 prev = -1;
            double worstTiming = 0;
            dec.process (buf.data(), (int) buf.size(), [&] (const LtcDecoder::Frame& f)
            {
                const auto n = toFrames (f.time, f.drop ? Rate::fps2997drop : r);
                if (prev >= 0 && n != prev + 1) ++wrongSeq;
                prev = n;
                ++frames;
                // the frame ended at sample where frame n+1 starts
                const double expectEnd = ((double) (n + 1) - startFrames) * kSR / (speed * fps (r));
                worstTiming = juce::jmax (worstTiming, std::abs ((double) f.endSample - expectEnd));
            });
            const int expected = (int) (3.0 * speed * fps (r)) - 2;
            CHECK (frames >= expected && wrongSeq == 0 && worstTiming < 3.0,
                   "LTC %s x%.1f: %d frames (want %d), %d out of order, timing off %.1f samples",
                   name (r).toRawUTF8(), speed, frames, expected, wrongSeq, worstTiming);
            std::printf ("    LTC %-10s x%.1f: %d frames decoded in order, timing within %.1f samples\n", name (r).toRawUTF8(), speed, frames, worstTiming);
        }

    // MTC: a full-frame message and a quarter-frame run come back as the right time
    {
        juce::String err;
        auto reel = Reel::create (makeTempDir ("mtc"), "MTC", kSR, 8, err);
        TapeEngine e;
        e.setReel (reel.get());
        e.setTimecodeFormat (Rate::fps25, 0.0);
        e.setChase (TapeEngine::ChaseSource::mtc);
        MtcInput in (e);
        const Time t { 1, 2, 3, 4 };
        in.handle (MtcOutput::fullFrame (t, Rate::fps25), 0.0);
        const double want = (double) toFrames (t, Rate::fps25) / 25.0;
        CHECK (std::abs (e.getChaseStatus().tcSeconds - want) < 1.0e-6, "MTC full frame %.4f want %.4f", e.getChaseStatus().tcSeconds, want);
        for (int piece = 0; piece < 8; ++piece)
            in.handle (MtcOutput::quarterFrame (piece, { 1, 2, 3, 6 }, Rate::fps25), 0.0);
        const double wantQ = ((double) toFrames ({ 1, 2, 3, 6 }, Rate::fps25) + 1.75) / 25.0;
        CHECK (std::abs (e.getChaseStatus().tcSeconds - wantQ) < 1.0e-6, "MTC quarter frames %.4f want %.4f", e.getChaseStatus().tcSeconds, wantQ);
        std::printf ("    MTC full-frame and quarter-frame messages decode exactly\n");
        e.setReel (nullptr);
    }

    // Click: sample-accurate on the beat at 120 BPM, accent on the bar
    {
        juce::String err;
        auto reel = Reel::create (makeTempDir ("click"), "Click", kSR, 8, err);
        TapeEngine e;
        e.setReel (reel.get());
        prepareEngine (e, 256, 8);
        e.setTempo (120.0, 4, 0.0);
        e.setClickOutputs (ChannelList { 3 });
        e.setClick (true, -6.0);
        juce::AudioBuffer<float> in (8, 256), out (4, 256);
        in.clear();
        e.locate (0);
        e.play();
        std::vector<float> click;
        for (int b = 0; b < 400; ++b)
        {
            e.process (in.getArrayOfReadPointers(), 8, out.getArrayOfWritePointers(), 4, 256);
            click.insert (click.end(), out.getReadPointer (3), out.getReadPointer (3) + 256);
        }
        e.stop();
        // an onset: the first non-zero sample after at least 100 silent ones
        std::vector<int64> onsets;
        int64 zeros = 1000;
        for (size_t i = 0; i < click.size(); ++i)
        {
            if (click[i] == 0.0f) { ++zeros; continue; }
            if (zeros >= 100) onsets.push_back ((int64) i - 1);   // sin(0) = 0: the click starts one sample earlier
            zeros = 0;
        }
        bool onBeat = onsets.size() >= 4;
        for (size_t k = 0; k < onsets.size(); ++k) onBeat = onBeat && onsets[k] == (int64) k * 24000;
        CHECK (onBeat, "click onsets not on the beats (%d onsets, first %lld, second %lld)", (int) onsets.size(),
               onsets.empty() ? -1LL : (long long) onsets[0], onsets.size() < 2 ? -1LL : (long long) onsets[1]);
        std::printf ("    click: %d beats, every one on the exact sample\n", (int) onsets.size());
        e.setReel (nullptr);
    }

    // Chase: one machine sends LTC, the other chases it and locks
    {
        juce::String err;
        auto reelA = Reel::create (makeTempDir ("chaseA"), "A", kSR, 8, err);
        auto reelB = Reel::create (makeTempDir ("chaseB"), "B", kSR, 8, err);
        TapeEngine a, b;
        a.setReel (reelA.get()); b.setReel (reelB.get());
        prepareEngine (a, 256, 8); prepareEngine (b, 256, 8);
        a.setTimecodeFormat (Rate::fps25, 3600.0);       // 01:00:00:00 at tape zero
        b.setTimecodeFormat (Rate::fps25, 3600.0);
        a.setLtcOutputs (ChannelList { 2 }, -10.0);
        b.setChase (TapeEngine::ChaseSource::ltc, 0);
        a.locate ((int64) (12.5 * kSR));
        a.play();

        juce::AudioBuffer<float> inA (8, 256), outA (4, 256), inB (8, 256), outB (4, 256);
        inA.clear();
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        int64 rendered = 0;
        bool locked = false;
        double lockedAt = 0, worst = 0;
        while (rendered < (int64) (6.0 * kSR))
        {
            a.process (inA.getArrayOfReadPointers(), 8, outA.getArrayOfWritePointers(), 4, 256);
            inB.clear();
            inB.copyFrom (0, 0, outA, 2, 0, 256);                     // LTC cable
            b.process (inB.getArrayOfReadPointers(), 8, outB.getArrayOfWritePointers(), 4, 256);
            rendered += 256;
            if (rendered % 1536 == 0) { a.tick(); b.tick(); }
            const auto st = b.getChaseStatus();
            if (st.locked && ! locked) { locked = true; lockedAt = (double) rendered / kSR; }
            if (locked && rendered > (int64) ((lockedAt + 1.0) * kSR))
                worst = juce::jmax (worst, std::abs ((double) (b.getPosition() - a.getPosition())));
            const double due = t0 + (double) rendered / kSR * 1000.0;
            const double now = juce::Time::getMillisecondCounterHiRes();
            if (due > now) juce::Thread::sleep ((int) (due - now));
        }
        CHECK (locked && b.isPlaying(), "chase never locked");
        CHECK (worst < 0.001 * kSR, "chase off by %.0f samples once locked", worst);
        std::printf ("    chase: locked to LTC after %.2f s, then within %.2f ms\n", lockedAt, worst / kSR * 1000.0);

        a.stop();
        for (int i = 0; i < 200; ++i)
        {
            a.process (inA.getArrayOfReadPointers(), 8, outA.getArrayOfWritePointers(), 4, 256);
            inB.copyFrom (0, 0, outA, 2, 0, 256);
            b.process (inB.getArrayOfReadPointers(), 8, outB.getArrayOfWritePointers(), 4, 256);
            if (i % 6 == 0) b.tick();
            juce::Thread::sleep (5);
        }
        CHECK (! b.isPlaying(), "chasing machine didn't stop with the timecode");
        a.setReel (nullptr); b.setReel (nullptr);
    }
}

static void benchVarispeed()
{
    // HAWTHORNE_BENCH=1: CPU cost of varispeed playback with every track full.
    juce::String err;
    auto reel = Reel::create (makeTempDir ("bench"), "Bench", kSR, kMaxTracks, err);
    for (int t = 0; t < kMaxTracks; ++t) writeSineTape (reel->getTapeFile (t), 440.0 + t, 6.0, 0.01f);
    TapeEngine e;
    e.setReel (reel.get());
    prepareEngine (e, 256);
    const int n = 256;
    juce::AudioBuffer<float> in (kDefaultTracks, n), out (4, n);
    in.clear();
    for (double ratio : { 1.0, 1.06, 1.5, 2.0, 4.0 })
    {
        e.setSpeed (ratio >= 2.0 ? 30.0 : 15.0, ratio != 1.0 && ratio != 2.0, ratio >= 2.0 ? (ratio / 2.0 - 1.0) * 100.0 : (ratio - 1.0) * 100.0);
        e.locate (0);
        e.process (in.getArrayOfReadPointers(), kDefaultTracks, out.getArrayOfWritePointers(), 4, n);
        e.play();
        double busy = 0.0; int blocks = 0;
        for (int i = 0; i < 200; ++i)
        {
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            e.process (in.getArrayOfReadPointers(), kDefaultTracks, out.getArrayOfWritePointers(), 4, n);
            busy += juce::Time::getMillisecondCounterHiRes() - t0; ++blocks;
            juce::Thread::sleep (3);
        }
        e.stop();
        std::printf ("    96 tracks at x%.2f: %.0f %% of one core\n", ratio, 100.0 * busy / (blocks * n / kSR * 1000.0));
    }
    e.setReel (nullptr);
}

//==============================================================================
//==============================================================================
static void testWind()
{
    std::printf ("[19] tape wind physics: spool, overshoot, creep back, land exactly\n");
    juce::String err;
    auto reel = Reel::create (makeTempDir ("wind"), "Wind", kSR, 8, err);
    TapeEngine e;
    e.setReel (reel.get());
    prepareEngine (e, 256, 8);
    e.setWindPhysics (true);
    for (juce::int64 target : { (juce::int64) (60 * kSR), (juce::int64) (61.5 * kSR), (juce::int64) 0 })
    {
        const auto from = e.getPosition();
        e.windTo (target);
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        juce::int64 beyond = 0; double maxV = 0; juce::int64 last = from; double tl = t0;
        while (e.getWindDirection() != 0 && juce::Time::getMillisecondCounterHiRes() - t0 < 20000)
        {
            juce::Thread::sleep (10);
            e.tick();
            const auto p = e.getPosition();
            const double tn = juce::Time::getMillisecondCounterHiRes(); maxV = juce::jmax (maxV, std::abs ((double) (p - last)) / kSR / juce::jmax (0.005, (tn - tl) / 1000.0)); tl = tn;
            last = p;
            beyond = juce::jmax (beyond, target > from ? p - target : target - p);
        }
        const double secs = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
        CHECK (e.getPosition() == target && e.getWindDirection() == 0, "landed on %lld (at %lld)", (long long) target, (long long) e.getPosition());
        CHECK (secs > 0.3 && secs < 10.0, "wind took %.2f s", secs);
        std::printf ("    %6.1f s -> %6.1f s: %.2f s, top speed ~%.0fx, overshoot %.2f s, landed exactly\n",
                     (double) from / kSR, (double) target / kSR, secs, maxV, (double) beyond / kSR);
    }
    e.setWindPhysics (false);
    e.windTo ((juce::int64) (5 * kSR));
    CHECK (e.getPosition() == (juce::int64) (5 * kSR) && e.getWindDirection() == 0, "physics off: instant");
    e.setReel (nullptr);
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    if (juce::SystemStats::getEnvironmentVariable ("HAWTHORNE_BENCH", {}).isNotEmpty()) { benchVarispeed(); return 0; }

    testTapeMachine();
    testDiskOverflowKeepsAlignment();
    testDeviceLossDuringRecording();
    testRouting();
    testEqualLengthsAndPaperwork();
    test96Tracks();
    testLibrary();
    testMasters();
    testVarispeed();
    testNetworkLink();
    testTempoClickTimecode();
    testTapeSync();
    testTapeStock();
    testWind();
   #if JUCE_LINUX || JUCE_MAC
    testCrashRecovery();
   #endif

    std::printf (failures == 0 ? "\nALL TESTS PASSED\n" : "\n%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
