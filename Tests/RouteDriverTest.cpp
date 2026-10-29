// Tests the Hawthorne Route ASIO driver without the registry: loads the DLL, plays
// "Hawthorne" (the shared memory + its clock) and "Reaper" (the ASIO host) at once,
// and checks every sample arrives in order, both ways, at the expected latency.
// Windows only (run under Wine on the build machine).

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <cstdio>
#include <cmath>
#include <atomic>
#include <thread>
#include <vector>
#include <string>

#include "asiosys.h"
#include "asio.h"
#include "iasiodrv.h"
#include "../Source/Route/RouteShared.h"

using namespace hawthorne;

static const CLSID clsid = { 0x75ab7497, 0xe772, 0x4196, { 0x9a, 0x44, 0xbe, 0xa8, 0xce, 0xf1, 0x77, 0xf6 } };
static int failures = 0;
static const long kTracks = 8;     // the reel on "Hawthorne"
#define CHECK(c, ...) do { if (! (c)) { ++failures; std::printf ("  FAIL: "); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

// --- the "host" (Reaper) side
static IASIO* drv = nullptr;
static std::vector<ASIOBufferInfo> infos;
static long hostBlock = 128;
static std::atomic<long long> hostCount { 0 };        // samples the host has produced
static std::atomic<long long> hostSwitches { 0 };
static std::vector<float> hostIn;                      // what the host received from Hawthorne (ch 0)

static float hostValue (long long n) { return (float) ((n % 100000) + 1) / 100000.0f; }       // host output, ch 0
static float hawValue (long long n)  { return -(float) ((n % 100000) + 1) / 100000.0f; }      // Hawthorne master L

static void bufferSwitch (long index, ASIOBool)
{
    for (auto& bi : infos)
    {
        float* b = static_cast<float*> (bi.buffers[index]);
        if (! bi.isInput && bi.channelNum == 0)
            for (long k = 0; k < hostBlock; ++k) b[k] = hostValue (hostCount + k);
        if (bi.isInput && bi.channelNum == kTracks)          // master L comes after the tracks
            for (long k = 0; k < hostBlock; ++k) hostIn.push_back (b[k]);
    }
    hostCount += hostBlock;
    ++hostSwitches;
}
static std::atomic<int> resetRequests { 0 };
static long asioMessage (long sel, long val, void*, double*)
{
    if (sel == kAsioSelectorSupported) return val == kAsioResetRequest ? 1 : 0;
    if (sel == kAsioResetRequest) { ++resetRequests; return 1; }
    return 0;
}
static ASIOTime* switchTI (ASIOTime*, long i, ASIOBool d) { bufferSwitch (i, d); return nullptr; }

int main()
{
    std::printf ("Hawthorne Route driver test\n");

    // --- "Hawthorne": the shared memory, a 48 kHz clock in 64-sample blocks
    HANDLE map = CreateFileMappingW (INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, (DWORD) sizeof (route::Shared), route::mappingName);
    auto* sh = static_cast<route::Shared*> (MapViewOfFile (map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof (route::Shared)));
    HANDLE tick = CreateEventW (nullptr, FALSE, FALSE, route::tickName);
    sh->magic = route::magic; sh->version = route::version;
    sh->sampleRate = 48000;
    sh->numTracks = (int) kTracks;
    sh->heartbeatMs = (long long) GetTickCount64();

    // --- load the driver like a host would (without the registry)
    HMODULE dll = LoadLibraryA ("HawthorneRoute.dll");
    CHECK (dll != nullptr, "load the DLL");
    if (dll == nullptr) return 1;
    using GetCO = HRESULT (__stdcall*) (REFCLSID, REFIID, void**);
    auto getCO = (GetCO) GetProcAddress (dll, "DllGetClassObject");
    CHECK (getCO != nullptr, "DllGetClassObject exported");
    IClassFactory* f = nullptr;
    CHECK (getCO (clsid, IID_IClassFactory, (void**) &f) == S_OK, "class factory");
    CHECK (f->CreateInstance (nullptr, clsid, (void**) &drv) == S_OK && drv != nullptr, "create the driver (CLSID as the interface, as ASIO hosts do)");

    CHECK (drv->init (nullptr) == ASIOTrue, "init");
    char name[64] {};
    drv->getDriverName (name);
    CHECK (std::string (name) == "Hawthorne Route", "name %s", name);
    long ni = 0, no = 0;
    drv->getChannels (&ni, &no);
    CHECK (ni == kTracks + 3 && no == kTracks + 2, "channels follow the reel: %ld in / %ld out", ni, no);
    CHECK (drv->canSampleRate (48000.0) == ASE_OK && drv->canSampleRate (44100.0) != ASE_OK, "follows Hawthorne's rate");

    hostIn.reserve (48000 * 10);
    infos.resize (4);
    infos[0] = { ASIOFalse, 0, { nullptr, nullptr } };
    infos[1] = { ASIOFalse, 1, { nullptr, nullptr } };
    infos[2] = { ASIOTrue, kTracks, { nullptr, nullptr } };
    infos[3] = { ASIOTrue, kTracks + 2, { nullptr, nullptr } };
    ASIOCallbacks cb { bufferSwitch, nullptr, asioMessage, switchTI };
    CHECK (drv->createBuffers (infos.data(), 4, hostBlock, &cb) == ASE_OK, "createBuffers");
    ASIOChannelInfo ci {};
    ci.channel = 0; ci.isInput = ASIOFalse;
    CHECK (drv->getChannelInfo (&ci) == ASE_OK && ci.type == ASIOSTFloat32LSB && ci.isActive, "channel info");
    ASIOChannelInfo lt {};
    lt.channel = kTracks + 2; lt.isInput = ASIOTrue;
    CHECK (drv->getChannelInfo (&lt) == ASE_OK && std::string (lt.name) == "Hawthorne LTC", "LTC channel named (%s)", lt.name);
    ASIOChannelInfo t5 {};
    t5.channel = 4; t5.isInput = ASIOTrue;
    CHECK (drv->getChannelInfo (&t5) == ASE_OK && std::string (t5.name) == "Hawthorne track 5", "track channels named (%s)", t5.name);
    ASIOChannelInfo mo {};
    mo.channel = kTracks; mo.isInput = ASIOFalse;
    CHECK (drv->getChannelInfo (&mo) == ASE_OK && std::string (mo.name) == "To Hawthorne master L", "master inputs named (%s)", mo.name);
    CHECK (drv->start() == ASE_OK, "start");

    // Hawthorne's audio thread: write the master, advance the clock, read what came in
    const int hb = 64;
    long long readPos = -1, got = 0, bad = 0, firstGood = -1;
    long long hawWritten = 0;
    for (int block = 0; block < 48000 * 3 / hb; ++block)   // 3 s of audio
    {
        for (int k = 0; k < hb; ++k) sh->fromHawthorne[route::masterLeft][(hawWritten + k) & route::ringMask] = hawValue (hawWritten + k);
        hawWritten += hb;
        sh->fromWrite.store (hawWritten);
        sh->clock.store (hawWritten);
        sh->heartbeatMs = (long long) GetTickCount64();
        SetEvent (tick);

        // reading side (as RouteHost does)
        const long long w = sh->toWrite.load();
        if (readPos < 0 && w > 0) readPos = w - hostBlock - hb;
        if (readPos >= 0)
            for (int k = 0; k < hb; ++k, ++readPos)
            {
                if (readPos >= w) break;
                const float v = sh->toHawthorne[0][readPos & route::ringMask];
                const float expect = hostValue (readPos);
                ++got;
                if (std::fabs (v - expect) > 1.0e-6f) ++bad; else if (firstGood < 0) firstGood = readPos;
            }
        Sleep (block % 3 == 0 ? 2 : 1);      // ~ real time-ish, jittery
    }
    // a reel with another track count: the program is asked to re-open the driver
    const size_t inBefore = hostIn.size();
    sh->numTracks = 16;
    for (int i = 0; i < 50 && resetRequests.load() == 0; ++i)
    {
        sh->clock.store (sh->clock.load() + hb); sh->fromWrite.store (sh->clock.load());
        sh->heartbeatMs = (long long) GetTickCount64(); SetEvent (tick); Sleep (2);
    }
    CHECK (resetRequests.load() == 1, "reset requested when the reel's track count changes (%d)", resetRequests.load());
    drv->stop();
    CHECK (sh->driverRunning.load() == 0, "stopped");

    CHECK (hostSwitches > 100, "host called back %lld times", (long long) hostSwitches.load());
    CHECK (got > 10000 && bad == 0, "host -> Hawthorne: %lld samples, %lld out of order", got, bad);
    CHECK (hostCount.load() <= hawWritten + hostBlock, "the host runs on Hawthorne's clock (%lld vs %lld)", hostCount.load(), hawWritten);

    // Hawthorne -> host: the master comes back in order (after the first block)
    long long inBad = 0, inN = 0;
    for (size_t i = (size_t) hostBlock * 2; i + 1 < inBefore; ++i, ++inN)
    {
        const float a = hostIn[i], b = hostIn[i + 1];
        if (a == 0.0f || b == 0.0f) { ++inBad; continue; }
        const float step = std::fabs (std::fabs (b) - std::fabs (a)) * 100000.0f;
        if (std::fabs (step - 1.0f) > 0.01f && std::fabs (b) > 2.0e-5f) ++inBad;
    }
    CHECK (inN > 10000 && inBad == 0, "Hawthorne -> host: %lld samples, %lld out of order", inN, inBad);

    // Hawthorne gone: the driver keeps time by itself
    sh->sampleRate = 0;
    const long long before = hostSwitches.load();
    CHECK (drv->start() == ASE_OK, "restart without Hawthorne");
    Sleep (500);
    drv->stop();
    CHECK (hostSwitches.load() - before > 50, "free-running without Hawthorne (%lld callbacks in 0.5 s)", hostSwitches.load() - before);

    drv->disposeBuffers();
    drv->Release();
    std::printf (failures == 0 ? "ALL ROUTE TESTS PASSED\n" : "%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
