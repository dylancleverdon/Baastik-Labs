#pragma once

#include <juce_core/juce_core.h>
#include <vector>

// Where Norg lives on disk, and the file swaps that install or roll back an update.
// Everything is inside the user's home folder, so no admin rights are ever needed.
namespace norg::update
{
    struct InstallItem
    {
        juce::String name;      // file or bundle name inside an update payload
        juce::File destination; // where it lives once installed
    };

    struct Layout
    {
        explicit Layout (juce::File homeDir) : home (std::move (homeDir)) {}

        static Layout forCurrentUser() { return Layout (juce::File::getSpecialLocation (juce::File::userHomeDirectory)); }

        juce::File home;

        juce::File library() const            { return home.getChildFile ("Library"); }
        juce::File supportDir() const         { return library().getChildFile ("Application Support/Norg"); }
        juce::File componentsDir() const      { return library().getChildFile ("Audio/Plug-Ins/Components"); }
        juce::File vst3Dir() const            { return library().getChildFile ("Audio/Plug-Ins/VST3"); }
        juce::File appsDir() const            { return home.getChildFile ("Applications"); }
        juce::File logsDir() const            { return library().getChildFile ("Logs/Norg"); }
        juce::File launchAgentsDir() const    { return library().getChildFile ("LaunchAgents"); }

        juce::File installedJson() const      { return supportDir().getChildFile ("installed.json"); }
        juce::File settingsJson() const       { return supportDir().getChildFile ("updater.json"); }
        juce::File previousDir() const        { return supportDir().getChildFile ("previous"); }
        juce::File stagingDir() const         { return supportDir().getChildFile ("staging"); }
        juce::File updaterExecutable() const  { return supportDir().getChildFile ("NorgUpdater"); }
        juce::File logFile() const            { return logsDir().getChildFile ("updater.log"); }
        juce::File launchAgentPlist() const   { return launchAgentsDir().getChildFile (juce::String (launchAgentLabel) + ".plist"); }

        static constexpr const char* launchAgentLabel = "com.baastiklabs.norg.updater";

        // The updater replaces itself last, so a failure earlier never leaves it half-updated.
        std::vector<InstallItem> items() const
        {
            return { { "Norg.component", componentsDir().getChildFile ("Norg.component") },
                     { "Norg.vst3",      vst3Dir().getChildFile ("Norg.vst3") },
                     { "Norg.app",       appsDir().getChildFile ("Norg.app") },
                     { "NorgUpdater",    updaterExecutable() } };
        }
    };

    // File operations behind an interface so tests can inject failures part-way through.
    struct FileOps
    {
        virtual ~FileOps() = default;
        virtual bool move (const juce::File& from, const juce::File& to);
        virtual bool removeRecursively (const juce::File& target);
        virtual bool copyFile (const juce::File& from, const juce::File& to);
    };

    // Moves the payload's items into place. The current install is moved into previousDir()
    // (replacing any older one) so it can be rolled back. If any step fails, every step already
    // done is undone and the original install is left exactly as it was.
    juce::Result swapIn (const Layout&, const juce::File& payloadDir, FileOps&);

    // Swaps the previous install with the current one (so calling it twice rolls forward again).
    juce::Result rollback (const Layout&, FileOps&);

    bool hasPrevious (const Layout&);
}
