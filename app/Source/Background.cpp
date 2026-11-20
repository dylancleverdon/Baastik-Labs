// Keeps Tutti running at full speed while its window is hidden. Both macOS (App Nap) and
// Windows (power throttling / "Efficiency mode") slow down apps working in the background,
// which delays Tutti's messages until friends' Tuttis think it has dropped out.
#include <string>

#if defined (__APPLE__)
 #include <objc/message.h>
 #include <objc/runtime.h>

 #include <cstdint>

std::string keepRunningInBackground()
{
    // [[NSProcessInfo processInfo] beginActivityWithOptions:... reason:...], held forever.
    using Msg = id (*) (id, SEL);
    auto info = reinterpret_cast<Msg> (objc_msgSend) ((id) objc_getClass ("NSProcessInfo"), sel_registerName ("processInfo"));
    auto reason = reinterpret_cast<id (*) (id, SEL, const char*)> (objc_msgSend) (
        (id) objc_getClass ("NSString"), sel_registerName ("stringWithUTF8String:"), "Tutti keeps your session connected");
    // NSActivityUserInitiatedAllowingIdleSystemSleep | NSActivityLatencyCritical: no App Nap and
    // no timer coalescing, but the Mac can still sleep when idle.
    const uint64_t options = 0x00EFFFFFULL | 0xFF00000000ULL;
    auto activity = reinterpret_cast<id (*) (id, SEL, uint64_t, id)> (objc_msgSend) (
        info, sel_registerName ("beginActivityWithOptions:reason:"), options, reason);
    if (activity == nullptr)
        return "couldn't turn off App Nap";
    reinterpret_cast<Msg> (objc_msgSend) (activity, sel_registerName ("retain"));
    return "App Nap is off for Tutti";
}

#elif defined (_WIN32)
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>

std::string keepRunningInBackground()
{
    // SetProcessInformation(ProcessPowerThrottling) exists from Windows 10 1709; look it up so
    // older SDKs and systems are fine.
    struct State
    {
        ULONG version, controlMask, stateMask;
    };
    using Fn = BOOL (WINAPI*) (HANDLE, int, LPVOID, DWORD);
    auto fn = reinterpret_cast<Fn> (reinterpret_cast<void*> (GetProcAddress (GetModuleHandleW (L"kernel32.dll"), "SetProcessInformation")));
    if (fn == nullptr)
        return "power throttling can't be changed on this Windows";
    State state { 1 /* PROCESS_POWER_THROTTLING_CURRENT_VERSION */,
                  0x1 | 0x4 /* EXECUTION_SPEED | IGNORE_TIMER_RESOLUTION */,
                  0 /* off for both */ };
    const int processPowerThrottling = 4; // PROCESS_INFORMATION_CLASS::ProcessPowerThrottling
    if (! fn (GetCurrentProcess(), processPowerThrottling, &state, sizeof (state)))
        return "couldn't turn off power throttling (error " + std::to_string (GetLastError()) + ")";
    return "power throttling is off for Tutti";
}

#else
std::string keepRunningInBackground() { return {}; }
#endif
