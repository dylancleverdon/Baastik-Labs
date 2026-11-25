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

    // logs: Tutti's (and Live's) recent logs to attach, or empty. They're kept in a file next to
    // the queue until sent; the sheet's script stores them in a "Tutti logs" Drive folder.
    void add (const std::string& kind, const std::string& text, const std::string& from, const std::string& version,
              const std::string& os, const std::string& live, const juce::String& logs = {});
    void setEndpoint (const std::string& url);
    void tick();

    SuggestionState state() const;
    std::function<void()> onChange;
    std::function<void (const std::string&)> log; // called on the sending thread

    // Passing suggestions through a friend's Tutti when this computer can't reach Google.
    std::function<bool (const tutti::Json& item)> relay;   // message thread; true: handed over
    void addRelayed (const tutti::Json& item);             // a friend's, to send on
    void relayAccepted (const std::string& id);            // a friend has ours

private:
    void save();
    void sendNext();

    juce::File file, logsDir;
    mutable std::mutex lock;
    tutti::Json queue = tutti::Json::array();
    std::string endpoint;
    std::string lastResult;
    std::atomic<bool> sending { false };
    juce::int64 nextTry = 0;
    std::unique_ptr<juce::Thread> worker;
};
