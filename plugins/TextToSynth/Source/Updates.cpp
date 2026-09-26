#include "Updates.h"

#include <chrono>
#include <fstream>
#include <future>

#include "tts/Content.h"

namespace
{
// Starts async work on the message thread and waits (on the calling thread)
// for its result, or returns fallback after the timeout.
template <typename T>
T awaitOnMessageThread (std::function<void (std::function<void (T)>)> start, T fallback, int seconds)
{
    auto promise = std::make_shared<std::promise<T>>();
    auto future = promise->get_future();
    auto settled = std::make_shared<std::atomic<bool>> (false);
    juce::MessageManager::callAsync ([start = std::move (start), promise, settled] {
        start ([promise, settled] (T value) {
            if (! settled->exchange (true))
                promise->set_value (std::move (value));
        });
    });
    if (future.wait_for (std::chrono::seconds (seconds)) != std::future_status::ready)
        return fallback;
    return future.get();
}

std::string describeRelease (const baastik::ReleaseInfo& r)
{
    std::string text = "Text To Synth " + r.version.toString().toStdString() + " is available";
    if (r.notes.isNotEmpty())
        text += " (" + r.notes.toStdString() + ")";
    return text;
}
} // namespace

JuceUpdates::JuceUpdates (const juce::String& version, const juce::URL& manifestUrl, std::filesystem::path dataDir)
    : checker_ ({ "text-to-synth", "Text To Synth", baastik::Version::parse (version), manifestUrl }),
      dataDir_ (std::move (dataDir))
{
}

void JuceUpdates::start()
{
    checker_.checkIfDue ([this] (auto update, auto manifest) { handleCheck (std::move (update), manifest); });
}

void JuceUpdates::handleCheck (std::optional<baastik::ReleaseInfo> update, const juce::var& manifest)
{
    {
        std::lock_guard lock (mutex_);
        pending_ = std::move (update);
    }
    updateContent (manifest);
}

void JuceUpdates::updateContent (const juce::var& manifest)
{
    // manifest["content"] = { "version": n, "url": "..." }
    const auto content = manifest.getProperty ("content", {});
    const int remote = content.getProperty ("version", 0);
    const juce::URL url (content.getProperty ("url", {}).toString());
    const auto cache = tts::Content::cacheFile (dataDir_);
    const int current = tts::Content::load (cache).value ("version", 0);
    if (remote <= current || url.isEmpty())
        return;

    juce::Thread::launch ([url, cache, current] {
        const auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                 .withConnectionTimeoutMs (15000)
                                 .withNumRedirectsToFollow (5);
        auto stream = url.createInputStream (options);
        if (stream == nullptr)
            return;
        const auto text = stream->readEntireStreamAsString().toStdString();
        try
        {
            const auto bundle = serum2::Json::parse (text);
            if (! tts::Content::isValid (bundle) || bundle["version"].get<int>() <= current)
                return;
            std::filesystem::create_directories (cache.parent_path());
            const auto tmp = std::filesystem::path (cache).concat (".download");
            std::ofstream (tmp, std::ios::binary | std::ios::trunc) << text;
            std::filesystem::rename (tmp, cache);
        }
        catch (const std::exception&)
        {
            // Bad download: keep what we have and try again next launch.
        }
    });
}

std::string JuceUpdates::notice()
{
    std::lock_guard lock (mutex_);
    if (! pending_ || installing_)
        return {};
    return describeRelease (*pending_) + ". Tell the user; if they want it, call install_update.";
}

std::string JuceUpdates::checkNow()
{
    return awaitOnMessageThread<std::string> (
        [this] (std::function<void (std::string)> done) {
            checker_.checkNow ([this, done] (auto update, auto manifest) {
                const bool reached = manifest.isObject();
                handleCheck (update, manifest);
                if (update)
                    done (describeRelease (*update) + ". Call install_update if the user wants it.");
                else if (reached)
                    done ("Text To Synth is up to date (" + checker_.config().currentVersion.toString().toStdString() + ").");
                else
                    done ("Couldn't reach the update server. Check the internet connection and try again later.");
            });
        },
        "The update check timed out. Try again in a minute.",
        30);
}

std::string JuceUpdates::installUpdate()
{
    std::optional<baastik::ReleaseInfo> release;
    {
        std::lock_guard lock (mutex_);
        release = pending_;
    }
    if (! release)
    {
        const auto status = checkNow();
        std::lock_guard lock (mutex_);
        if (! pending_)
            return status;
        release = pending_;
    }

    {
        std::lock_guard lock (mutex_);
        installing_ = true;
    }
    const auto result = awaitOnMessageThread<std::string> (
        [this, release = *release] (std::function<void (std::string)> done) {
            checker_.downloadAndInstall (
                release, [] (float) {},
                [done] (bool ok, juce::String message) {
                    done (ok ? std::string ("The Text To Synth installer is open. Once it finishes, restart Claude to "
                                            "load the new version.")
                             : message.toStdString());
                });
        },
        "The download is taking a while; the installer will open when it finishes.",
        300);
    {
        std::lock_guard lock (mutex_);
        installing_ = false;
    }
    return result;
}
