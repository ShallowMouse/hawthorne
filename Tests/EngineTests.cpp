// Headless torture tests for the Hawthorne tape engine.
// A fake device thread drives TapeEngine::process() in (accelerated) real time
// with an input signal that encodes the device sample clock, so every sample
// on tape can be checked for exact value and position.

#include "../Source/Engine/TapeEngine.h"
#include "../Source/Engine/Library.h"
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
    std::printf ("[13] masters on the reel: internal mix, outboard insert, console returns, ping\n");
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

    // 2. Insert: mix out of 3/4 through the outboard, back on 23/24
    e.setMasterInsert (true, { 2, 3 }, { 22, 23 });
    CHECK (e.getMasterBusLatency() == D, "insert latency %d", e.getMasterBusLatency());
    checkMasterAligned (*reel, recordMaster ("Mix B"), "insert");
    e.setMasterInsert (false, { -1, -1 }, { -1, -1 });

    // 3. Console returns: track 1 leaves the internal mix, goes out of 3,
    //    through a console, and comes back on master input 23.
    e.setTrackToMix (0, false);
    e.setTrackOutputs (0, ChannelList { 2 });
    e.setMasterInputs (0, ChannelList { 22 });
    e.setMasterInputs (1, {});
    CHECK (e.getMasterBusLatency() == D, "returns latency %d", e.getMasterBusLatency());
    checkMasterAligned (*reel, recordMaster ("Mix C"), "console returns");

    dev.stopThread (5000);

    // Every tape on the reel is the same length, and the masters reload.
    juce::String lerr;
    auto reloaded = Reel::load (reel->getFile(), lerr);
    CHECK (reloaded != nullptr && reloaded->masters.size() == 3, "masters not saved");
    if (reloaded != nullptr)
    {
        const auto len = readFile (reloaded->getMasterFile (0, 0)).size();
        bool same = len > 0;
        for (auto& f : reloaded->getAllTapeFiles()) same = same && readFile (f).size() == len;
        CHECK (same, "tape lengths differ");
        CHECK (reloaded->takeLog.back().tracks == "Mix C", "take log '%s'", reloaded->takeLog.back().tracks.toRawUTF8());
        std::printf ("    3 masters saved, all %d tape files the same length\n", reloaded->getAllTapeFiles().size());
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
   #if JUCE_LINUX || JUCE_MAC
    testCrashRecovery();
   #endif

    std::printf (failures == 0 ? "\nALL TESTS PASSED\n" : "\n%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
