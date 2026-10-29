// Hawthorne Route - a virtual ASIO driver.
//
// Pick "Hawthorne Route" as the ASIO device in another program (Reaper, a synth...):
//   its outputs 1-32  -> Hawthorne inputs ROUTE 1-32
//   its inputs  1-2   <- Hawthorne's master mix
// Clocked by Hawthorne's own audio device (see Source/Route/RouteShared.h), so the
// two run sample-synchronously. Without Hawthorne running it keeps time by itself.
//
// A plain Win32 COM in-process server (no JUCE). Registered machine-wide (ASIO hosts
// look in HKLM\SOFTWARE\ASIO), which Hawthorne does once with an elevated regsvr32.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <olectl.h>
#include <avrt.h>
#include <atomic>
#include <thread>
#include <vector>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <string>

#include "asiosys.h"
#include "asio.h"
#include "iasiodrv.h"

#include "../Route/RouteShared.h"

namespace
{
    // {75AB7497-E772-4196-9A44-BEA8CEF177F6}
    const CLSID CLSID_HawthorneRoute = { 0x75ab7497, 0xe772, 0x4196, { 0x9a, 0x44, 0xbe, 0xa8, 0xce, 0xf1, 0x77, 0xf6 } };
    const char* const clsidString = "{75AB7497-E772-4196-9A44-BEA8CEF177F6}";
    const char* const driverName  = "Hawthorne Route";

    HMODULE thisModule = nullptr;
    std::atomic<long> objectCount { 0 }, lockCount { 0 };

    using namespace hawthorne;

    long long nowMs() { return (long long) GetTickCount64(); }

    //==========================================================================
    class RouteDriver : public IASIO
    {
    public:
        RouteDriver()  { ++objectCount; }
        ~RouteDriver() { stopThread(); disposeBuffers(); closeShared(); --objectCount; }

        // IUnknown (ASIO hosts ask for the driver's CLSID as the interface)
        HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, void** ppv) override
        {
            if (ppv == nullptr) return E_POINTER;
            if (IsEqualIID (riid, IID_IUnknown) || IsEqualIID (riid, CLSID_HawthorneRoute))
            {
                *ppv = static_cast<IASIO*> (this);
                AddRef();
                return S_OK;
            }
            *ppv = nullptr;
            return E_NOINTERFACE;
        }
        ULONG STDMETHODCALLTYPE AddRef() override  { return (ULONG) ++refs; }
        ULONG STDMETHODCALLTYPE Release() override
        {
            const long r = --refs;
            if (r == 0) delete this;
            return (ULONG) r;
        }

        // IASIO
        ASIOBool init (void*) override
        {
            openShared();
            std::strcpy (error, "");
            return ASIOTrue;
        }
        void getDriverName (char* name) override       { std::strcpy (name, driverName); }
        long getDriverVersion() override               { return 1; }
        void getErrorMessage (char* s) override        { std::strcpy (s, error); }

        ASIOError start() override
        {
            if (! buffersReady) return ASE_NotPresent;
            if (running) return ASE_OK;
            running = true;
            switchCount = 0;
            if (shared != nullptr)
            {
                shared->driverBlock = block;
                shared->driverClientId.fetch_add (1);
                shared->driverRunning = 1;
            }
            worker = std::thread ([this] { run(); });
            return ASE_OK;
        }
        ASIOError stop() override
        {
            stopThread();
            return ASE_OK;
        }

        // The channels follow the reel on Hawthorne: tracks 1..N, then master L / R (and LTC on
        // the inputs). Taken when the program asks, and kept until it re-opens the driver.
        int liveTracks() const { return route::tracksOf (shared != nullptr ? (int) shared->numTracks.load() : 0); }
        long numIns() const  { return tracks + 3; }
        long numOuts() const { return tracks + 2; }
        int inSlot (long c) const
        {
            if (c < 0) return -1;
            if (c < tracks) return (int) c;
            if (c == tracks) return route::masterLeft;
            if (c == tracks + 1) return route::masterRight;
            if (c == tracks + 2) return route::ltcChannel;
            return -1;
        }
        int outSlot (long c) const
        {
            if (c < 0) return -1;
            if (c < tracks) return (int) c;
            if (c == tracks) return route::toMasterLeft;
            if (c == tracks + 1) return route::toMasterRight;
            return -1;
        }

        ASIOError getChannels (long* numIn, long* numOut) override
        {
            if (! buffersReady) tracks = liveTracks();
            if (numIn)  *numIn  = numIns();
            if (numOut) *numOut = numOuts();
            return ASE_OK;
        }
        ASIOError getLatencies (long* inLat, long* outLat) override
        {
            // our block, plus the block of Hawthorne's side of the ring
            if (inLat)  *inLat  = block;
            if (outLat) *outLat = block * 2;
            return ASE_OK;
        }
        ASIOError getBufferSize (long* minSize, long* maxSize, long* preferred, long* granularity) override
        {
            if (minSize) *minSize = 32;
            if (maxSize) *maxSize = 4096;
            if (preferred) *preferred = 256;
            if (granularity) *granularity = -1;     // powers of two
            return ASE_OK;
        }

        int hawthorneRate() const { return shared != nullptr ? (int) shared->sampleRate.load() : 0; }
        bool hawthorneAlive() const
        {
            return shared != nullptr && shared->sampleRate.load() > 0 && nowMs() - shared->heartbeatMs.load() < 250;
        }

        ASIOError canSampleRate (ASIOSampleRate sr) override
        {
            const int hr = hawthorneRate();
            if (hr > 0) return std::fabs (sr - hr) < 1.0 ? ASE_OK : ASE_NoClock;
            for (double r : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
                if (std::fabs (sr - r) < 1.0) return ASE_OK;
            return ASE_NoClock;
        }
        ASIOError getSampleRate (ASIOSampleRate* sr) override
        {
            if (sr == nullptr) return ASE_InvalidParameter;
            const int hr = hawthorneRate();
            *sr = hr > 0 ? (double) hr : rate;
            return ASE_OK;
        }
        ASIOError setSampleRate (ASIOSampleRate sr) override
        {
            if (canSampleRate (sr) != ASE_OK) return ASE_NoClock;   // follows Hawthorne's rate
            rate = sr;
            return ASE_OK;
        }
        ASIOError getClockSources (ASIOClockSource* clocks, long* num) override
        {
            if (clocks == nullptr || num == nullptr || *num < 1) return ASE_InvalidParameter;
            clocks[0].index = 0;
            clocks[0].associatedChannel = -1;
            clocks[0].associatedGroup = -1;
            clocks[0].isCurrentSource = ASIOTrue;
            std::strcpy (clocks[0].name, "Hawthorne");
            *num = 1;
            return ASE_OK;
        }
        ASIOError setClockSource (long ref) override { return ref == 0 ? ASE_OK : ASE_InvalidParameter; }

        ASIOError getSamplePosition (ASIOSamples* pos, ASIOTimeStamp* ts) override
        {
            const unsigned long long p = (unsigned long long) switchCount.load() * (unsigned long long) block;
            const unsigned long long ns = (unsigned long long) lastSwitchNs.load();
            if (pos) { pos->hi = (unsigned long) (p >> 32); pos->lo = (unsigned long) (p & 0xffffffffULL); }
            if (ts)  { ts->hi = (unsigned long) (ns >> 32); ts->lo = (unsigned long) (ns & 0xffffffffULL); }
            return ASE_OK;
        }

        ASIOError getChannelInfo (ASIOChannelInfo* info) override
        {
            if (info == nullptr) return ASE_InvalidParameter;
            const bool in = info->isInput != ASIOFalse;
            const long n = in ? numIns() : numOuts();
            if (info->channel < 0 || info->channel >= n) return ASE_InvalidParameter;
            info->isActive = ASIOFalse;
            for (auto& b : bufs) if ((b.isInput != 0) == in && b.channel == info->channel) info->isActive = ASIOTrue;
            info->channelGroup = 0;
            info->type = ASIOSTFloat32LSB;
            if (in)
            {
                const int s = inSlot (info->channel);
                if (s == route::masterLeft)       std::snprintf (info->name, sizeof (info->name), "Hawthorne master L");
                else if (s == route::masterRight) std::snprintf (info->name, sizeof (info->name), "Hawthorne master R");
                else if (s == route::ltcChannel)  std::snprintf (info->name, sizeof (info->name), "Hawthorne LTC");
                else std::snprintf (info->name, sizeof (info->name), "Hawthorne track %ld", info->channel + 1);
            }
            else
            {
                const int s = outSlot (info->channel);
                if (s == route::toMasterLeft)       std::snprintf (info->name, sizeof (info->name), "To Hawthorne master L");
                else if (s == route::toMasterRight) std::snprintf (info->name, sizeof (info->name), "To Hawthorne master R");
                else std::snprintf (info->name, sizeof (info->name), "To Hawthorne track %ld", info->channel + 1);
            }
            return ASE_OK;
        }

        ASIOError createBuffers (ASIOBufferInfo* infos, long numChannels, long bufferSize, ASIOCallbacks* cb) override
        {
            if (running) return ASE_InvalidMode;
            disposeBuffers();
            if (infos == nullptr || cb == nullptr || bufferSize < 16 || bufferSize > 8192) return ASE_InvalidParameter;
            block = bufferSize;
            callbacks = cb;
            tracks = liveTracks();
            resetAsked = false;
            for (long i = 0; i < numChannels; ++i)
            {
                auto& in = infos[i];
                const bool isIn = in.isInput != ASIOFalse;
                const long n = isIn ? numIns() : numOuts();
                if (in.channelNum < 0 || in.channelNum >= n) { disposeBuffers(); return ASE_InvalidParameter; }
                Buf b;
                b.isInput = isIn ? 1 : 0;
                b.channel = in.channelNum;
                b.slot = isIn ? inSlot (in.channelNum) : outSlot (in.channelNum);
                b.data.assign ((size_t) block * 2, 0.0f);
                bufs.push_back (std::move (b));
            }
            for (long i = 0; i < numChannels; ++i)
            {
                auto& b = bufs[(size_t) i];
                infos[i].buffers[0] = b.data.data();
                infos[i].buffers[1] = b.data.data() + block;
            }
            timeInfo = callbacks->asioMessage != nullptr
                       && callbacks->asioMessage (kAsioSelectorSupported, kAsioSupportsTimeInfo, nullptr, nullptr) != 0
                       && callbacks->asioMessage (kAsioSupportsTimeInfo, 0, nullptr, nullptr) != 0;
            buffersReady = true;
            return ASE_OK;
        }
        ASIOError disposeBuffers() override
        {
            stopThread();
            bufs.clear();
            buffersReady = false;
            callbacks = nullptr;
            return ASE_OK;
        }

        ASIOError controlPanel() override
        {
            char msg[512];
            const int hr = hawthorneRate();
            std::snprintf (msg, sizeof (msg),
                           "Hawthorne Route\n\n"
                           "Channels follow the reel loaded on Hawthorne (%d tracks now).\n"
                           "Your outputs: one per track, then master L / R - into Hawthorne as ROUTE inputs.\n"
                           "Your inputs: every Hawthorne track, then its master L / R, then LTC.\n\n"
                           "Hawthorne: %s\n"
                           "The sample rate follows Hawthorne's audio device.",
                           liveTracks(), hawthorneAlive() ? (std::string ("running at ") + std::to_string (hr) + " Hz").c_str()
                                            : "not running (silent until it starts)");
            MessageBoxA (nullptr, msg, driverName, MB_OK | MB_ICONINFORMATION);
            return ASE_OK;
        }

        ASIOError future (long selector, void*) override
        {
            switch (selector)
            {
                case kAsioCanTimeInfo:  return ASE_SUCCESS;
                default:                return ASE_InvalidParameter;
            }
        }
        ASIOError outputReady() override { return ASE_NotPresent; }

    private:
        struct Buf { int isInput = 0; long channel = 0; int slot = 0; std::vector<float> data; };

        void openShared()
        {
            if (shared != nullptr) return;
            mapping = CreateFileMappingW (INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, (DWORD) sizeof (route::Shared), route::mappingName);
            if (mapping == nullptr) return;
            shared = static_cast<route::Shared*> (MapViewOfFile (mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof (route::Shared)));
            if (shared != nullptr && shared->magic != route::magic)
            {
                shared->magic = route::magic;
                shared->version = route::version;
            }
            tick = CreateEventW (nullptr, FALSE, FALSE, route::tickName);
        }
        void closeShared()
        {
            if (shared != nullptr) { shared->driverRunning = 0; UnmapViewOfFile (shared); shared = nullptr; }
            if (mapping != nullptr) { CloseHandle (mapping); mapping = nullptr; }
            if (tick != nullptr) { CloseHandle (tick); tick = nullptr; }
        }

        void stopThread()
        {
            running = false;
            if (tick != nullptr) SetEvent (tick);
            if (worker.joinable()) worker.join();
            if (shared != nullptr) shared->driverRunning = 0;
        }

        // One buffer: Hawthorne's master in, the program's callback, its outputs to Hawthorne.
        void doSwitch (long long framePos, bool alive)
        {
            const int idx = (int) (switchCount.load() & 1);
            for (auto& b : bufs)
                if (b.isInput)
                {
                    float* dst = b.data.data() + (size_t) idx * (size_t) block;
                    if (alive && shared->fromWrite.load() >= framePos + block && framePos >= 0)
                        for (long k = 0; k < block; ++k)
                            dst[k] = shared->fromHawthorne[b.slot][(framePos + k) & route::ringMask];
                    else
                        std::memset (dst, 0, sizeof (float) * (size_t) block);
                }

            LARGE_INTEGER qpc, freq;
            QueryPerformanceCounter (&qpc);
            QueryPerformanceFrequency (&freq);
            lastSwitchNs = (long long) ((double) qpc.QuadPart * 1.0e9 / (double) freq.QuadPart);

            if (timeInfo && callbacks->bufferSwitchTimeInfo != nullptr)
            {
                ASIOTime t {};
                const unsigned long long p = (unsigned long long) switchCount.load() * (unsigned long long) block;
                t.timeInfo.samplePosition.hi = (unsigned long) (p >> 32);
                t.timeInfo.samplePosition.lo = (unsigned long) (p & 0xffffffffULL);
                const unsigned long long ns = (unsigned long long) lastSwitchNs.load();
                t.timeInfo.systemTime.hi = (unsigned long) (ns >> 32);
                t.timeInfo.systemTime.lo = (unsigned long) (ns & 0xffffffffULL);
                ASIOSampleRate sr = 0.0;
                getSampleRate (&sr);
                t.timeInfo.sampleRate = sr;
                t.timeInfo.flags = kSystemTimeValid | kSamplePositionValid | kSampleRateValid;
                callbacks->bufferSwitchTimeInfo (&t, idx, ASIOTrue);
            }
            else if (callbacks->bufferSwitch != nullptr)
                callbacks->bufferSwitch (idx, ASIOTrue);

            // what the program played: on to Hawthorne
            if (shared != nullptr)
            {
                const long long w = shared->toWrite.load();
                for (int ch = 0; ch < route::toHawthorneChannels; ++ch)
                {
                    const Buf* src = nullptr;
                    for (auto& b : bufs) if (! b.isInput && b.slot == ch) { src = &b; break; }
                    float* ring = shared->toHawthorne[ch];
                    for (long k = 0; k < block; ++k)
                        ring[(w + k) & route::ringMask] = src != nullptr ? src->data[(size_t) idx * (size_t) block + (size_t) k] : 0.0f;
                }
                shared->toWrite.store (w + block);
            }
            ++switchCount;
        }

        void run()
        {
            DWORD taskIndex = 0;
            HANDLE avrt = AvSetMmThreadCharacteristicsW (L"Pro Audio", &taskIndex);
            SetThreadPriority (GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

            bool wasAlive = false;
            long long pos = 0;                  // Hawthorne frame our next buffer starts at
            LARGE_INTEGER freq, start, now;
            QueryPerformanceFrequency (&freq);
            QueryPerformanceCounter (&start);
            long long freeFrames = 0;

            while (running)
            {
                const bool alive = hawthorneAlive();
                // the reel on Hawthorne changed its track count: ask the program to re-open us
                if (alive && ! resetAsked && liveTracks() != tracks && callbacks != nullptr && callbacks->asioMessage != nullptr
                     && callbacks->asioMessage (kAsioSelectorSupported, kAsioResetRequest, nullptr, nullptr) != 0)
                {
                    resetAsked = true;
                    callbacks->asioMessage (kAsioResetRequest, 0, nullptr, nullptr);
                }
                if (alive)
                {
                    const long long clk = shared->clock.load();
                    if (! wasAlive || clk - pos > 8LL * block || clk < pos - 8LL * block)
                        pos = clk - block;          // (re)lock onto Hawthorne's clock
                    wasAlive = true;
                    while (running && shared->clock.load() - pos >= block)
                    {
                        doSwitch (pos, true);
                        pos += block;
                    }
                    WaitForSingleObject (tick, 20);
                }
                else
                {
                    // Hawthorne isn't running: keep time ourselves, silently
                    if (wasAlive) { QueryPerformanceCounter (&start); freeFrames = 0; wasAlive = false; }
                    ASIOSampleRate sr = 48000.0;
                    getSampleRate (&sr);
                    QueryPerformanceCounter (&now);
                    const double elapsed = (double) (now.QuadPart - start.QuadPart) / (double) freq.QuadPart;
                    const long long due = (long long) (elapsed * sr);
                    if (due - freeFrames >= block)
                    {
                        doSwitch (-1, false);
                        freeFrames += block;
                    }
                    else
                        Sleep (1);
                }
            }
            if (avrt != nullptr) AvRevertMmThreadCharacteristics (avrt);
        }

        std::atomic<long> refs { 1 };
        std::atomic<bool> running { false };
        std::atomic<long long> switchCount { 0 }, lastSwitchNs { 0 };
        std::thread worker;
        std::vector<Buf> bufs;
        ASIOCallbacks* callbacks = nullptr;
        bool buffersReady = false, timeInfo = false, resetAsked = false;
        int tracks = route::defaultTracks;
        long block = 256;
        double rate = 48000.0;
        char error[128] {};

        HANDLE mapping = nullptr, tick = nullptr;
        route::Shared* shared = nullptr;
    };

    //==========================================================================
    class Factory : public IClassFactory
    {
    public:
        HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, void** ppv) override
        {
            if (ppv == nullptr) return E_POINTER;
            if (IsEqualIID (riid, IID_IUnknown) || IsEqualIID (riid, IID_IClassFactory)) { *ppv = this; return S_OK; }
            *ppv = nullptr;
            return E_NOINTERFACE;
        }
        ULONG STDMETHODCALLTYPE AddRef() override  { return 2; }
        ULONG STDMETHODCALLTYPE Release() override { return 1; }
        HRESULT STDMETHODCALLTYPE CreateInstance (IUnknown* outer, REFIID riid, void** ppv) override
        {
            if (outer != nullptr) return CLASS_E_NOAGGREGATION;
            auto* d = new RouteDriver();
            const HRESULT hr = d->QueryInterface (riid, ppv);
            d->Release();
            return hr;
        }
        HRESULT STDMETHODCALLTYPE LockServer (BOOL lock) override
        {
            if (lock) ++lockCount; else --lockCount;
            return S_OK;
        }
    };
    Factory factory;

    bool setKey (HKEY root, const char* path, const char* name, const char* value)
    {
        HKEY k = nullptr;
        if (RegCreateKeyExA (root, path, 0, nullptr, 0, KEY_WRITE | KEY_WOW64_64KEY, nullptr, &k, nullptr) != ERROR_SUCCESS)
            return false;
        const LONG r = RegSetValueExA (k, name, 0, REG_SZ, (const BYTE*) value, (DWORD) std::strlen (value) + 1);
        RegCloseKey (k);
        return r == ERROR_SUCCESS;
    }
}

//==============================================================================
extern "C" BOOL WINAPI DllMain (HINSTANCE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) { thisModule = instance; DisableThreadLibraryCalls (instance); }
    return TRUE;
}

STDAPI DllGetClassObject (REFCLSID clsid, REFIID riid, void** ppv)
{
    if (! IsEqualCLSID (clsid, CLSID_HawthorneRoute)) return CLASS_E_CLASSNOTAVAILABLE;
    return factory.QueryInterface (riid, ppv);
}

STDAPI DllCanUnloadNow()
{
    return objectCount.load() == 0 && lockCount.load() == 0 ? S_OK : S_FALSE;
}

STDAPI DllRegisterServer()
{
    char path[MAX_PATH];
    GetModuleFileNameA (thisModule, path, MAX_PATH);
    const std::string clsidKey = std::string ("SOFTWARE\\Classes\\CLSID\\") + clsidString;
    const std::string asioKey  = std::string ("SOFTWARE\\ASIO\\") + driverName;
    const bool ok = setKey (HKEY_LOCAL_MACHINE, clsidKey.c_str(), nullptr, driverName)
                 && setKey (HKEY_LOCAL_MACHINE, (clsidKey + "\\InprocServer32").c_str(), nullptr, path)
                 && setKey (HKEY_LOCAL_MACHINE, (clsidKey + "\\InprocServer32").c_str(), "ThreadingModel", "Apartment")
                 && setKey (HKEY_LOCAL_MACHINE, asioKey.c_str(), "CLSID", clsidString)
                 && setKey (HKEY_LOCAL_MACHINE, asioKey.c_str(), "Description", driverName);
    return ok ? S_OK : SELFREG_E_CLASS;
}

STDAPI DllUnregisterServer()
{
    const std::string clsidKey = std::string ("SOFTWARE\\Classes\\CLSID\\") + clsidString;
    const std::string asioKey  = std::string ("SOFTWARE\\ASIO\\") + driverName;
    RegDeleteKeyExA (HKEY_LOCAL_MACHINE, (clsidKey + "\\InprocServer32").c_str(), KEY_WOW64_64KEY, 0);
    RegDeleteKeyExA (HKEY_LOCAL_MACHINE, clsidKey.c_str(), KEY_WOW64_64KEY, 0);
    RegDeleteKeyExA (HKEY_LOCAL_MACHINE, asioKey.c_str(), KEY_WOW64_64KEY, 0);
    return S_OK;
}
