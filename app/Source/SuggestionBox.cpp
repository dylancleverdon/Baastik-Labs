#include "SuggestionBox.h"

#include <juce_events/juce_events.h>

#include "tutti/Ids.h"

namespace
{
    class Job : public juce::Thread
    {
    public:
        explicit Job (std::function<void()> fn) : juce::Thread ("Tutti suggestions"), body (std::move (fn)) {}
        void run() override { body(); }

    private:
        std::function<void()> body;
    };
}

SuggestionBox::SuggestionBox (juce::File dataDir)
    : file (dataDir.getChildFile ("suggestions.json")), logsDir (dataDir.getChildFile ("suggestion-logs"))
{
    if (auto j = tutti::parseJson (file.loadFileAsString().toStdString()); j && j->is_array())
        queue = *j;
}

SuggestionBox::~SuggestionBox()
{
    if (worker != nullptr)
        worker->stopThread (5000);
}

void SuggestionBox::save()
{
    file.replaceWithText (juce::String (queue.dump (2)));
}

void SuggestionBox::add (const std::string& kind, const std::string& text, const std::string& from,
                         const std::string& version, const std::string& os, const std::string& live, const juce::String& logs)
{
    {
        std::lock_guard<std::mutex> g (lock);
        const auto id = tutti::randomId ("sg");
        tutti::Json item { { "id", id }, { "kind", kind }, { "text", text }, { "from", from },
                           { "version", version }, { "os", os }, { "live", live },
                           { "at", juce::Time::getCurrentTime().toISO8601 (true).toStdString() } };
        if (logs.isNotEmpty())
        {
            logsDir.createDirectory();
            auto f = logsDir.getChildFile (juce::String (id) + ".txt");
            if (f.replaceWithText (logs, false, false, "\n"))
                item["logFile"] = f.getFileName().toStdString();
        }
        queue.push_back (item);
        save();
        lastResult = endpoint.empty() ? "Saved." : "Sending...";
        nextTry = 0;
    }
    if (onChange)
        onChange();
}

void SuggestionBox::setEndpoint (const std::string& url)
{
    std::lock_guard<std::mutex> g (lock);
    if (url != endpoint && url.rfind ("https://", 0) == 0)
    {
        endpoint = url;
        nextTry = 0;
    }
}

SuggestionState SuggestionBox::state() const
{
    std::lock_guard<std::mutex> g (lock);
    return { static_cast<int> (queue.size()), ! endpoint.empty(), lastResult };
}

void SuggestionBox::tick()
{
    {
        std::lock_guard<std::mutex> g (lock);
        if (queue.empty() || endpoint.empty() || sending || juce::Time::currentTimeMillis() < nextTry)
            return;
    }
    sendNext();
}

void SuggestionBox::sendNext()
{
    if (sending.exchange (true))
        return;
    tutti::Json item;
    std::string url;
    {
        std::lock_guard<std::mutex> g (lock);
        item = queue.front();
        url = endpoint;
    }
    if (worker != nullptr)
        worker->stopThread (1000);
    auto post = item;
    juce::File logFile;
    if (const auto name = item.value ("logFile", std::string()); ! name.empty())
    {
        logFile = logsDir.getChildFile (juce::String (name));
        post.erase ("logFile");
        // After several failed tries with the logs attached, send the words without them, so
        // the suggestion itself always gets through (the logs are kept for the next try).
        if (logFile.existsAsFile() && item.value ("tries", 0) < 6)
            post["log"] = logFile.loadFileAsString().toStdString();
    }
    post.erase ("tries");
    worker = std::make_unique<Job> ([this, item, url, post, logFile] {
        // Post once, without following redirects. Apps Script saves the row, then answers with a
        // redirect to where its reply waits; that reply must be fetched with GET. (On Windows,
        // following the redirect re-sent the POST there, Google refused it, and suggestions never
        // counted as sent.)
        int status = 0;
        juce::StringPairArray headers;
        auto stream = juce::URL (juce::String (url))
                          .withPOSTData (juce::String (post.dump (-1, ' ', false, tutti::Json::error_handler_t::replace)))
                          .createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                                                  .withExtraHeaders ("Content-Type: text/plain;charset=utf-8")
                                                  .withConnectionTimeoutMs (30000)
                                                  .withNumRedirectsToFollow (0)
                                                  .withResponseHeaders (&headers)
                                                  .withStatusCode (&status));
        auto body = stream != nullptr ? stream->readEntireStreamAsString() : juce::String();
        juce::String location;
        for (const auto& key : headers.getAllKeys())
            if (key.equalsIgnoreCase ("Location"))
                location = headers[key].upToFirstOccurrenceOf (",", false, false).trim();
        const bool redirected = status >= 300 && status < 400 && location.isNotEmpty();
        if (redirected)
        {
            int replyStatus = 0;
            if (auto reply = juce::URL (location).createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                                                          .withConnectionTimeoutMs (20000)
                                                                          .withNumRedirectsToFollow (5)
                                                                          .withStatusCode (&replyStatus)))
                body = reply->readEntireStreamAsString();
        }
        // The redirect means the script ran and kept the row; a direct answer must say ok. A retry
        // is harmless anyway, thanks to the id.
        const bool ok = stream != nullptr && (redirected || body.contains ("\"ok\":true") || ((status == 0 || status == 200) && body.contains ("\"ok\"")));
        if (log)
            log ("suggestion " + item.value ("id", std::string()) + (logFile != juce::File() ? " (with logs)" : "") + ": "
                 + (stream == nullptr ? std::string ("couldn't reach Google")
                                      : "HTTP " + std::to_string (status) + (redirected ? " (redirect)" : "") + ", reply: "
                                            + body.substring (0, 200).replaceCharacters ("\r\n", "  ").toStdString())
                 + (ok ? "" : " - will retry"));
        {
            std::lock_guard<std::mutex> g (lock);
            if (ok)
            {
                for (size_t i = 0; i < queue.size(); ++i)
                    if (queue[i]["id"] == item["id"])
                    {
                        queue.erase (i);
                        break;
                    }
                save();
                if (logFile != juce::File() && ! post.contains ("log"))
                {
                    logFile.deleteFile();
                    lastResult = "Sent, thanks! (Your logs couldn't be attached.)";
                }
                else if (logFile != juce::File())
                {
                    logFile.deleteFile();
                    // The current sheet script answers {"ok":true,"log":"<link>"} when it kept the
                    // logs; an older one ignores them.
                    lastResult = body.contains ("\"log\"") || body.isEmpty() ? "Sent with your logs, thanks!"
                                                                             : "Sent, thanks! (The suggestion sheet's script needs updating to keep logs.)";
                }
                else
                    lastResult = "Sent, thanks!";
            }
            else
            {
                lastResult = "Couldn't send yet; Tutti will keep trying.";
                // Move it behind the others, so one that keeps failing never holds the rest up.
                for (size_t i = 0; i < queue.size(); ++i)
                    if (queue[i]["id"] == item["id"])
                    {
                        auto failed = queue[i];
                        failed["tries"] = failed.value ("tries", 0) + 1;
                        queue.erase (i);
                        queue.push_back (failed);
                        break;
                    }
                save();
                nextTry = juce::Time::currentTimeMillis() + (queue.size() > 1 ? 15 : 60) * 1000;
                // Failed twice: hand it (logs included) to a friend's Tutti, which sends it on.
                if (item.value ("tries", 0) >= 1)
                {
                    auto handOver = post;
                    handOver["id"] = item["id"];
                    juce::MessageManager::callAsync ([this, handOver] {
                        if (relay && relay (handOver))
                        {
                            std::lock_guard<std::mutex> g (lock);
                            lastResult = "Couldn't reach Google: passing it to a friend's Tutti...";
                        }
                    });
                }
            }
        }
        sending = false;
        juce::MessageManager::callAsync ([this] { if (onChange) onChange(); });
    });
    worker->startThread();
}

void SuggestionBox::addRelayed (const tutti::Json& item)
{
    {
        std::lock_guard<std::mutex> g (lock);
        const auto id = item.value ("id", std::string());
        for (const auto& q : queue)
            if (q.value ("id", std::string()) == id)
                return; // already have it
        auto keep = item;
        if (keep.contains ("log") && keep["log"].is_string())
        {
            logsDir.createDirectory();
            auto f = logsDir.getChildFile (juce::String (id) + ".txt");
            if (f.replaceWithText (juce::String (keep["log"].get<std::string>()), false, false, "\n"))
                keep["logFile"] = f.getFileName().toStdString();
        }
        keep.erase ("log");
        keep.erase ("tries");
        queue.push_back (keep);
        save();
        nextTry = 0;
    }
    if (onChange)
        onChange();
}

void SuggestionBox::relayAccepted (const std::string& id)
{
    {
        std::lock_guard<std::mutex> g (lock);
        for (size_t i = 0; i < queue.size(); ++i)
            if (queue[i].value ("id", std::string()) == id)
            {
                if (const auto name = queue[i].value ("logFile", std::string()); ! name.empty())
                    logsDir.getChildFile (juce::String (name)).deleteFile();
                queue.erase (i);
                break;
            }
        save();
        lastResult = "Sent through a friend's Tutti, thanks!";
    }
    if (onChange)
        onChange();
}
