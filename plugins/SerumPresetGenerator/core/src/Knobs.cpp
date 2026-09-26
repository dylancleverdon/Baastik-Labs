#include "serumgen/Knobs.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <stdexcept>

namespace serumgen
{
namespace
{
double lerp(double a, double b, double t)
{
    return a + (b - a) * t;
}

double parseNumber(const std::string& s, double fallback)
{
    try
    {
        std::size_t used = 0;
        const double v = std::stod(s, &used);
        return used == s.size() ? v : fallback;
    }
    catch (...)
    {
        return fallback;
    }
}

std::vector<std::pair<std::string, double>> normalized(const std::vector<std::pair<std::string, double>>& choices)
{
    double total = 0.0;
    for (const auto& c : choices)
        total += std::max(c.second, 0.0);
    std::vector<std::pair<std::string, double>> out;
    for (const auto& c : choices)
        out.emplace_back(c.first, total > 0.0 ? std::max(c.second, 0.0) / total : 0.0);
    return out;
}
} // namespace

KnobSpec KnobSpec::parse(const Json& j)
{
    KnobSpec spec;
    if (j.is_number() || j.is_boolean())
    {
        spec.kind = Kind::Fixed;
        spec.value = j.is_boolean() ? (j.get<bool>() ? 1.0 : 0.0) : j.get<double>();
    }
    else if (j.is_array() && j.size() == 2)
    {
        spec.kind = Kind::Range;
        spec.lo = j[0].get<double>();
        spec.hi = j[1].get<double>();
    }
    else if (j.is_object() && j.contains("range"))
    {
        spec.kind = Kind::Range;
        spec.lo = j["range"][0].get<double>();
        spec.hi = j["range"][1].get<double>();
        spec.log = j.value("log", false);
    }
    else if (j.is_object() && j.contains("choose"))
    {
        spec.kind = Kind::Choice;
        for (const auto& [key, weight] : j["choose"].items())
            spec.choices.emplace_back(key, weight.get<double>());
    }
    else if (j.is_string())
    {
        spec.kind = Kind::Choice;
        spec.choices.emplace_back(j.get<std::string>(), 1.0);
    }
    else
    {
        throw std::invalid_argument("unrecognized knob value: " + j.dump());
    }
    return spec;
}

std::string genericKnobName(std::string_view name)
{
    const auto dot = name.find('.');
    if (dot == std::string_view::npos || dot == 0)
        return {};
    std::size_t digits = dot;
    while (digits > 0 && std::isdigit(static_cast<unsigned char>(name[digits - 1])))
        --digits;
    if (digits == dot || digits == 0)
        return {};
    return std::string(name.substr(0, digits)) + std::string(name.substr(dot));
}

KnobSet::KnobSet(const Json& base)
{
    for (const auto& [name, value] : base.items())
        base_[name] = KnobSpec::parse(value);
}

void KnobSet::applyLayer(const Json& knobs)
{
    for (const auto& [name, value] : knobs.items())
    {
        auto spec = KnobSpec::parse(value);
        // A layer's plain [a, b] inherits log scaling from the base knob.
        if (spec.kind == KnobSpec::Kind::Range && !(value.is_object() && value.contains("log")))
            if (const auto* b = lookup(base_, name); b && b->kind == KnobSpec::Kind::Range)
                spec.log = b->log;
        layered_[name] = std::move(spec);
    }
}

const KnobSpec* KnobSet::lookup(const std::map<std::string, KnobSpec, std::less<>>& table, std::string_view name) const
{
    if (const auto it = table.find(name); it != table.end())
        return &it->second;
    const auto generic = genericKnobName(name);
    if (!generic.empty())
        if (const auto it = table.find(generic); it != table.end())
            return &it->second;
    return nullptr;
}

bool KnobSet::has(std::string_view name) const
{
    return lookup(layered_, name) || lookup(base_, name);
}

std::optional<KnobSpec> KnobSet::effective(std::string_view name) const
{
    // Precedence: slot-specific layered, generic layered, slot-specific base,
    // generic base.
    const KnobSpec* layered = lookup(layered_, name);
    const KnobSpec* baseSpec = lookup(base_, name);
    if (!layered)
        return baseSpec ? std::optional<KnobSpec>(*baseSpec) : std::nullopt;
    if (!baseSpec || chaos_ <= 0.0)
        return *layered;

    // Blend toward the base knob. Ranges widen linearly with chaos (in log
    // space for log knobs); choices and probabilities follow chaos squared, so
    // low chaos nudges values without swapping in off-profile options.
    KnobSpec out = *layered;
    const auto& b = *baseSpec;
    const double discrete = chaos_ * chaos_;
    if (out.kind == KnobSpec::Kind::Choice && b.kind == KnobSpec::Kind::Choice)
    {
        auto mine = normalized(out.choices);
        const auto theirs = normalized(b.choices);
        std::map<std::string, double> blended;
        for (const auto& [k, w] : mine)
            blended[k] += (1.0 - discrete) * w;
        for (const auto& [k, w] : theirs)
            blended[k] += discrete * w;
        out.choices.clear();
        // Keep the layer's order first so picks stay stable across chaos values.
        for (const auto& [k, w] : mine)
            out.choices.emplace_back(k, blended[k]);
        for (const auto& [k, w] : theirs)
            if (std::none_of(mine.begin(), mine.end(), [&](const auto& c) { return c.first == k; }))
                out.choices.emplace_back(k, blended[k]);
        return out;
    }
    const auto lowOf = [](const KnobSpec& s) { return s.kind == KnobSpec::Kind::Range ? s.lo : s.value; };
    const auto highOf = [](const KnobSpec& s) { return s.kind == KnobSpec::Kind::Range ? s.hi : s.value; };
    if (out.kind != KnobSpec::Kind::Choice && b.kind != KnobSpec::Kind::Choice)
    {
        if (out.kind == KnobSpec::Kind::Fixed && b.kind == KnobSpec::Kind::Fixed)
        {
            out.value = lerp(out.value, b.value, discrete);
            return out;
        }
        out.kind = KnobSpec::Kind::Range;
        out.log = out.log || b.log;
        const auto blend = [&](double mine, double theirs) {
            if (out.log && mine > 0.0 && theirs > 0.0)
                return std::exp(lerp(std::log(mine), std::log(theirs), chaos_));
            return lerp(mine, theirs, chaos_);
        };
        const double lo = blend(lowOf(*layered), lowOf(b));
        const double hi = blend(highOf(*layered), highOf(b));
        out.lo = std::min(lo, hi);
        out.hi = std::max(lo, hi);
    }
    return out;
}

double KnobSet::real(std::string_view name, Rng& rng, double fallback) const
{
    const auto spec = effective(name);
    if (!spec)
        return fallback;
    switch (spec->kind)
    {
        case KnobSpec::Kind::Fixed:
            return spec->value;
        case KnobSpec::Kind::Range:
            return spec->log ? rng.logRange(spec->lo, spec->hi) : rng.range(spec->lo, spec->hi);
        case KnobSpec::Kind::Choice:
        {
            std::vector<double> weights;
            for (const auto& c : spec->choices)
                weights.push_back(c.second);
            if (spec->choices.empty())
                return fallback;
            return parseNumber(spec->choices[rng.weighted(weights)].first, fallback);
        }
    }
    return fallback;
}

int KnobSet::integer(std::string_view name, Rng& rng, int fallback) const
{
    return static_cast<int>(std::lround(real(name, rng, fallback)));
}

bool KnobSet::chance(std::string_view name, Rng& rng, double fallback) const
{
    return rng.chance(std::clamp(real(name, rng, fallback), 0.0, 1.0));
}

std::string KnobSet::pick(std::string_view name, Rng& rng, std::string_view fallback) const
{
    const auto spec = effective(name);
    if (!spec || spec->kind != KnobSpec::Kind::Choice || spec->choices.empty())
        return std::string(fallback);
    std::vector<double> weights;
    for (const auto& c : spec->choices)
        weights.push_back(c.second);
    return spec->choices[rng.weighted(weights)].first;
}

std::vector<std::pair<std::string, double>> KnobSet::choices(std::string_view name) const
{
    const auto spec = effective(name);
    if (!spec || spec->kind != KnobSpec::Kind::Choice)
        return {};
    return spec->choices;
}
} // namespace serumgen
