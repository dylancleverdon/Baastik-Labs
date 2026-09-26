#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "serum2/PresetFile.h"
#include "serum2/Schema.h"
#include "serumgen/Content.h"

namespace serumgen
{
// The parts of a patch the user can randomize or lock independently. Mod
// routes belong to the group of their source (an LFO's routes are part of
// "LFOs", a macro's to "Macros"); everything else is "Mod Matrix".
enum class Group
{
    Oscillators,
    NoiseSub,
    Filters,
    Envelopes,
    Lfos,
    ModMatrix,
    Macros,
    Fx,
    Global,
};

constexpr std::array<Group, 9> kAllGroups {
    Group::Oscillators, Group::NoiseSub, Group::Filters, Group::Envelopes, Group::Lfos,
    Group::ModMatrix,   Group::Macros,   Group::Fx,      Group::Global,
};

std::string_view groupId(Group g);    // "oscillators"
std::string_view groupLabel(Group g); // "Oscillators"
std::optional<Group> groupFromId(std::string_view id);

enum class Mode
{
    Random, // anything goes: wide base ranges, category and genre ignored
    Guided, // category + genre profiles, loosened by chaos
    Mutate, // small variations of the base preset
};

std::string_view modeId(Mode m);
std::optional<Mode> modeFromId(std::string_view id);

inline constexpr std::string_view kAny = "any";   // category/genre picked at random
inline constexpr std::string_view kNone = "none"; // no genre layer

struct Settings
{
    Mode mode = Mode::Guided;
    std::string category { kAny };
    std::string genre { kNone };
    // Forces one recipe (Guided mode). Empty = the category/genre profile picks.
    // When category is "any", the category that lists this recipe is used.
    std::string recipe;
    double chaos = 0.2; // 0 = strictly on-profile, 1 = anything the base allows
    std::uint64_t seed = 1;
    // Locked groups are copied from the base preset instead of generated.
    std::array<bool, kAllGroups.size()> locked {};
    std::string author = "Baastik Labs";

    bool isLocked(Group g) const { return locked[static_cast<std::size_t>(g)]; }
    void setLocked(Group g, bool value) { locked[static_cast<std::size_t>(g)] = value; }
};

struct Result
{
    serum2::Preset preset;
    std::string name;
    std::string category; // resolved ids ("any" becomes a real pick)
    std::string genre;
    std::string recipe;
    std::uint64_t seed = 0;
    std::vector<std::string> summary; // human-readable description, one line per part
};

class Generator
{
public:
    explicit Generator(const ContentLibrary& content = ContentLibrary::builtin(),
                       const serum2::Schema& schema = serum2::Schema::builtin());

    // base: the preset locked groups are taken from (and, in Mutate mode, the
    // preset being varied). Defaults to Serum's init patch.
    Result generate(const Settings& settings, const serum2::Preset* base = nullptr) const;

    const ContentLibrary& content() const { return content_; }

private:
    const ContentLibrary& content_;
    const serum2::Schema& schema_;
};

// Human-readable file name for a preset ("BA Wonky Growl.SerumPreset").
std::string presetFileName(const std::string& presetName);
} // namespace serumgen
