#include "RouteHost.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #define WIN32_LEAN_AND_MEAN
 #include <windows.h>
 #include <shellapi.h>
 #include <cstring>
#endif

namespace hawthorne
{

void RouteHost::open (double sampleRate, int maxBlockSize)
{
   #if JUCE_WINDOWS
    if (shared == nullptr)
    {
        mapping = CreateFileMappingW (INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, (DWORD) sizeof (route::Shared), route::mappingName);
        if (mapping != nullptr)
            shared = static_cast<route::Shared*> (MapViewOfFile ((HANDLE) mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof (route::Shared)));
        tick = CreateEventW (nullptr, FALSE, FALSE, route::tickName);
        if (shared != nullptr && shared->magic != route::magic)
        {
            shared->magic = route::magic;
            shared->version = route::version;
        }
    }
    if (shared != nullptr)
    {
        shared->numTracks = tracks;
        shared->sampleRate = (std::int32_t) std::lround (sampleRate);
    }
   #else
    juce::ignoreUnused (sampleRate);
   #endif
    bufs.setSize (route::toHawthorneChannels, juce::jmax (1, maxBlockSize));
    bufs.clear();
    readPos = -1;
}

void RouteHost::close()
{
   #if JUCE_WINDOWS
    if (shared != nullptr) { shared->sampleRate = 0; UnmapViewOfFile (shared); shared = nullptr; }
    if (mapping != nullptr) { CloseHandle ((HANDLE) mapping); mapping = nullptr; }
    if (tick != nullptr) { CloseHandle ((HANDLE) tick); tick = nullptr; }
   #endif
}

void RouteHost::closeInfo()
{
   #if JUCE_WINDOWS
    if (info != nullptr) { info->numTracks = 0; UnmapViewOfFile (info); info = nullptr; }
    if (infoMapping != nullptr) { CloseHandle ((HANDLE) infoMapping); infoMapping = nullptr; }
   #endif
}

void RouteHost::publishInfo (const juce::String& reelName, int numTracks, const juce::StringArray& trackNames,
                             const std::vector<bool>& armedTracks, const std::vector<bool>& tracksToMix)
{
   #if JUCE_WINDOWS
    // only when something changed
    juce::String key = reelName + "|" + juce::String (numTracks);
    for (int t = 0; t < numTracks; ++t)
        key << "|" << trackNames[t] << (t < (int) armedTracks.size() && armedTracks[(size_t) t] ? "A" : "")
            << (t < (int) tracksToMix.size() && tracksToMix[(size_t) t] ? "M" : "");
    const auto h = key.hashCode64();
    if (info != nullptr && h == infoHash) return;

    if (info == nullptr)
    {
        infoMapping = CreateFileMappingW (INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, (DWORD) sizeof (route::Info), route::infoMappingName);
        if (infoMapping == nullptr) return;
        info = static_cast<route::Info*> (MapViewOfFile ((HANDLE) infoMapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof (route::Info)));
        if (info == nullptr) { CloseHandle ((HANDLE) infoMapping); infoMapping = nullptr; return; }
        info->magic = route::infoMagic;
        info->version = route::infoVersion;
    }
    infoHash = h;

    info->seq.fetch_add (1, std::memory_order_acq_rel);          // odd: writing
    std::atomic_thread_fence (std::memory_order_release);
    info->numTracks = juce::jlimit (0, route::maxTracks, numTracks);
    std::memset (info->reelName, 0, sizeof (info->reelName));
    reelName.copyToUTF8 (info->reelName, sizeof (info->reelName) - 1);
    for (int t = 0; t < route::maxTracks; ++t)
    {
        std::memset (info->trackNames[t], 0, route::infoNameBytes);
        if (t < numTracks) trackNames[t].trim().copyToUTF8 (info->trackNames[t], route::infoNameBytes - 1);
        info->armed[t] = (std::uint8_t) (t < numTracks && t < (int) armedTracks.size() && armedTracks[(size_t) t] ? 1 : 0);
        info->toMix[t] = (std::uint8_t) (t < numTracks && t < (int) tracksToMix.size() && tracksToMix[(size_t) t] ? 1 : 0);
    }
    std::atomic_thread_fence (std::memory_order_release);
    info->seq.fetch_add (1, std::memory_order_acq_rel);          // even: done
   #else
    juce::ignoreUnused (reelName, numTracks, trackNames, armedTracks, tracksToMix);
   #endif
}

bool RouteHost::isDriverInstalled()
{
   #if JUCE_WINDOWS
    return juce::WindowsRegistry::valueExists ("HKEY_LOCAL_MACHINE\\SOFTWARE\\ASIO\\Hawthorne Route\\CLSID");
   #else
    return false;
   #endif
}

#if JUCE_WINDOWS
static juce::String runElevated (const juce::String& exe, const juce::String& params)
{
    SHELLEXECUTEINFOW info {};
    info.cbSize = sizeof (info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS;
    info.lpVerb = L"runas";
    info.lpFile = exe.toWideCharPointer();
    info.lpParameters = params.toWideCharPointer();
    info.nShow = SW_HIDE;
    if (! ShellExecuteExW (&info) || info.hProcess == nullptr)
        return "Windows didn't allow it (the permission prompt was declined).";
    WaitForSingleObject (info.hProcess, 30000);
    DWORD code = 1;
    GetExitCodeProcess (info.hProcess, &code);
    CloseHandle (info.hProcess);
    return code == 0 ? juce::String() : "Registering the driver failed (code " + juce::String ((int) code) + ").";
}
#endif

#if JUCE_WINDOWS
static const char* const routeClsidKey = "HKEY_LOCAL_MACHINE\\SOFTWARE\\Classes\\CLSID\\{75AB7497-E772-4196-9A44-BEA8CEF177F6}\\InprocServer32\\";

// Each driver version has its own file name, so an update never has to overwrite a DLL
// another program (Reaper...) still has loaded - the registration just points to the new one.
static juce::File appDir()        { return juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory(); }
static juce::File versionedDriver() { return appDir().getChildFile ("HawthorneRoute-v" + juce::String ((int) route::version) + ".dll"); }
static juce::File registeredDriver() { return juce::File (juce::WindowsRegistry::getValue (routeClsidKey)); }
#endif

bool RouteHost::isDriverCurrent()
{
   #if JUCE_WINDOWS
    const auto reg = registeredDriver();
    return reg == versionedDriver() && reg.existsAsFile();
   #else
    return true;
   #endif
}

juce::String RouteHost::installDriver()
{
   #if JUCE_WINDOWS
    const auto fresh = appDir().getChildFile ("HawthorneRoute.new.dll");
    const auto dll = versionedDriver();
    if (fresh.existsAsFile() && ! (dll.existsAsFile() && dll.hasIdenticalContentTo (fresh)))
        if (! fresh.copyFileTo (dll))
            return "Couldn't put the driver in place (" + dll.getFileName() + ").\n"
                   "Close any program using Hawthorne Route (Reaper...) and try again.";
    if (! dll.existsAsFile())
        return "The driver isn't included in this build of Hawthorne.";
    const auto err = runElevated ("regsvr32.exe", "/s \"" + dll.getFullPathName() + "\"");
    if (err.isEmpty())
    {
        // tidy up older drivers (any still loaded in a program stay until it closes)
        appDir().getChildFile ("HawthorneRoute.dll").deleteFile();
        for (auto& f : appDir().findChildFiles (juce::File::findFiles, false, "HawthorneRoute-v*.dll"))
            if (f != dll) f.deleteFile();
    }
    return err;
   #else
    return "Hawthorne Route is for Windows.";
   #endif
}

juce::String RouteHost::uninstallDriver()
{
   #if JUCE_WINDOWS
    auto dll = registeredDriver();
    if (! dll.existsAsFile()) dll = versionedDriver();
    if (! dll.existsAsFile()) dll = appDir().getChildFile ("HawthorneRoute.dll");
    return runElevated ("regsvr32.exe", "/s /u \"" + dll.getFullPathName() + "\"");
   #else
    return {};
   #endif
}

void RouteHost::signal() noexcept
{
   #if JUCE_WINDOWS
    shared->heartbeatMs.store ((std::int64_t) GetTickCount64(), std::memory_order_relaxed);
    if (tick != nullptr) SetEvent ((HANDLE) tick);
   #endif
}

} // namespace hawthorne
