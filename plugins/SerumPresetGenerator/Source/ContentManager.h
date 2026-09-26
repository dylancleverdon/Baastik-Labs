#pragma once

#include <juce_core/juce_core.h>

#include <memory>

#include "serumgen/Content.h"

// Owns the generator content in use: the version compiled into the plugin, or
// a newer bundle downloaded from this plugin's release channel. Content
// updates (new genres, recipe tweaks) arrive this way without reinstalling.
class ContentManager
{
public:
    ContentManager();

    std::shared_ptr<const serumgen::ContentLibrary> current() const { return current_; }
    int version() const { return current_->version(); }
    bool isDownloaded() const { return downloaded_; }

    // Installs a bundle (already fetched) if it is newer and valid. Returns
    // true when the active content changed.
    bool install (const juce::String& bundleText);

    // manifest["content"] = { "version": n, "url": "..." }: fetches the bundle
    // on a background thread when it is newer than what we have, then calls
    // onUpdated on the message thread.
    void updateFromManifest (const juce::var& manifest, std::function<void()> onUpdated);

private:
    static juce::File cacheFile();

    std::shared_ptr<const serumgen::ContentLibrary> current_;
    bool downloaded_ = false;
    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>> (true);

public:
    ~ContentManager() { alive_->store (false); }
};
