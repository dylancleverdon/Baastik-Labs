#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "serum2/PresetFile.h"
#include "serumgen/Random.h"

namespace serumgen
{
using serum2::Json;

// One generator setting from a profile file. In JSON:
//   0.7                        fixed value (or a probability, for chance knobs)
//   [0.2, 0.9]                 uniform range
//   {"range": [a, b], "log": true}   log-uniform range (times, rates)
//   {"choose": {"a": 3, "b": 1}}     weighted choice
struct KnobSpec
{
    enum class Kind
    {
        Fixed,
        Range,
        Choice,
    };

    Kind kind = Kind::Fixed;
    double value = 0.0;
    double lo = 0.0;
    double hi = 0.0;
    bool log = false;
    std::vector<std::pair<std::string, double>> choices;

    static KnobSpec parse(const Json& j);
};

// The knobs in force for one generation: profile layers merged in order
// (base, genre, category, genre-per-category, recipe), later layers winning.
// Chaos blends every knob back toward the base layer's wide defaults.
//
// Lookups fall back from a slot-specific name to the generic one, so
// "osc1.octave" uses "osc.octave" unless a layer sets osc1 explicitly.
class KnobSet
{
public:
    KnobSet() = default;
    explicit KnobSet(const Json& base);

    void applyLayer(const Json& knobs);
    void setChaos(double chaos) { chaos_ = chaos; }
    double chaos() const { return chaos_; }

    bool has(std::string_view name) const;
    double real(std::string_view name, Rng& rng, double fallback = 0.0) const;
    int integer(std::string_view name, Rng& rng, int fallback = 0) const;
    bool chance(std::string_view name, Rng& rng, double fallback = 0.0) const;
    std::string pick(std::string_view name, Rng& rng, std::string_view fallback = {}) const;
    // Weighted choices after chaos blending, for picking without replacement.
    std::vector<std::pair<std::string, double>> choices(std::string_view name) const;

    // The effective spec for a knob, or nullopt.
    std::optional<KnobSpec> effective(std::string_view name) const;

private:
    const KnobSpec* lookup(const std::map<std::string, KnobSpec, std::less<>>& table, std::string_view name) const;

    std::map<std::string, KnobSpec, std::less<>> base_;
    std::map<std::string, KnobSpec, std::less<>> layered_;
    double chaos_ = 0.0;
};

// "osc1.octave" -> "osc.octave"; returns empty when there is no slot index.
std::string genericKnobName(std::string_view name);
} // namespace serumgen
