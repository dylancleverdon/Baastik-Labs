#pragma once

#include "Updater.h"

#include <vector>

// Sample libraries (packs) that Norg downloads and prepares by itself after it is installed, so the
// sampled instruments just work. The list lives in packs.json on this repository's norg-samples
// release; each pack is a .tar.gz checked against its SHA-256 before it is used.
namespace norg::update
{
    inline constexpr const char* packsUrl =
        "https://github.com/dylancleverdon/Baastik-Labs/releases/download/norg-samples/packs.json";

    struct SamplePack
    {
        juce::String id, name, folder, url, sha256, license;
        int version = 0;
        juce::int64 size = 0;
    };

    std::vector<SamplePack> parsePacks (const juce::String& json, juce::String& error);

    // What the plugin shows while a pack is on its way.
    struct PackStatus
    {
        juce::String id, name;
        juce::String state; // "downloading", "preparing", "ready", "failed"
        float progress = 0.0f;
    };

    juce::String toJson (const std::vector<PackStatus>&);
    std::vector<PackStatus> parsePackStatus (const juce::String& json);

    class PackInstaller
    {
    public:
        PackInstaller (Layout, Platform&, FileOps&, Updater::LogFn);

        // Installs every pack that's missing (and, with allowUpgrades, every newer version).
        // Returns true when all packs are installed and ready.
        bool installAll (const juce::String& listUrl, const juce::StringArray& extraAllowedPrefixes, bool allowUpgrades);

        bool isInstalled (const SamplePack&) const;

    private:
        bool install (const SamplePack&, const juce::StringArray& extraAllowedPrefixes);
        void setStatus (const SamplePack&, const juce::String& state, float progress);
        void markInstalled (const SamplePack&);

        Layout layout;
        Platform& platform;
        FileOps& fileOps;
        Updater::LogFn log;
        std::vector<PackStatus> statuses;
        juce::uint32 lastStatusWrite = 0;
    };
}
