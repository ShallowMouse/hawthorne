#include "RouteHost.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #define WIN32_LEAN_AND_MEAN
 #include <windows.h>
 #include <shellapi.h>
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
        shared->sampleRate = (std::int32_t) std::lround (sampleRate);
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

juce::String RouteHost::installDriver()
{
   #if JUCE_WINDOWS
    const auto dir = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();
    const auto fresh = dir.getChildFile ("HawthorneRoute.new.dll");
    const auto dll = dir.getChildFile ("HawthorneRoute.dll");
    if (fresh.existsAsFile())
        if (! fresh.copyFileTo (dll) && ! dll.existsAsFile())
            return "Couldn't put the driver in place: " + dll.getFullPathName();
    if (! dll.existsAsFile())
        return "The driver isn't included in this build of Hawthorne.";
    // (if the copy failed because a program has the driver open, the one in place is kept)
    return runElevated ("regsvr32.exe", "/s \"" + dll.getFullPathName() + "\"");
   #else
    return "Hawthorne Route is for Windows.";
   #endif
}

juce::String RouteHost::uninstallDriver()
{
   #if JUCE_WINDOWS
    const auto dll = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory().getChildFile ("HawthorneRoute.dll");
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
