// Pressing Save in Live for the person (Cmd+S on a Mac, Ctrl+S on Windows), so a plugin's sound
// and other save-only changes reach everyone without anyone having to remember.
//
// Safety: the keystroke only ever goes to Live. Without bringToFront it's sent only when Live is
// already the app in front. It's never sent while a modifier key or mouse button is held (Shift
// would turn it into Save As), and on a Mac only once macOS has allowed Tutti to send keys
// (Privacy & Security > Accessibility).
#include "Engine.h"

#include <string>

#if defined (__APPLE__)
 #include <ApplicationServices/ApplicationServices.h>
 #include <objc/message.h>
 #include <objc/runtime.h>

 #include <unistd.h>

namespace
{
    using Msg = id (*) (id, SEL);

    id send (id target, const char* selector)
    {
        return reinterpret_cast<Msg> (objc_msgSend) (target, sel_registerName (selector));
    }

    // The running Ableton Live (NSRunningApplication), or nil.
    id runningLive()
    {
        id workspace = send ((id) objc_getClass ("NSWorkspace"), "sharedWorkspace");
        id apps = send (workspace, "runningApplications");
        const auto count = reinterpret_cast<unsigned long (*) (id, SEL)> (objc_msgSend) (apps, sel_registerName ("count"));
        for (unsigned long i = 0; i < count; ++i)
        {
            id app = reinterpret_cast<id (*) (id, SEL, unsigned long)> (objc_msgSend) (apps, sel_registerName ("objectAtIndex:"), i);
            id bundle = send (app, "bundleIdentifier");
            if (bundle == nullptr)
                continue;
            const char* text = reinterpret_cast<const char* (*) (id, SEL)> (objc_msgSend) (bundle, sel_registerName ("UTF8String"));
            if (text != nullptr && std::string (text).rfind ("com.ableton.live", 0) == 0)
                return app;
        }
        return nullptr;
    }

    pid_t pidOf (id app)
    {
        return reinterpret_cast<pid_t (*) (id, SEL)> (objc_msgSend) (app, sel_registerName ("processIdentifier"));
    }

    bool liveInFront (id live)
    {
        id workspace = send ((id) objc_getClass ("NSWorkspace"), "sharedWorkspace");
        id front = send (workspace, "frontmostApplication");
        return front != nullptr && pidOf (front) == pidOf (live);
    }

    bool keysOrMouseHeld()
    {
        const auto flags = CGEventSourceFlagsState (kCGEventSourceStateCombinedSessionState);
        const auto modifiers = kCGEventFlagMaskShift | kCGEventFlagMaskControl | kCGEventFlagMaskAlternate | kCGEventFlagMaskCommand;
        return (flags & modifiers) != 0
               || CGEventSourceButtonState (kCGEventSourceStateCombinedSessionState, kCGMouseButtonLeft)
               || CGEventSourceButtonState (kCGEventSourceStateCombinedSessionState, kCGMouseButtonRight);
    }
}

bool allowedToPressKeys (bool askIfNot)
{
    const void* keys[] = { kAXTrustedCheckOptionPrompt };
    const void* values[] = { askIfNot ? kCFBooleanTrue : kCFBooleanFalse };
    auto options = CFDictionaryCreate (nullptr, keys, values, 1, &kCFCopyStringDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    const bool trusted = AXIsProcessTrustedWithOptions (options);
    CFRelease (options);
    return trusted;
}

int pressSaveInLive (bool bringToFront)
{
    id live = runningLive();
    if (live == nullptr)
        return tutti::saveKeyNoLive;
    if (! allowedToPressKeys (bringToFront))
        return tutti::saveKeyNeedsPermission;
    if (! liveInFront (live))
    {
        if (! bringToFront)
            return tutti::saveKeyNotInFront;
        reinterpret_cast<bool (*) (id, SEL, unsigned long)> (objc_msgSend) (live, sel_registerName ("activateWithOptions:"), 2ul /* ignoring other apps */);
        for (int i = 0; i < 20 && ! liveInFront (live); ++i)
            usleep (50 * 1000);
        if (! liveInFront (live))
            return tutti::saveKeyNotInFront;
    }
    if (keysOrMouseHeld())
        return tutti::saveKeyBusy;
    const auto pid = pidOf (live);
    const CGKeyCode s = 1; // kVK_ANSI_S
    auto down = CGEventCreateKeyboardEvent (nullptr, s, true);
    auto up = CGEventCreateKeyboardEvent (nullptr, s, false);
    if (down == nullptr || up == nullptr)
        return tutti::saveKeyFailed;
    CGEventSetFlags (down, kCGEventFlagMaskCommand);
    CGEventSetFlags (up, kCGEventFlagMaskCommand);
    CGEventPostToPid (pid, down);
    usleep (20 * 1000);
    CGEventPostToPid (pid, up);
    CFRelease (down);
    CFRelease (up);
    return tutti::saveKeyPressed;
}

#elif defined (_WIN32)
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>

namespace
{
    bool isLiveProcess (DWORD pid)
    {
        HANDLE process = OpenProcess (PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (process == nullptr)
            return false;
        wchar_t path[MAX_PATH] = {};
        DWORD size = MAX_PATH;
        const bool ok = QueryFullProcessImageNameW (process, 0, path, &size) != 0;
        CloseHandle (process);
        if (! ok)
            return false;
        std::wstring p (path);
        const auto slash = p.find_last_of (L"\\/");
        const auto name = slash == std::wstring::npos ? p : p.substr (slash + 1);
        return name.rfind (L"Ableton Live", 0) == 0;
    }

    struct Search
    {
        HWND found = nullptr;
    };

    BOOL CALLBACK findLiveWindow (HWND hwnd, LPARAM param)
    {
        if (! IsWindowVisible (hwnd) || GetWindow (hwnd, GW_OWNER) != nullptr || GetWindowTextLengthW (hwnd) == 0)
            return TRUE;
        DWORD pid = 0;
        GetWindowThreadProcessId (hwnd, &pid);
        if (isLiveProcess (pid))
        {
            reinterpret_cast<Search*> (param)->found = hwnd;
            return FALSE;
        }
        return TRUE;
    }

    bool liveInFront()
    {
        DWORD pid = 0;
        GetWindowThreadProcessId (GetForegroundWindow(), &pid);
        return pid != 0 && isLiveProcess (pid);
    }

    bool keysOrMouseHeld()
    {
        for (int key : { VK_SHIFT, VK_CONTROL, VK_MENU, VK_LWIN, VK_RWIN, VK_LBUTTON, VK_RBUTTON })
            if ((GetAsyncKeyState (key) & 0x8000) != 0)
                return true;
        return false;
    }
}

bool allowedToPressKeys (bool) { return true; }

int pressSaveInLive (bool bringToFront)
{
    if (! liveInFront())
    {
        Search search;
        EnumWindows (findLiveWindow, reinterpret_cast<LPARAM> (&search));
        if (search.found == nullptr)
            return tutti::saveKeyNoLive;
        if (! bringToFront)
            return tutti::saveKeyNotInFront;
        if (IsIconic (search.found))
            ShowWindow (search.found, SW_RESTORE);
        SetForegroundWindow (search.found);
        for (int i = 0; i < 20 && ! liveInFront(); ++i)
            Sleep (50);
        if (! liveInFront())
            return tutti::saveKeyNotInFront;
    }
    if (keysOrMouseHeld())
        return tutti::saveKeyBusy;
    INPUT inputs[4] = {};
    for (auto& in : inputs)
        in.type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = VK_CONTROL;
    inputs[1].ki.wVk = 'S';
    inputs[2].ki.wVk = 'S';
    inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
    inputs[3].ki.wVk = VK_CONTROL;
    inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
    return SendInput (4, inputs, sizeof (INPUT)) == 4 ? tutti::saveKeyPressed : tutti::saveKeyFailed;
}

#else
bool allowedToPressKeys (bool) { return false; }
int pressSaveInLive (bool) { return tutti::saveKeyFailed; }
#endif
