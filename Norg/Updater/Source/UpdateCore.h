#pragma once

#include <juce_core/juce_core.h>
#include <optional>

// Pure update logic: manifest parsing, URL policy and the install decision.
// No file moves or network here, so all of it is unit-testable.
//
// Trust model: manifests and downloads come only over HTTPS from this repository's norg-*
// releases (github.com's certificate proves where they came from), and every download must match
// the SHA-256 in the manifest before anything is installed.
namespace norg::update
{
    // Norg releases live only under tags starting with "norg-" in this repo, so the updater can
    // never be pointed at another program's releases.
    inline constexpr const char* manifestUrl =
        "https://github.com/dylancleverdon/Baastik-Labs/releases/download/norg-channel/norg-update.json";
    inline constexpr const char* allowedAssetPrefix =
        "https://github.com/dylancleverdon/Baastik-Labs/releases/download/norg-";

    struct Manifest
    {
        int schema = 0;
        juce::String version;
        juce::int64 build = 0;
        juce::String commit;
        juce::String notes;
        juce::String zipName;
        juce::String zipUrl;
        juce::String sha256;
        juce::String publishedAt;
    };

    // What is installed (installed.json) or shipped inside an update (release.json).
    struct ReleaseInfo
    {
        juce::String version;
        juce::int64 build = 0;
        juce::String commit;
        juce::String notes;
        juce::String date;
    };

    struct Settings
    {
        bool autoUpdate = true;
        juce::int64 skipBuild = 0; // set by a rollback so that exact build isn't reinstalled
    };

    std::optional<Manifest> parseManifest (const juce::String& json, juce::String& error);

    std::optional<ReleaseInfo> parseReleaseInfo (const juce::String& json);
    juce::String toJson (const ReleaseInfo&);

    Settings parseSettings (const juce::String& json);
    juce::String toJson (const Settings&);

    bool isAllowedAssetUrl (const juce::String& url, const juce::StringArray& extraAllowedPrefixes = {});
    bool isSafeZipName (const juce::String& name);

    std::optional<juce::MemoryBlock> fromHex (const juce::String& hex);
    juce::String toHex (const void* data, size_t size);
    juce::String sha256OfFile (const juce::File& file);

    enum class Decision { install, upToDate, skipped, disabled };

    Decision decide (const Manifest&, const std::optional<ReleaseInfo>& installed,
                     const Settings&, bool manualCheck);
}
