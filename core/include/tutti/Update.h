#pragma once

#include "Json.h"

#include <map>
#include <optional>
#include <vector>

// Update decisions (no network or files here, so all of it is testable).
//
// Trust model, as for Norg: the manifest and the payloads come only over HTTPS from this
// repository's tutti-* releases, and a payload is installed only if its SHA-256 matches the
// manifest.
namespace tutti::update
{
    inline constexpr const char* manifestUrl =
        "https://github.com/dylancleverdon/Baastik-Labs/releases/download/tutti-channel/tutti-update.json";
    inline constexpr const char* allowedPrefix =
        "https://github.com/dylancleverdon/Baastik-Labs/releases/download/tutti-";

    struct Asset
    {
        std::string name, url, sha256;
        int64_t size = 0;
    };

    struct Manifest
    {
        int schema = 0;
        std::string version;
        int64_t build = 0;
        std::string commit, notes, published;
        int protocol = 0;
        std::map<std::string, Asset> platforms; // "mac", "windows"
        std::string suggestionsEndpoint;
    };

    std::optional<Manifest> parse (const std::string& json, std::string& error);

    bool isAllowedUrl (const std::string& url, const std::vector<std::string>& extraPrefixes = {});

    // A zip entry that stays inside the folder it's extracted to.
    bool isSafeEntry (const std::string& name);

    struct Settings
    {
        bool autoUpdate = true;
        int64_t skipBuild = 0; // set by a rollback so that exact build isn't reinstalled

        Json toJson() const { return Json { { "auto", autoUpdate }, { "skip", skipBuild } }; }
        static Settings fromJson (const Json& j)
        {
            Settings s;
            s.autoUpdate = boolean (j, "auto", true);
            s.skipBuild = integer (j, "skip");
            return s;
        }
    };

    enum class Decision { install, upToDate, skipped, disabled, unavailable };

    Decision decide (const Manifest&, int64_t installedBuild, const Settings&, bool manualCheck, const std::string& platform,
                     const std::vector<std::string>& extraPrefixes = {});
}
