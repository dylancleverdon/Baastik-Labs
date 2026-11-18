// Clicking Tutti's Dock icon, or opening Tutti from Applications or Spotlight while it's running,
// sends it a "reopen" Apple event. JUCE ignores that event, so a closed window could only come
// back from the menu bar icon. This brings the window back, the way Mac apps normally behave.
#if defined (__APPLE__)
 #include <CoreServices/CoreServices.h>

 #include <functional>

namespace
{
    std::function<void()> onReopen;

    OSErr handleReopen (const AppleEvent*, AppleEvent*, SRefCon)
    {
        if (onReopen)
            onReopen();
        return noErr;
    }
}

// Call once the app has finished launching (macOS installs its own handler while launching, and
// the last one installed wins).
void installReopenHandler (std::function<void()> callback)
{
    onReopen = std::move (callback);
    static AEEventHandlerUPP upp = NewAEEventHandlerUPP (handleReopen);
    AEInstallEventHandler (kCoreEventClass, kAEReopenApplication, upp, 0, false);
}
#endif
