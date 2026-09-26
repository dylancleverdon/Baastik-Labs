#pragma once

#include <cstdint>
#include <string>

// The exact bytes an update signature covers. Shared by norg-sign (CI) and NorgUpdater,
// so both sides always agree. Any change here must bump the "v1" tag.
namespace norg::update
{
    inline std::string canonicalMessage (const std::string& version,
                                         std::int64_t build,
                                         const std::string& zipName,
                                         const std::string& zipUrl,
                                         const std::string& sha256Hex)
    {
        return "norg-update-v1\n"
               "version=" + version + "\n"
               "build=" + std::to_string (build) + "\n"
               "zip=" + zipName + "\n"
               "url=" + zipUrl + "\n"
               "sha256=" + sha256Hex + "\n";
    }
}
