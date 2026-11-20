#pragma once

#include "Engine.h"

// Everything the window shows, gathered in one place so screens can be rendered from made-up
// data (screenshots) as easily as from the real engine.
struct UpdaterState
{
    std::string installedVersion, installedNotes;
    std::string availableVersion, availableNotes;
    std::string status;      // human-readable: "Up to date", "Downloading...", "Ready: restarts after the session"
    bool autoUpdate = true;
    bool canRollBack = false;
    bool busy = false;
};

struct SuggestionState
{
    int queued = 0;
    bool endpointKnown = false;
    std::string lastResult;
};

struct SetupState
{
    juce::StringArray scriptInstalledIn;
    bool startAtLogin = true;
};

struct AppState
{
    tutti::EngineView engine;
    UpdaterState updates;
    SuggestionState suggestions;
    SetupState setup;
};

// What the window can ask for.
struct AppActions
{
    virtual ~AppActions() = default;
    virtual void startSession (const juce::File& als) = 0;
    virtual void join (const std::string& peer) = 0;
    virtual void leave() = 0;
    virtual void approveJoin (const std::string& peer, bool allow) = 0;
    virtual void answerPlayer1 (const std::string& peer, bool allow) = 0;
    virtual void requestPlayer1() = 0;
    virtual void handOver (const std::string& peer) = 0;
    virtual void takeOver() = 0;
    virtual void playTogether() = 0;
    virtual void answerPlay (bool join) = 0;
    virtual void stopTogether() = 0;
    virtual void setPlayMode (const std::string& mode) = 0;
    virtual void openSong() = 0;
    virtual void revealSong() = 0;
    virtual void makeIdea() = 0;
    virtual void acceptIdea (const std::string& track) = 0;
    virtual void rejectIdea (const std::string& track) = 0;
    virtual void setIdentity (const std::string& name, int colour) = 0;
    virtual void checkForUpdates() = 0;
    virtual void rollBack() = 0;
    virtual void setAutoUpdate (bool) = 0;
    virtual void sendSuggestion (const std::string& kind, const std::string& text, bool withLogs) = 0;
    virtual void setStartAtLogin (bool) = 0;
    virtual void showLogs() = 0;
    virtual void openUrl (const juce::String&) = 0;
    virtual void updateNow() = 0;
    virtual void labelVersion (const std::string& label) = 0;
    virtual void restoreVersion (const std::string& id) = 0;
    virtual void openVersionCopy (const std::string& id) = 0;
    virtual void setBackupFolder (const juce::File& folder) = 0;
    virtual void setSharedFolder (const juce::File& folder) = 0;  // empty: stop using one
};
