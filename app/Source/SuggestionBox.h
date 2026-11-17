#pragma once

#include "AppState.h"

#include <atomic>
#include <mutex>

// The suggestion box: suggestions are queued on disk and posted to the team's Google Sheet
// through its Apps Script web app (suggestions/Code.gs). The endpoint arrives with the update
// manifest, so it can be connected (or changed) without a new build. Each suggestion carries an
// id and the script ignores ids it already has, so a retry never makes a duplicate row.
class SuggestionBox
{
public:
    explicit SuggestionBox (juce::File dataDir);
    ~SuggestionBox();

    void add (const std::string& kind, const std::string& text, const std::string& from, const std::string& version,
              const std::string& os, const std::string& live);
    void setEndpoint (const std::string& url);
    void tick();

    SuggestionState state() const;
    std::function<void()> onChange;

private:
    void save();
    void sendNext();

    juce::File file;
    mutable std::mutex lock;
    tutti::Json queue = tutti::Json::array();
    std::string endpoint;
    std::string lastResult;
    std::atomic<bool> sending { false };
    juce::int64 nextTry = 0;
    std::unique_ptr<juce::Thread> worker;
};
