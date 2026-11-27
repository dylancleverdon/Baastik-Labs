// Pressing Save in Live for the person (Cmd+S on a Mac, Ctrl+S on Windows), so a plugin's sound
// and other save-only changes reach everyone without anyone having to remember.
//
// Safety: the keystroke only ever goes to Live. Without bringToFront it's sent only when Live is
// already the app in front. It's never sent while a modifier key or mouse button is held (Shift
// would turn it into Save As), and on a Mac only once macOS has allowed Tutti to send keys
// (Privacy & Security > Accessibility).
#include "Engine.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace
{
    // Live's window title shows the open song's name ("Night Drive (Tutti) [Night Drive]...").
    // An unreadable title only lets a click on the button through, never an automatic save.
    bool titleShows (const std::string& title, const std::string& song, bool clicked)
    {
        if (title.empty() || song.empty())
            return clicked && title.empty();
        auto lower = [] (std::string s) {
            std::transform (s.begin(), s.end(), s.begin(), [] (unsigned char c) { return (char) std::tolower (c); });
            return s;
        };
        return lower (title).find (lower (song)) != std::string::npos;
    }
}

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

namespace
{
    std::string cfString (CFTypeRef value)
    {
        if (value == nullptr || CFGetTypeID (value) != CFStringGetTypeID())
            return {};
        char buffer[1024] = {};
        CFStringGetCString ((CFStringRef) value, buffer, sizeof (buffer), kCFStringEncodingUTF8);
        return buffer;
    }

    // Live's main window title (it shows the open song's name), via Accessibility.
    std::string liveWindowTitle (pid_t pid)
    {
        auto app = AXUIElementCreateApplication (pid);
        if (app == nullptr)
            return {};
        std::string title;
        CFTypeRef window = nullptr;
        if (AXUIElementCopyAttributeValue (app, kAXMainWindowAttribute, &window) == kAXErrorSuccess && window != nullptr)
        {
            CFTypeRef value = nullptr;
            if (AXUIElementCopyAttributeValue ((AXUIElementRef) window, kAXTitleAttribute, &value) == kAXErrorSuccess)
            {
                title = cfString (value);
                if (value != nullptr)
                    CFRelease (value);
            }
            CFRelease (window);
        }
        CFRelease (app);
        return title;
    }

    // A Save panel: a sheet on Live's window, or a separate focused window that isn't the main one.
    bool saveDialogOpen (pid_t pid)
    {
        auto app = AXUIElementCreateApplication (pid);
        if (app == nullptr)
            return false;
        bool open = false;
        CFTypeRef focused = nullptr, main = nullptr;
        AXUIElementCopyAttributeValue (app, kAXFocusedWindowAttribute, &focused);
        AXUIElementCopyAttributeValue (app, kAXMainWindowAttribute, &main);
        for (CFTypeRef w : { focused, main })
        {
            if (w == nullptr)
                continue;
            CFTypeRef children = nullptr;
            if (AXUIElementCopyAttributeValue ((AXUIElementRef) w, kAXChildrenAttribute, &children) == kAXErrorSuccess && children != nullptr)
            {
                const auto n = CFArrayGetCount ((CFArrayRef) children);
                for (CFIndex i = 0; i < n && ! open; ++i)
                {
                    CFTypeRef role = nullptr;
                    if (AXUIElementCopyAttributeValue ((AXUIElementRef) CFArrayGetValueAtIndex ((CFArrayRef) children, i), kAXRoleAttribute, &role) == kAXErrorSuccess)
                    {
                        open = cfString (role) == "AXSheet";
                        if (role != nullptr)
                            CFRelease (role);
                    }
                }
                CFRelease (children);
            }
        }
        if (! open && focused != nullptr && main != nullptr && ! CFEqual (focused, main))
        {
            CFTypeRef title = nullptr;
            if (AXUIElementCopyAttributeValue ((AXUIElementRef) focused, kAXTitleAttribute, &title) == kAXErrorSuccess)
            {
                const auto t = cfString (title);
                open = t.find ("Save") != std::string::npos;
                if (title != nullptr)
                    CFRelease (title);
            }
        }
        if (focused != nullptr)
            CFRelease (focused);
        if (main != nullptr)
            CFRelease (main);
        CFRelease (app);
        return open;
    }
}

bool liveSaveDialogShowing()
{
    id live = runningLive();
    return live != nullptr && saveDialogOpen (pidOf (live));
}

std::string liveTitleForLog()
{
    id live = runningLive();
    return live != nullptr ? liveWindowTitle (pidOf (live)) : std::string();
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

int pressSaveInLive (bool bringToFront, const std::string& expectedSong)
{
    id live = runningLive();
    if (live == nullptr)
        return tutti::saveKeyNoLive;
    if (! allowedToPressKeys (bringToFront))
        return tutti::saveKeyNeedsPermission;
    // Only when Live is on the session song: anything else makes Live ask where to save.
    if (! titleShows (liveWindowTitle (pidOf (live)), expectedSong, bringToFront))
        return tutti::saveKeyWrongSong;
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

namespace
{
    std::string windowTitle (HWND hwnd)
    {
        wchar_t text[512] = {};
        GetWindowTextW (hwnd, text, 511);
        return juce::String (text).toStdString();
    }

    struct DialogSearch
    {
        bool found = false;
    };

    // A Save dialog: a standard dialog window ("#32770") belonging to Live.
    BOOL CALLBACK findLiveDialog (HWND hwnd, LPARAM param)
    {
        if (! IsWindowVisible (hwnd))
            return TRUE;
        wchar_t cls[64] = {};
        GetClassNameW (hwnd, cls, 63);
        DWORD pid = 0;
        GetWindowThreadProcessId (hwnd, &pid);
        if (std::wstring (cls) == L"#32770" && isLiveProcess (pid))
        {
            reinterpret_cast<DialogSearch*> (param)->found = true;
            return FALSE;
        }
        return TRUE;
    }
}

bool liveSaveDialogShowing()
{
    DialogSearch search;
    EnumWindows (findLiveDialog, reinterpret_cast<LPARAM> (&search));
    return search.found;
}

std::string liveTitleForLog()
{
    Search search;
    EnumWindows (findLiveWindow, reinterpret_cast<LPARAM> (&search));
    return search.found != nullptr ? windowTitle (search.found) : std::string();
}

bool allowedToPressKeys (bool) { return true; }

int pressSaveInLive (bool bringToFront, const std::string& expectedSong)
{
    {
        Search search;
        EnumWindows (findLiveWindow, reinterpret_cast<LPARAM> (&search));
        if (search.found == nullptr)
            return tutti::saveKeyNoLive;
        // Only when Live is on the session song: anything else makes Live ask where to save.
        if (! titleShows (windowTitle (search.found), expectedSong, bringToFront))
            return tutti::saveKeyWrongSong;
    }
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
int pressSaveInLive (bool, const std::string&) { return tutti::saveKeyFailed; }
bool liveSaveDialogShowing() { return false; }
std::string liveTitleForLog() { return {}; }
#endif
