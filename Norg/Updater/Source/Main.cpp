// NorgUpdater: keeps Norg up to date in the background.
//
//   NorgUpdater --auto                 launchd runs this at login and hourly (respects the auto-update setting)
//   NorgUpdater --check-now            install a newer version right away, if there is one
//   NorgUpdater --rollback             swap back to the previous version (and skip the build rolled back from)
//   NorgUpdater --set-auto-update on|off
//   NorgUpdater --install-agent        (re)install the launchd agent; run by the installer
//   NorgUpdater --status               print what's installed, as JSON
//
// Test hooks: --home <dir>, --manifest-url <url>, --allow-url-prefix <prefix>, --public-key <hex>.

#include "Updater.h"
#include "NorgVersion.h"

#include <iostream>

namespace
{
    juce::String argValue (const juce::StringArray& args, const juce::String& name)
    {
        const int i = args.indexOf (name);
        return i >= 0 && i + 1 < args.size() ? args[i + 1] : juce::String();
    }
}

int main (int argc, char* argv[])
{
    using namespace norg::update;

    juce::StringArray args;
    for (int i = 1; i < argc; ++i)
        args.add (juce::CharPointer_UTF8 (argv[i]));

    if (args.contains ("--version"))
    {
        std::cout << norg::version::string << std::endl;
        return 0;
    }

    const auto homeOverride = argValue (args, "--home");
    const Layout layout = homeOverride.isNotEmpty() ? Layout (juce::File (homeOverride)) : Layout::forCurrentUser();

    const auto log = [&layout] (const juce::String& message)
    {
        const auto line = juce::Time::getCurrentTime().toISO8601 (true) + "  " + message;
        std::cout << line << std::endl;

        layout.logsDir().createDirectory();
        const auto logFile = layout.logFile();
        if (logFile.getSize() > 512 * 1024)
            logFile.replaceWithText ({});
        logFile.appendText (line + "\n");
    };

    const auto keyOverride = argValue (args, "--public-key");
    const juce::String publicKey = keyOverride.isNotEmpty() ? keyOverride : juce::String (norg::version::updatePublicKeyHex);

    auto platform = createSystemPlatform();
    FileOps fileOps;
    Updater updater (layout, *platform, fileOps, publicKey, log);

    if (args.contains ("--status"))
    {
        std::cout << updater.statusJson() << std::endl;
        return 0;
    }

    if (args.contains ("--set-auto-update"))
    {
        auto settings = updater.loadSettings();
        settings.autoUpdate = argValue (args, "--set-auto-update") != "off";
        updater.saveSettings (settings);
        log (juce::String ("auto-update ") + (settings.autoUpdate ? "on" : "off"));
        return 0;
    }

    if (args.contains ("--install-agent"))
    {
        juce::String error;
        if (! platform->installLaunchAgent (layout, error))
        {
            log ("could not install the background updater: " + error);
            return 1;
        }
        log ("background updater installed");
        return 0;
    }

    // Only one updater at a time (launchd and a "Check now" click could overlap).
    juce::InterProcessLock lock ("com.baastiklabs.norg.updater");
    if (! lock.enter (0))
    {
        log ("another update is already running");
        return 0;
    }

    if (args.contains ("--rollback"))
        return updater.rollbackToPrevious().wasOk() ? 0 : 1;

    RunOptions options;
    options.manualCheck = args.contains ("--check-now");

    if (const auto url = argValue (args, "--manifest-url"); url.isNotEmpty())
        options.manifestUrl = url;

    for (int i = 0; i < args.size(); ++i)
        if (args[i] == "--allow-url-prefix" && i + 1 < args.size())
            options.extraAllowedPrefixes.add (args[i + 1]);

    switch (updater.checkAndInstall (options))
    {
        case Outcome::installed:
        case Outcome::upToDate:
        case Outcome::skipped:
        case Outcome::disabled:
            return 0;
        case Outcome::failed:
            break;
    }
    return 2;
}
