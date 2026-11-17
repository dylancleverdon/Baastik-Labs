#pragma once

#include "AppState.h"
#include "tutti/Update.h"

#include <atomic>
#include <mutex>

// Keeps Tutti up to date, Norg-style: checks the tutti-channel manifest at launch and hourly,
// downloads the payload for this OS over HTTPS from this repo's tutti-* releases, checks its
// SHA-256, and swaps it in when no session is running, then relaunches. The previous version is
// kept for Roll back (which also skips the build rolled back from).
class AppUpdater
{
public:
    struct Options
    {
        juce::File dataDir;
        juce::String manifestUrl = tutti::update::manifestUrl;
        std::vector<std::string> extraPrefixes;     // test hook
        std::function<bool()> safeToInstall;         // false while in a session
        std::function<void()> quitForRelaunch;
    };

    explicit AppUpdater (Options);
    ~AppUpdater();

    void tick();
    void checkNow();
    void rollBack();
    void setAutoUpdate (bool);

    UpdaterState state() const;
    std::string suggestionsEndpoint() const;
    std::function<void()> onChange;

    // Removes leftovers from the previous update (Windows keeps the old .exe until restart).
    static void cleanUpAfterUpdate();

private:
    void startCheck (bool manual);
    void runCheck (bool manual);
    bool installStaged();
    bool swapIn (const juce::File& payloadDir, const juce::File& previousDir);
    void relaunch();
    void saveSettings();
    void setStatus (const std::string&);

    Options options;
    tutti::update::Settings settings;
    juce::int64 lastCheck = 0;
    std::string endpoint;

    mutable std::mutex lock;
    std::string status;
    std::string availableVersion, availableNotes;
    juce::File staged;           // extracted payload ready to install
    juce::int64 stagedBuild = 0;
    std::string stagedVersion, stagedNotes;
    std::atomic<bool> busy { false };
    std::unique_ptr<juce::Thread> worker;
};
