#include "ContentManager.h"

#include <juce_events/juce_events.h>

ContentManager::ContentManager()
{
    current_ = std::shared_ptr<const serumgen::ContentLibrary> (&serumgen::ContentLibrary::builtin(), [] (auto*) {});
    const auto cached = cacheFile();
    if (cached.existsAsFile())
        install (cached.loadFileAsString());
}

juce::File ContentManager::cacheFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
#if JUCE_MAC
        .getChildFile ("Application Support")
#endif
        .getChildFile ("Baastik Labs")
        .getChildFile ("Serum Preset Generator")
        .getChildFile ("content.json");
}

bool ContentManager::install (const juce::String& bundleText)
{
    try
    {
        auto library = serumgen::ContentLibrary::fromBundle (serum2::Json::parse (bundleText.toStdString()));
        if (library.version() <= current_->version())
            return false;
        current_ = std::make_shared<const serumgen::ContentLibrary> (std::move (library));
        downloaded_ = true;
        return true;
    }
    catch (const std::exception& e)
    {
        DBG ("Ignoring content bundle: " << e.what());
        return false;
    }
}

void ContentManager::updateFromManifest (const juce::var& manifest, std::function<void()> onUpdated)
{
    const auto content = manifest.getProperty ("content", {});
    const int remoteVersion = content.getProperty ("version", 0);
    const juce::URL url (content.getProperty ("url", {}).toString());
    if (remoteVersion <= version() || url.isEmpty())
        return;

    auto alive = alive_;
    juce::Thread::launch ([this, alive, url, onUpdated = std::move (onUpdated)] {
        const auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                 .withConnectionTimeoutMs (15000)
                                 .withNumRedirectsToFollow (5);
        auto stream = url.createInputStream (options);
        if (stream == nullptr)
            return;
        const auto text = stream->readEntireStreamAsString();
        juce::MessageManager::callAsync ([this, alive, text, onUpdated] {
            if (! alive->load() || ! install (text))
                return;
            const auto file = cacheFile();
            file.getParentDirectory().createDirectory();
            file.replaceWithText (text);
            if (onUpdated)
                onUpdated();
        });
    });
}
