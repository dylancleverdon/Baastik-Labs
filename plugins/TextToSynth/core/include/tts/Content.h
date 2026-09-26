#pragma once

#include <filesystem>

#include "serum2/PresetFile.h"

namespace tts
{
// The sound-design vocabulary (words -> parameter moves). The built-in copy is
// compiled in; the updater can drop a newer bundle into the data folder, which
// wins when it is valid and newer. That's how vocabulary tweaks ship without
// a reinstall.
class Content
{
public:
    // Bump when the server starts relying on new bundle fields. Bundles that
    // declare a higher minEngine are ignored.
    static constexpr int kEngineVersion = 1;

    static const serum2::Json& builtin();
    static bool isValid(const serum2::Json& bundle);
    // The built-in bundle, or the cached download when it is newer and valid.
    static serum2::Json load(const std::filesystem::path& cacheFile);
    static std::filesystem::path cacheFile(const std::filesystem::path& dataDir);
};
} // namespace tts
