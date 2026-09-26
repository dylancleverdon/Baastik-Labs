#include "UpdaterBridge.h"

namespace norg
{
    UpdaterBridge::UpdaterBridge() : layout (update::Layout::forCurrentUser()) {}

    UpdaterBridge::~UpdaterBridge()
    {
        stopTimer();
    }

    UpdaterBridge::Status UpdaterBridge::read() const
    {
        Status s;
       #if JUCE_MAC
        s.available = layout.updaterExecutable().existsAsFile();
       #endif
        s.installed = update::parseReleaseInfo (layout.installedJson().loadFileAsString());
        s.autoUpdate = update::parseSettings (layout.settingsJson().loadFileAsString()).autoUpdate;
        s.previousAvailable = update::hasPrevious (layout);

        juce::StringArray lines;
        lines.addLines (layout.logFile().loadFileAsString());
        lines.removeEmptyStrings();
        if (! lines.isEmpty())
            s.lastLogLine = lines[lines.size() - 1].fromFirstOccurrenceOf ("  ", false, false);

        return s;
    }

    void UpdaterBridge::setAutoUpdate (bool enabled)
    {
        auto settings = update::parseSettings (layout.settingsJson().loadFileAsString());
        settings.autoUpdate = enabled;
        layout.supportDir().createDirectory();
        layout.settingsJson().replaceWithText (update::toJson (settings));
    }

    void UpdaterBridge::run (juce::StringArray args, std::function<void (bool, juce::String)> done)
    {
        if (process != nullptr)
            return;

        args.insert (0, layout.updaterExecutable().getFullPathName());

        auto child = std::make_unique<juce::ChildProcess>();
        if (! child->start (args, 0))
        {
            if (done)
                done (false, "The Norg updater isn't installed.");
            return;
        }

        process = std::move (child);
        onDone = std::move (done);
        startTimer (250);
    }

    void UpdaterBridge::timerCallback()
    {
        if (process == nullptr || process->isRunning())
            return;

        stopTimer();
        const bool ok = process->getExitCode() == 0;
        process.reset();

        if (auto callback = std::move (onDone))
            callback (ok, read().lastLogLine);
    }
}
