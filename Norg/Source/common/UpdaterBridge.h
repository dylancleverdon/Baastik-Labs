#pragma once

#include "Installer.h"
#include "UpdateCore.h"

#include <juce_events/juce_events.h>

#include <functional>
#include <memory>

namespace norg
{
    // The plugin's view of the background updater: reads what's installed and runs the
    // NorgUpdater helper for "Check now" and "Roll back".
    //
    // The helper runs as its own process with no pipes back to us, so it finishes its work even if
    // the plugin is closed or unloaded mid-update.
    class UpdaterBridge final : private juce::Timer
    {
    public:
        struct Status
        {
            bool available = false;              // installed via the Norg installer on macOS
            std::optional<update::ReleaseInfo> installed;
            bool autoUpdate = true;
            bool previousAvailable = false;
            juce::String lastLogLine;
        };

        UpdaterBridge();
        ~UpdaterBridge() override;

        Status read() const;
        void setAutoUpdate (bool enabled);

        // Starts NorgUpdater with the given arguments; `done` is called on the message thread
        // when it exits, with whether it succeeded and the last line it logged.
        void run (juce::StringArray args, std::function<void (bool ok, juce::String message)> done);
        bool isBusy() const { return process != nullptr; }

    private:
        void timerCallback() override;

        update::Layout layout;
        std::unique_ptr<juce::ChildProcess> process;
        std::function<void (bool, juce::String)> onDone;
    };
}
