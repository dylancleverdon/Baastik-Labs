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

SuggestionBox::SuggestionBox (juce::File dataDir) : file (dataDir.getChildFile ("suggestions.json"))
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
                         const std::string& version, const std::string& os, const std::string& live)
{
    {
        std::lock_guard<std::mutex> g (lock);
        queue.push_back (tutti::Json { { "id", tutti::randomId ("sg") }, { "kind", kind }, { "text", text }, { "from", from },
                                       { "version", version }, { "os", os }, { "live", live },
                                       { "at", juce::Time::getCurrentTime().toISO8601 (true).toStdString() } });
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
    worker = std::make_unique<Job> ([this, item, url] {
        int status = 0;
        auto stream = juce::URL (juce::String (url))
                          .withPOSTData (juce::String (item.dump()))
                          .createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                                                  .withExtraHeaders ("Content-Type: text/plain;charset=utf-8")
                                                  .withConnectionTimeoutMs (20000)
                                                  .withNumRedirectsToFollow (5)
                                                  .withStatusCode (&status));
        const auto body = stream != nullptr ? stream->readEntireStreamAsString() : juce::String();
        // Apps Script answers through a redirect; any response from Google means the row is in
        // (and a retry is harmless anyway, thanks to the id).
        const bool ok = stream != nullptr && (status == 0 || (status >= 200 && status < 400) || body.contains ("\"ok\""));
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
                lastResult = "Sent, thanks!";
            }
            else
            {
                lastResult = "Couldn't send yet; Tutti will keep trying.";
                nextTry = juce::Time::currentTimeMillis() + 60 * 1000;
            }
        }
        sending = false;
        juce::MessageManager::callAsync ([this] { if (onChange) onChange(); });
    });
    worker->startThread();
}
