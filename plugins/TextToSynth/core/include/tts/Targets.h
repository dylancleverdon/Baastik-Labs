#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "serum2/Patch.h"
#include "serum2/Schema.h"

namespace tts
{
using serum2::Json;

// A parameter addressed the way a person reads Serum's UI: "oscA.level",
// "filter1.cutoff", "env1.release", "lfo2.rate", "macro1.value", "global.glide",
// "fx.reverb.mix". Slots are 1-based like the UI; oscillators are A/B/C plus
// "noise" and "sub".
struct Target
{
    enum class Kind
    {
        Module, // a kParam inside a module's plainParams
        Fx,     // a kParam of an FX unit in one of the racks
    };

    Kind kind = Kind::Module;
    serum2::Module module;     // Kind::Module
    std::string fxType;        // Kind::Fx, schema name ("FXReverb")
    int fxInstance = 0;        // Kind::Fx, 0 = first unit of that type
    std::string key;           // "kParamFreq"
    const serum2::ParamDef* def = nullptr;
    std::string label;         // canonical friendly name ("filter1.cutoff")
    std::string owner;         // "filter1", "oscA", "fx.reverb"
};

// Resolves friendly names. Throws std::invalid_argument with a helpful
// message (including suggestions) when the name doesn't resolve.
class TargetResolver
{
public:
    explicit TargetResolver(const serum2::Schema& schema = serum2::Schema::builtin());

    Target resolve(std::string_view name) const;

    // "reverb" / "Reverb" / "FXReverb" -> "FXReverb".
    std::optional<std::string> fxType(std::string_view name) const;
    // "FXReverb" -> "reverb".
    static std::string fxFriendly(std::string_view fxType);

    // Friendly owner name for a mod matrix destination or an FX unit.
    std::string ownerLabel(const std::string& moduleType, int id) const;
    // Friendly parameter name for a kParam in a schema module ("kParamFreq" -> "cutoff").
    std::string paramLabel(std::string_view schemaModule, std::string_view key) const;

    // Mod sources: "lfo1", "env2", "macro3", "velocity", "modwheel", ...
    std::optional<int> modSource(std::string_view name) const;
    std::string modSourceLabel(int id) const;

    // Destination for a mod route to target, when Serum can modulate it.
    // FX targets need the unit's rack and position.
    std::optional<serum2::ModDest> modDest(const Target& t, int fxRack = 0, int fxPosition = 0) const;

    // One entry per owner ("oscA", "filter1", "fx.reverb", ...): its
    // parameters with kind, unit, range and enum values. For the guide.
    Json reference() const;

    const serum2::Schema& schema() const { return schema_; }

private:
    struct Owner
    {
        std::string label;                     // "filter1"
        std::vector<serum2::Module> modules;   // candidate modules, in lookup order
    };

    std::optional<Owner> owner(std::string_view name) const;
    std::optional<Target> findParam(const Owner& o, std::string_view param) const;
    std::optional<Target> findFxParam(const std::string& fxType, int instance, std::string_view param) const;

    const serum2::Schema& schema_;
    Json rawModDests_;
};

// "Filter 1" / "filter_1" / "FILTER1" -> "filter1".
std::string normalize(std::string_view text);
} // namespace tts
