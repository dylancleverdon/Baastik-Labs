#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "serum2/PresetFile.h"

namespace tts
{
namespace fs = std::filesystem;

// Where Text To Synth keeps things.
struct Paths
{
    // Presets Claude makes, inside Serum 2's user folder so they show up in
    // Serum's browser: Documents/Xfer/Serum 2 Presets/Presets/User/Text To Synth.
    fs::path presets;
    // App data: version history, content updates, settings.
    // macOS: ~/Library/Application Support/Baastik Labs/Text To Synth
    // Windows: %APPDATA%\Baastik Labs\Text To Synth
    fs::path data;

    // Defaults for this machine. TEXT_TO_SYNTH_PRESETS_DIR and
    // TEXT_TO_SYNTH_DATA_DIR override them (tests, custom setups).
    static Paths defaults();
    static fs::path home();
    static fs::path documents();
    static fs::path appData(); // the per-user application data root
};

struct VersionInfo
{
    int version = 0;
    std::string note;
    std::string time; // ISO 8601, UTC
};

// Presets in the output folder plus a linear history of every version, so
// "go back to before it got harsh" always works. Presets outside the folder
// (factory, other packs) are never modified: editing one saves a copy here.
class PresetStore
{
public:
    explicit PresetStore(Paths paths);

    const Paths& paths() const { return paths_; }

    // A path, a file name, or a preset name ("Dark Reese"). Throws
    // std::invalid_argument listing what exists when nothing matches.
    fs::path resolve(const std::string& nameOrPath) const;
    bool owns(const fs::path& file) const;

    // Saves a new preset as version 1 under a unique file name. The preset's
    // name is updated to match if the file name had to change.
    fs::path create(serum2::Preset preset, const std::string& note);
    // Writes the next version of an owned preset. If the file changed on disk
    // since our last version (tweaked and saved in Serum), that state is
    // recorded as its own version first.
    int commit(const fs::path& file, const serum2::Preset& preset, const std::string& note);
    // Makes an old version current again (as a new version).
    int restore(const fs::path& file, int version);

    std::vector<VersionInfo> versions(const fs::path& file) const;
    serum2::Preset load(const fs::path& file) const;
    std::vector<fs::path> list() const;

private:
    fs::path historyDir(const fs::path& file) const;
    fs::path versionFile(const fs::path& file, int version) const;
    int record(const fs::path& file, const serum2::Bytes& bytes, const std::string& note);

    Paths paths_;
};

std::string presetNameOf(const serum2::Preset& preset);
void setPresetName(serum2::Preset& preset, const std::string& name);
} // namespace tts
