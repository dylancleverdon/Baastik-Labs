#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "serum2/PresetFile.h"

namespace serum2
{
// One kParam* field inside a module's plainParams.
struct ParamDef
{
    enum class Kind
    {
        Float,
        Bool,
        Enum,
    };

    Kind kind = Kind::Float;
    std::optional<Json> defaultValue; // absent-key value; nullopt means "always write"
    std::optional<double> min;
    std::optional<double> max;
    std::vector<std::string> enumValues;
    std::map<int, Json> slotDefaults; // per-slot overrides (Osc A is on by default)

    std::optional<Json> defaultFor(int slot) const;
    double clamp(double value) const;
};

using ParamTable = std::map<std::string, ParamDef, std::less<>>;

// A mod matrix destination as Serum stores it in a ModSlot.
struct ModDest
{
    std::string type; // destModuleTypeString, e.g. "VoiceFilter"
    int id = 0;       // destModuleID (slot index, or rack * 100 + position for FX)
    std::string paramName;
    int paramId = 0;
};

struct Wavetable
{
    std::string id;
    std::string path;
    int numFrames = 0;
    int sampleRate = 44100;
    int numChannels = 1;
    std::vector<std::string> tags;
    std::map<std::string, double, std::less<>> knownFrames; // e.g. "sine" -> 32.875

    bool hasTag(std::string_view tag) const;
};

// A factory multisample instrument, referenced by its SFZ keymap.
struct MultiSample
{
    std::string id;
    Json container; // sfzPathRelative, embedded_sfz, files
    std::vector<std::string> tags;

    bool hasTag(std::string_view tag) const;
};

class Schema
{
public:
    static const Schema& builtin();
    static Schema fromJson(const Json& schema, const Json& multisamples);

    // A fresh Serum 2 init patch (metadata + data).
    static Preset initPreset();

    const ParamTable* module(std::string_view name) const;
    const ParamDef* param(std::string_view module, std::string_view key) const;

    const ParamTable* fxParams(std::string_view fxType) const;
    std::optional<int> fxTypeId(std::string_view fxType) const;
    std::optional<std::string> fxTypeName(int typeId) const;
    std::vector<std::string> fxTypes() const;

    std::optional<int> modSource(std::string_view name) const;
    std::optional<std::string> modSourceName(int id) const;
    std::optional<ModDest> modDest(std::string_view name) const;
    // Destination for a parameter of the FX unit at rack/position.
    std::optional<ModDest> fxModDest(std::string_view fxType, int rack, int position,
                                     std::string_view param) const;

    const std::vector<Wavetable>& wavetables() const { return wavetables_; }
    const Wavetable* wavetable(std::string_view id) const;
    const std::vector<MultiSample>& multisamples() const { return multisamples_; }
    const MultiSample* multisample(std::string_view id) const;

    // Beat-synced LFO rate name ("1/8") -> raw kParamRate.
    const std::map<std::string, double, std::less<>>& lfoSyncRates() const { return lfoSyncRates_; }

private:
    std::map<std::string, ParamTable, std::less<>> modules_;
    std::map<std::string, ParamTable, std::less<>> fx_;
    std::map<std::string, int, std::less<>> fxTypeIds_;
    std::map<std::string, int, std::less<>> modSources_;
    std::map<std::string, ModDest, std::less<>> modDests_;
    // fx type -> short name ("width") -> (param name, param id)
    std::map<std::string, std::map<std::string, std::pair<std::string, int>, std::less<>>, std::less<>> fxExtraDests_;
    int fxWetParamId_ = 1;
    std::vector<Wavetable> wavetables_;
    std::vector<MultiSample> multisamples_;
    std::map<std::string, double, std::less<>> lfoSyncRates_;
};
} // namespace serum2
