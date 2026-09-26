#pragma once

#include <cstdlib>
#include <filesystem>
#include <vector>

// Serum-saved presets to test against: tests/fixtures/*.SerumPreset plus any
// directory listed in the SERUM_FIXTURES environment variable. Presets from
// commercial packs can be tested locally this way without committing them.
// Subfolders are searched too, so a whole preset library can be pointed at.
inline std::vector<std::filesystem::path> fixturePresets()
{
    std::vector<std::filesystem::path> dirs { BAASTIK_FIXTURES_DIR };
    if (const char* extra = std::getenv("SERUM_FIXTURES"))
        dirs.emplace_back(extra);

    std::vector<std::filesystem::path> presets;
    for (const auto& dir : dirs)
    {
        std::error_code ec;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(dir, ec))
            if (entry.path().extension() == ".SerumPreset")
                presets.push_back(entry.path());
    }
    return presets;
}
