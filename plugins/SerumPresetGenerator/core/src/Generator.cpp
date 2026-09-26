#include "serumgen/Generator.h"

#include <algorithm>
#include <cctype>
#include <memory>
#include <cmath>
#include <functional>
#include <map>
#include <set>
#include <sstream>

#include "serum2/Patch.h"
#include "serumgen/Knobs.h"
#include "serumgen/Random.h"

namespace serumgen
{
using serum2::CurvePoint;
using serum2::FxUnit;
using serum2::ModDest;
using serum2::ModRoute;
using serum2::Module;
using serum2::PatchEditor;
using serum2::Preset;
using serum2::Schema;

namespace
{
constexpr std::array<std::string_view, 9> kGroupIds {
    "oscillators", "noise_sub", "filters", "envelopes", "lfos", "mod_matrix", "macros", "fx", "global",
};
constexpr std::array<std::string_view, 9> kGroupLabels {
    "Oscillators", "Noise + Sub", "Filters", "Envelopes", "LFOs", "Mod Matrix", "Macros", "FX", "Global",
};

// Serial FX order the generator builds racks in (dynamics and tone first,
// modulation next, time-based last).
const std::vector<std::string> kFxOrder {
    "FXDistortion", "FXFilter", "FXEQ",   "FXComp",  "FXChorus", "FXPhaser",
    "FXFlanger",    "FXHyperD", "FXBode", "FXDelay", "FXReverb", "FXUtils",
};

// Mod source ids by family (see serum2_schema.json modSources).
Group sourceGroup(int source)
{
    if (source >= 2 && source <= 5)
        return Group::Envelopes;
    if (source >= 6 && source <= 15)
        return Group::Lfos;
    if (source >= 25 && source <= 32)
        return Group::Macros;
    return Group::ModMatrix;
}

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string percent(double v)
{
    return std::to_string(static_cast<int>(std::lround(v * 100.0))) + "%";
}

std::string fixed(double v, int decimals = 2)
{
    std::ostringstream out;
    out.setf(std::ios::fixed);
    out.precision(decimals);
    out << v;
    return out.str();
}

std::string prettyFx(std::string_view type)
{
    if (type == "FXHyperD")
        return "Hyper/Dimension";
    if (type == "FXComp")
        return "Compressor";
    if (type == "FXUtils")
        return "Utility";
    return std::string(type.substr(2));
}

double sampleSpec(const Json& spec, Rng& rng, double fallback)
{
    if (spec.is_null())
        return fallback;
    KnobSet one(Json { { "v", spec } });
    return one.real("v", rng, fallback);
}

// One generation in progress.
class Build
{
public:
    Build(const ContentLibrary& content, const Schema& schema, const Settings& settings, const Preset& base, Rng& rng)
        : content_(content), schema_(schema), settings_(settings), baseData_(base.data), baseEd_(baseData_, schema),
          preset_(Schema::initPreset()), ed_(preset_.data, schema)
    {
        // Independent streams per stage: locking one group never changes what
        // the other groups generate for the same seed.
        for (std::size_t i = 0; i < stageRngs_.size(); ++i)
            stageRngs_[i] = rng.fork(i + 1);
        nameRng_ = std::make_unique<Rng>(rng.fork(100));
        pickRng_ = std::make_unique<Rng>(rng.fork(101));
    }

    Result run()
    {
        resolveProfile();
        if (settings_.mode == Mode::Mutate)
            mutate();
        else
        {
            stage(Group::Global, [this](Rng& r) { global(r); });
            stage(Group::Oscillators, [this](Rng& r) { oscillators(r); });
            stage(Group::NoiseSub, [this](Rng& r) { noiseSub(r); });
            stage(Group::Filters, [this](Rng& r) { filters(r); });
            stage(Group::Fx, [this](Rng& r) { fx(r); });
            stage(Group::Envelopes, [this](Rng& r) { envelopes(r); });
            stage(Group::Lfos, [this](Rng& r) { lfos(r); });
            stage(Group::Macros, [this](Rng& r) { macros(r); });
            stage(Group::ModMatrix, [this](Rng& r) { modMatrix(r); });
        }
        sanity();
        finish();
        return std::move(result_);
    }

private:
    // ---------------------------------------------------------------- setup

    void resolveProfile()
    {
        auto& rng = *pickRng_;
        knobs_ = KnobSet(content_.base());
        result_.seed = settings_.seed;

        if (settings_.mode == Mode::Random)
        {
            result_.category = "random";
            knobs_.applyLayer(Json { { "recipe", "none" } });
            return;
        }

        if (settings_.mode == Mode::Mutate)
        {
            result_.category = "variation";
            return;
        }

        // Genre.
        std::string genre = settings_.genre;
        if (genre == kAny && !content_.genres().empty())
            genre = content_.genres()[rng.index(content_.genres().size())].id;
        if (genre == kNone || content_.genreData(genre).empty())
            genre.clear();
        const Json& genreData = genre.empty() ? Json::object() : content_.genreData(genre);

        // Category.
        std::string category = settings_.category;
        if (category == kAny || content_.categoryData(category).empty())
        {
            category.clear();
            if (genreData.contains("categories"))
                category = KnobSet(Json { { "c", Json { { "choose", genreData["categories"] } } } }).pick("c", rng);
            if (category.empty() && !content_.categories().empty())
                category = content_.categories()[rng.index(content_.categories().size())].id;
        }
        const Json& categoryData = content_.categoryData(category);

        if (genreData.contains("knobs"))
            knobs_.applyLayer(genreData["knobs"]);
        if (categoryData.contains("knobs"))
            knobs_.applyLayer(categoryData["knobs"]);
        if (genreData.contains("perCategory") && genreData["perCategory"].contains(category))
            knobs_.applyLayer(genreData["perCategory"][category]);

        const auto recipeId = knobs_.pick("recipe", rng, "none");
        if (recipeId != "none" && !content_.recipe(recipeId).empty())
        {
            recipe_ = content_.recipe(recipeId);
            if (recipe_.contains("knobs"))
                knobs_.applyLayer(recipe_["knobs"]);
            result_.recipe = recipeId;
        }
        knobs_.setChaos(std::clamp(settings_.chaos, 0.0, 1.0));
        result_.category = category;
        result_.genre = genre;
    }

    template <typename Fn>
    void stage(Group g, Fn&& fn)
    {
        auto& rng = stageRngs_[static_cast<std::size_t>(g)];
        if (settings_.isLocked(g))
        {
            copyFromBase(g);
            summary(std::string(groupLabel(g)) + ": kept from base preset");
            return;
        }
        fn(rng);
    }

    void copyModule(const std::string& key)
    {
        if (baseData_.contains(key))
            preset_.data[key] = baseData_[key];
    }

    void copyFromBase(Group g)
    {
        const auto copyIndexed = [this](const char* name, int from, int to) {
            for (int i = from; i <= to; ++i)
                copyModule(name + std::to_string(i));
        };
        switch (g)
        {
            case Group::Oscillators:
                copyIndexed("Oscillator", 0, 2);
                copyIndexed("RoutingSlot", 0, 2);
                break;
            case Group::NoiseSub:
                copyIndexed("Oscillator", 3, 4);
                copyIndexed("RoutingSlot", 3, 4);
                break;
            case Group::Filters:
                copyIndexed("VoiceFilter", 0, 1);
                copyIndexed("RoutingSlot", 5, 6);
                break;
            case Group::Envelopes:
                copyIndexed("Env", 0, 3);
                break;
            case Group::Lfos:
                copyIndexed("LFO", 0, 9);
                break;
            case Group::Macros:
                copyIndexed("Macro", 0, 7);
                break;
            case Group::Fx:
                copyIndexed("FXRack", 0, 2);
                break;
            case Group::Global:
                copyModule("Global0");
                break;
            case Group::ModMatrix:
                break;
        }

        // Carry over the base preset's routes owned by this group, dropping
        // any that point at FX units that no longer exist.
        for (const int slot : baseEd_.usedModSlots())
        {
            const auto route = baseEd_.modRoute(slot);
            if (!route || sourceGroup(route->source) != g)
                continue;
            if (route->dest.type.rfind("FX", 0) == 0)
            {
                const auto units = ed_.fxRack(route->dest.id / 100);
                const auto pos = static_cast<std::size_t>(route->dest.id % 100);
                if (pos >= units.size() || units[pos].type != route->dest.type)
                    continue;
            }
            const int newSlot = ed_.addModRoute(*route);
            // Keep any extra per-route settings (curves, smoothing) intact.
            if (newSlot >= 0)
                preset_.data["ModSlot" + std::to_string(newSlot)] = baseData_["ModSlot" + std::to_string(slot)];
        }
    }

    void summary(std::string line) { result_.summary.push_back(std::move(line)); }

    // ------------------------------------------------------------ patch state

    bool oscEnabled(int i) const { return ed_.flag(Module::osc(i), "kParamEnable"); }
    bool oscIsWavetable(int i) const { return i < 3 && ed_.text(Module::osc(i), "kParamType") != "kOsc_MultiSample"; }
    bool filterEnabled(int i) const { return ed_.flag(Module::filter(i), "kParamEnable"); }

    std::vector<int> fxPositions(std::string_view type) const
    {
        std::vector<int> positions;
        const auto units = ed_.fxRack(0);
        for (std::size_t i = 0; i < units.size(); ++i)
            if (units[i].type == type)
                positions.push_back(static_cast<int>(i));
        return positions;
    }

    bool destAvailable(std::string_view prefix, int index, std::string_view param) const
    {
        if (prefix == "oscillator")
        {
            if (!oscEnabled(index))
                return false;
            if (param == "table_position")
                return oscIsWavetable(index);
            // Warp amount only matters once a warp mode is chosen.
            if (param.rfind("warp", 0) == 0)
                return oscIsWavetable(index) && ed_.has(Module::wavetable(index), "kParamWarpMenu");
            return true;
        }
        if (prefix == "filter")
            return filterEnabled(index);
        if (prefix == "lfo")
            return lfoUsed_[static_cast<std::size_t>(index)];
        if (prefix == "env")
            return index == 0 || envUsed_[static_cast<std::size_t>(index)];
        if (prefix == "macro")
            return true;
        return true;
    }

    // Resolves a destination name from a profile:
    //   "filter0.cutoff"           explicit (skipped if filter 0 is off)
    //   "oscillator*.table_position" any active slot
    //   "fx:FXReverb.wet"          a parameter of an FX unit in rack 0
    std::optional<ModDest> resolveDest(std::string_view name, Rng& rng) const
    {
        if (name.rfind("fx:", 0) == 0)
        {
            const auto dot = name.find('.');
            if (dot == std::string_view::npos)
                return std::nullopt;
            const auto type = name.substr(3, dot - 3);
            const auto positions = fxPositions(type);
            if (positions.empty())
                return std::nullopt;
            return schema_.fxModDest(type, 0, positions[rng.index(positions.size())], name.substr(dot + 1));
        }

        const auto dot = name.find('.');
        if (dot == std::string_view::npos)
            return std::nullopt;
        const auto head = name.substr(0, dot);
        const auto param = name.substr(dot + 1);
        std::size_t digits = head.size();
        while (digits > 0 && (std::isdigit(static_cast<unsigned char>(head[digits - 1])) || head[digits - 1] == '*'))
            --digits;
        const auto prefix = head.substr(0, digits);
        const auto indexPart = head.substr(digits);

        if (indexPart == "*")
        {
            const int count = prefix == "oscillator" ? 5 : prefix == "filter" ? 2 : prefix == "lfo" ? 10 : prefix == "env" ? 4 : prefix == "macro" ? 8 : 0;
            std::vector<int> candidates;
            for (int i = 0; i < count; ++i)
                if (destAvailable(prefix, i, param) && schema_.modDest(std::string(prefix) + std::to_string(i) + "." + std::string(param)))
                    candidates.push_back(i);
            if (candidates.empty())
                return std::nullopt;
            const int i = candidates[rng.index(candidates.size())];
            return schema_.modDest(std::string(prefix) + std::to_string(i) + "." + std::string(param));
        }
        if (!indexPart.empty() && !destAvailable(prefix, std::stoi(std::string(indexPart)), param))
            return std::nullopt;
        return schema_.modDest(name);
    }

    // Picks a destination from a choice knob, retrying other options when the
    // chosen one isn't available in this patch.
    std::optional<ModDest> pickDest(std::string_view knob, Rng& rng) const
    {
        auto options = knobs_.choices(knob);
        while (!options.empty())
        {
            std::vector<double> weights;
            for (const auto& o : options)
                weights.push_back(o.second);
            const auto i = rng.weighted(weights);
            if (auto dest = resolveDest(options[i].first, rng))
                return dest;
            options.erase(options.begin() + static_cast<std::ptrdiff_t>(i));
        }
        return std::nullopt;
    }

    bool addRoute(int source, const ModDest& dest, double amount, bool bipolar = false, int aux = 0)
    {
        return ed_.addModRoute({ source, aux, dest, amount, bipolar }) >= 0;
    }

    // Recipe routes for one group; returns the source indices they used.
    std::set<int> recipeRoutes(Group g, Rng& rng)
    {
        std::set<int> used;
        if (!recipe_.contains("routes"))
            return used;
        for (const auto& r : recipe_["routes"])
        {
            const auto source = schema_.modSource(r.value("source", ""));
            if (!source || sourceGroup(*source) != g)
                continue;
            if (!rng.chance(r.value("chance", 1.0)))
                continue;
            const auto dest = resolveDest(r.value("dest", ""), rng);
            if (!dest)
                continue;
            const int aux = r.contains("aux") ? schema_.modSource(r["aux"].get<std::string>()).value_or(0) : 0;
            if (addRoute(*source, *dest, sampleSpec(r.value("amount", Json(50.0)), rng, 50.0), r.value("bipolar", false), aux))
                used.insert(*source);
        }
        return used;
    }

    std::vector<std::string> recipeMacroNames() const
    {
        std::vector<std::string> names(serum2::kNumMacros);
        if (!recipe_.contains("routes"))
            return names;
        for (const auto& r : recipe_["routes"])
        {
            const auto source = schema_.modSource(r.value("source", ""));
            if (source && *source >= 25 && *source <= 32 && r.contains("name"))
                names[static_cast<std::size_t>(*source - 25)] = r["name"].get<std::string>();
        }
        return names;
    }

    // --------------------------------------------------------------- stages

    void global(Rng& rng)
    {
        const auto g = Module::global();
        const bool mono = knobs_.chance("global.mono", rng);
        ed_.setFlag(g, "kParamMonoToggle", mono);
        std::string line = mono ? "Global: mono" : "Global: poly";
        if (mono && knobs_.chance("global.legato", rng))
        {
            ed_.setFlag(g, "kParamLegato", true);
            line += ", legato";
        }
        else if (!mono)
        {
            const int voices = std::clamp(knobs_.integer("global.poly", rng, 8), 1, 32);
            ed_.set(g, "kParamPolyCount", voices);
            line += " (" + std::to_string(voices) + " voices)";
        }
        if (knobs_.chance("global.porta", rng))
        {
            const double time = knobs_.real("global.portaTime", rng, 0.05);
            ed_.set(g, "kParamPortamentoTime", time);
            ed_.setFlag(g, "kParamPortaAlways", knobs_.chance("global.portaAlways", rng));
            line += ", glide " + std::to_string(static_cast<int>(time * 1000.0)) + " ms";
        }
        const int bend = knobs_.integer("global.bend", rng, 2);
        ed_.set(g, "kParamBendRangeUp", bend);
        ed_.set(g, "kParamBendRangeDn", -bend);
        summary(line);
    }

    const serum2::Wavetable* chooseWavetable(const std::string& slot, Rng& rng) const
    {
        if (knobs_.has(slot + ".wavetable"))
            if (const auto* w = schema_.wavetable(knobs_.pick(slot + ".wavetable", rng)))
                return w;
        const auto tag = knobs_.pick(slot + ".wtTag", rng, "basic");
        std::vector<const serum2::Wavetable*> candidates;
        for (const auto& w : schema_.wavetables())
            if (w.hasTag(tag))
                candidates.push_back(&w);
        if (candidates.empty())
            return schema_.wavetable("default");
        return candidates[rng.index(candidates.size())];
    }

    const serum2::MultiSample* chooseMultisample(const std::string& slot, Rng& rng) const
    {
        if (knobs_.has(slot + ".multisample"))
            if (const auto* m = schema_.multisample(knobs_.pick(slot + ".multisample", rng)))
                return m;
        const auto tag = knobs_.pick(slot + ".msTag", rng, "keys");
        std::vector<const serum2::MultiSample*> candidates;
        for (const auto& m : schema_.multisamples())
            if (m.hasTag(tag))
                candidates.push_back(&m);
        if (candidates.empty())
            return nullptr;
        return candidates[rng.index(candidates.size())];
    }

    void oscillators(Rng& rng)
    {
        const int count = std::clamp(knobs_.integer("osc.count", rng, 1), 1, 3);
        static constexpr const char* kLetters[] = { "A", "B", "C" };
        for (int i = 0; i < 3; ++i)
        {
            const auto slot = "osc" + std::to_string(i);
            const auto osc = Module::osc(i);
            bool on = i < count;
            if (knobs_.has(slot + ".on"))
                on = knobs_.chance(slot + ".on", rng);
            if (i == 0)
                on = true;
            ed_.setFlag(osc, "kParamEnable", on);
            if (!on)
                continue;

            std::string line = std::string("Osc ") + kLetters[i] + ": ";
            const auto engine = knobs_.pick(slot + ".engine", rng, "wavetable");
            const auto* instrument = engine == "multisample" ? chooseMultisample(slot, rng) : nullptr;
            if (instrument)
            {
                ed_.setMultisample(i, *instrument);
                line += "multisample " + instrument->container["sfzPathRelative"].get<std::string>();
            }
            else
            {
                const auto* table = chooseWavetable(slot, rng);
                ed_.setWavetable(i, *table);
                const auto wt = Module::wavetable(i);
                double pos = knobs_.real(slot + ".pos", rng, 0.0);
                if (knobs_.has(slot + ".frame"))
                {
                    const auto frame = knobs_.pick(slot + ".frame", rng);
                    if (const auto it = table->knownFrames.find(frame); it != table->knownFrames.end())
                        pos = it->second;
                }
                ed_.set(wt, "kParamTablePos", pos);
                line += table->path + " @ " + std::to_string(static_cast<int>(pos));
                if (knobs_.chance(slot + ".warp", rng))
                {
                    const auto mode = knobs_.pick(slot + ".warpMode", rng, "kBendPos");
                    const double amount = knobs_.real(slot + ".warpAmount", rng, 0.3);
                    ed_.force(wt, "kParamWarpMenu", mode);
                    ed_.set(wt, "kParamWarp", amount);
                    line += ", warp " + mode.substr(1) + " " + percent(amount);
                }
                if (knobs_.has(slot + ".randomPhase"))
                    ed_.set(wt, "kParamRandomPhase", knobs_.real(slot + ".randomPhase", rng, 0.0));
            }

            const int octave = knobs_.integer(slot + ".octave", rng, 0);
            const int semi = knobs_.integer(slot + ".semi", rng, 0);
            ed_.set(osc, "kParamOctave", octave);
            ed_.set(osc, "kParamPitch", semi);
            ed_.set(osc, "kParamFine", knobs_.real(slot + ".fine", rng, 0.0));
            if (octave != 0 || semi != 0)
                line += ", " + std::to_string(octave) + " oct" + (semi != 0 ? " " + std::to_string(semi) + " st" : "");

            const int unison = std::clamp(knobs_.integer(slot + ".unison", rng, 1), 1, 16);
            ed_.set(osc, "kParamUnison", unison);
            if (unison > 1)
            {
                const double detune = knobs_.real(slot + ".detune", rng, 0.1);
                ed_.set(osc, "kParamDetune", detune);
                if (knobs_.has(slot + ".width"))
                    ed_.set(osc, "kParamUnisonStereo", knobs_.real(slot + ".width", rng, 100.0));
                line += ", unison " + std::to_string(unison) + " detune " + fixed(detune);
            }
            ed_.set(osc, "kParamVolume", knobs_.real(slot + ".volume", rng, 0.75));
            ed_.set(osc, "kParamPan", knobs_.real(slot + ".pan", rng, 0.0));
            summary(line);
        }
    }

    void noiseSub(Rng& rng)
    {
        if (knobs_.chance("noise.on", rng))
        {
            ed_.setFlag(Module::osc(3), "kParamEnable", true);
            const auto type = knobs_.pick("noise.type", rng, "White");
            ed_.setNoiseType(type);
            const double volume = knobs_.real("noise.volume", rng, 0.2);
            ed_.set(Module::osc(3), "kParamVolume", volume);
            summary("Noise: " + type + " at " + percent(volume));
        }
        if (knobs_.chance("sub.on", rng))
        {
            const auto sub = Module::osc(4);
            ed_.setFlag(sub, "kParamEnable", true);
            const int octave = knobs_.integer("sub.octave", rng, -1);
            ed_.set(sub, "kParamOctave", octave);
            const double volume = knobs_.real("sub.volume", rng, 0.5);
            ed_.set(sub, "kParamVolume", volume);
            const auto shape = knobs_.pick("sub.shape", rng, "sine");
            if (shape != "sine")
                ed_.setText(Module::sub(), "kParamShape", shape);
            summary("Sub: " + (shape == "sine" ? std::string("sine") : shape.substr(1)) + ", octave " + std::to_string(octave) + ", " + percent(volume));
        }
    }

    void filters(Rng& rng)
    {
        for (int f = 0; f < 2; ++f)
        {
            // Filter B has its own knob names (no slot digits) so a profile's
            // "filter.on" doesn't also switch it on.
            const std::string own = f == 0 ? "filter" : "filterB";
            const auto knob = [&](const std::string& name) {
                return knobs_.has(own + "." + name) ? own + "." + name : "filter." + name;
            };
            if (f == 1 && !filterEnabled(0))
                break;
            if (!knobs_.chance(own + ".on", rng))
                continue;
            const auto flt = Module::filter(f);
            ed_.setFlag(flt, "kParamEnable", true);
            const auto type = knobs_.pick(knob("type"), rng, "MgL24");
            ed_.setText(flt, "kParamType", type);
            const double cutoff = knobs_.real(knob("cutoff"), rng, 0.6);
            const double reso = knobs_.real(knob("reso"), rng, 10.0);
            const double drive = knobs_.real(knob("drive"), rng, 0.0);
            ed_.set(flt, "kParamFreq", cutoff);
            ed_.set(flt, "kParamReso", reso);
            ed_.set(flt, "kParamDrive", drive);
            if (knobs_.chance(knob("keytrack"), rng))
                ed_.setFlag(flt, "kParamKeyTrack", true);
            summary(std::string("Filter ") + (f == 0 ? "1" : "2") + ": " + type + " cutoff " + percent(cutoff) + " reso "
                    + std::to_string(static_cast<int>(reso)) + " drive " + std::to_string(static_cast<int>(drive)));
        }
    }

    void fx(Rng& rng)
    {
        std::vector<FxUnit> units;
        for (const auto& type : kFxOrder)
        {
            if (!knobs_.chance("fx." + type, rng))
                continue;
            FxUnit unit { type, Json::object() };
            if (const auto* table = schema_.fxParams(type))
                for (const auto& [param, def] : *table)
                {
                    const auto knob = "fx." + type + "." + param;
                    if (!knobs_.has(knob))
                        continue;
                    if (def.kind == serum2::ParamDef::Kind::Enum)
                        unit.params[param] = knobs_.pick(knob, rng);
                    else if (def.kind == serum2::ParamDef::Kind::Bool)
                        unit.params[param] = knobs_.chance(knob, rng) ? 1.0 : 0.0;
                    else
                        unit.params[param] = knobs_.real(knob, rng);
                }
            units.push_back(std::move(unit));
        }
        const auto maxUnits = static_cast<std::size_t>(std::max(0, knobs_.integer("fx.max", rng, 6)));
        while (units.size() > maxUnits)
            units.erase(units.begin() + static_cast<std::ptrdiff_t>(rng.index(units.size())));
        if (units.size() > 1 && knobs_.chance("fx.shuffle", rng))
        {
            const auto i = rng.index(units.size() - 1);
            std::swap(units[i], units[i + 1]);
        }
        ed_.setFxRack(0, units);

        std::string line = "FX: ";
        for (std::size_t i = 0; i < units.size(); ++i)
            line += (i ? " > " : "") + prettyFx(units[i].type);
        summary(units.empty() ? "FX: none" : line);
    }

    void envelopeShape(int e, const std::string& prefix, Rng& rng)
    {
        const auto env = Module::env(e);
        ed_.set(env, "kParamAttack", knobs_.real(prefix + ".attack", rng, 0.0005));
        ed_.set(env, "kParamDecay", knobs_.real(prefix + ".decay", rng, 1.0));
        ed_.set(env, "kParamSustain", knobs_.real(prefix + ".sustain", rng, 1.0));
        ed_.set(env, "kParamRelease", knobs_.real(prefix + ".release", rng, 0.015));
        for (int c = 1; c <= 3; ++c)
        {
            const auto knob = prefix + ".curve" + std::to_string(c);
            if (knobs_.has(knob))
                ed_.set(env, "kParamCurve" + std::to_string(c), knobs_.real(knob, rng));
        }
    }

    void envelopes(Rng& rng)
    {
        envelopeShape(0, "amp", rng);
        const auto amp = Module::env(0);
        summary("Amp env: A " + fixed(ed_.number(amp, "kParamAttack"), 3) + "s D " + fixed(ed_.number(amp, "kParamDecay"))
                + "s S " + percent(ed_.number(amp, "kParamSustain")) + " R " + fixed(ed_.number(amp, "kParamRelease")) + "s");

        // Envelopes named by recipe routes are always shaped; then up to
        // env.count more get random destinations.
        std::set<int> wanted;
        if (recipe_.contains("routes"))
            for (const auto& r : recipe_["routes"])
                if (const auto s = schema_.modSource(r.value("source", "")); s && *s >= 3 && *s <= 5)
                    wanted.insert(*s - 2);
        const int count = std::clamp(knobs_.integer("env.count", rng, 0), 0, 3);
        for (int e = 1; e <= count; ++e)
            wanted.insert(e);
        for (const int e : wanted)
        {
            envUsed_[static_cast<std::size_t>(e)] = true;
            envelopeShape(e, "env" + std::to_string(e), rng);
        }

        const auto fromRecipe = recipeRoutes(Group::Envelopes, rng);
        for (const int e : wanted)
        {
            const int source = e + 2;
            if (fromRecipe.count(source))
                continue;
            const auto prefix = "env" + std::to_string(e);
            const int dests = std::clamp(knobs_.integer(prefix + ".dests", rng, 1), 0, 4);
            for (int d = 0; d < dests; ++d)
                if (const auto dest = pickDest(prefix + ".dest", rng))
                    addRoute(source, *dest, knobs_.real(prefix + ".amount", rng, 40.0));
        }
        for (const int e : wanted)
        {
            const auto env = Module::env(e);
            summary("Env " + std::to_string(e + 1) + ": A " + fixed(ed_.number(env, "kParamAttack"), 3) + "s D "
                    + fixed(ed_.number(env, "kParamDecay")) + "s S " + percent(ed_.number(env, "kParamSustain")));
        }
    }

    void applyLfoShape(int l, const std::string& shape, Rng& rng)
    {
        std::vector<CurvePoint> pts;
        if (shape == "triangle")
        {
            ed_.clearLfoCurve(l);
            return;
        }
        if (shape == "lorenz" || shape == "rossler" || shape == "random")
        {
            ed_.setLfoChaos(l, shape == "lorenz" ? "Lorenz" : shape == "rossler" ? "Rossler" : "RandomSH");
            return;
        }
        if (shape == "sine")
            for (int i = 0; i <= 16; ++i)
            {
                const double x = i / 16.0;
                pts.push_back({ x, 0.5 - 0.5 * std::cos(6.283185307179586 * x) });
            }
        else if (shape == "saw_up")
            pts = { { 0, 0 }, { 0.5, 0.5 }, { 1, 1 } };
        else if (shape == "saw_down")
            pts = { { 0, 1 }, { 0.5, 0.5 }, { 1, 0 } };
        else if (shape == "square")
            pts = { { 0, 1 }, { 0.5, 1 }, { 0.5, 0 }, { 1, 0 } };
        else if (shape == "pulse")
            pts = { { 0, 1 }, { 0.25, 1 }, { 0.25, 0 }, { 1, 0 } };
        else if (shape == "decay")
            pts = { { 0, 1 }, { 0.12, 0.45 }, { 0.35, 0.15 }, { 1, 0 } };
        else if (shape == "stairs")
            pts = { { 0, 0 }, { 0.25, 0 }, { 0.25, 0.33 }, { 0.5, 0.33 }, { 0.5, 0.66 }, { 0.75, 0.66 }, { 0.75, 1 }, { 1, 1 } };
        else if (shape == "gate")
        {
            // Eight-step trance gate with a random on/off pattern.
            const int steps = 8;
            bool anyOn = false;
            for (int s = 0; s < steps; ++s)
            {
                const bool on = s == 0 || rng.chance(0.6);
                anyOn = anyOn || on;
                const double y = on ? 1.0 : 0.0;
                const double x0 = s / static_cast<double>(steps);
                const double x1 = (s + 1) / static_cast<double>(steps);
                pts.push_back({ x0, y });
                pts.push_back({ x1 - 0.02, y });
                pts.push_back({ x1 - 0.02, 0.0 });
            }
            pts.back().x = 1.0;
        }
        else
        {
            ed_.clearLfoCurve(l);
            return;
        }
        ed_.setLfoCurve(l, pts);
    }

    int firstFreeLfo() const
    {
        for (int l = 0; l < serum2::kNumLfos; ++l)
            if (!lfoUsed_[static_cast<std::size_t>(l)])
                return l;
        return -1;
    }

    std::string configureLfo(int l, Rng& rng)
    {
        lfoUsed_[static_cast<std::size_t>(l)] = true;
        const auto prefix = "lfo" + std::to_string(l);
        const auto lfo = Module::lfo(l);
        const auto shape = knobs_.pick(prefix + ".shape", rng, "triangle");
        applyLfoShape(l, shape, rng);

        const auto mode = knobs_.pick(prefix + ".mode", rng, "Retrig");
        ed_.force(lfo, "kParamMode", mode);
        ed_.force(lfo, "kParamDefaultMode", 0.0);

        std::string rate;
        if (knobs_.chance(prefix + ".sync", rng, 0.7))
        {
            rate = knobs_.pick(prefix + ".syncRate", rng, "1/4");
            const auto it = schema_.lfoSyncRates().find(rate);
            ed_.set(lfo, "kParamRate", it != schema_.lfoSyncRates().end() ? it->second : 6.25);
            if (knobs_.chance(prefix + ".triplet", rng))
            {
                ed_.setFlag(lfo, "kParamTriplets", true);
                rate += "T";
            }
            else if (knobs_.chance(prefix + ".dotted", rng))
            {
                ed_.setFlag(lfo, "kParamDotted", true);
                rate += ".";
            }
        }
        else
        {
            const double hz = knobs_.real(prefix + ".rateHz", rng, 2.0);
            ed_.setFlag(lfo, "kParamBeatSync", false);
            ed_.set(lfo, "kParamRate", hz);
            rate = fixed(hz) + " Hz";
        }
        for (const auto& [knob, key] : { std::pair { ".delay", "kParamDelay" }, { ".rise", "kParamRise" }, { ".smooth", "kParamSmooth" } })
            if (knobs_.has(prefix + knob))
                ed_.set(lfo, key, knobs_.real(prefix + knob, rng));
        return "LFO " + std::to_string(l + 1) + ": " + shape + ", " + rate + ", " + lower(mode);
    }

    void lfos(Rng& rng)
    {
        std::set<int> wanted;
        if (recipe_.contains("routes"))
            for (const auto& r : recipe_["routes"])
                if (const auto s = schema_.modSource(r.value("source", "")); s && *s >= 6 && *s <= 15)
                    wanted.insert(*s - 6);
        // LFOs named by recipe routes, plus lfo.count more with random targets.
        const auto target = wanted.size() + static_cast<std::size_t>(std::clamp(knobs_.integer("lfo.count", rng, 0), 0, 4));
        for (int l = 0; l < serum2::kNumLfos && wanted.size() < target; ++l)
            wanted.insert(l);

        std::vector<std::string> lines;
        for (const int l : wanted)
            lines.push_back(configureLfo(l, rng));

        const auto fromRecipe = recipeRoutes(Group::Lfos, rng);
        for (const int l : wanted)
        {
            const int source = l + 6;
            if (fromRecipe.count(source))
                continue;
            const auto prefix = "lfo" + std::to_string(l);
            const int dests = std::clamp(knobs_.integer(prefix + ".dests", rng, 1), 0, 4);
            for (int d = 0; d < dests; ++d)
                if (const auto dest = pickDest(prefix + ".dest", rng))
                    addRoute(source, *dest, knobs_.real(prefix + ".amount", rng, 30.0), knobs_.chance(prefix + ".bipolar", rng));
        }

        // Expressive vibrato: a delayed sine LFO on pitch, scaled by the mod
        // wheel so it only appears when played.
        if (knobs_.chance("expr.vibrato", rng))
        {
            const int l = firstFreeLfo();
            if (l >= 0)
            {
                lfoUsed_[static_cast<std::size_t>(l)] = true;
                const auto lfo = Module::lfo(l);
                applyLfoShape(l, "sine", rng);
                ed_.force(lfo, "kParamMode", "Retrig");
                ed_.force(lfo, "kParamDefaultMode", 0.0);
                ed_.setFlag(lfo, "kParamBeatSync", false);
                ed_.set(lfo, "kParamRate", knobs_.real("expr.vibratoRate", rng, 5.5));
                ed_.set(lfo, "kParamDelay", knobs_.real("expr.vibratoDelay", rng, 0.3));
                ed_.set(lfo, "kParamRise", knobs_.real("expr.vibratoRise", rng, 0.5));
                const double depth = knobs_.real("expr.vibratoDepth", rng, 15.0);
                const auto wheel = schema_.modSource("mod_wheel").value_or(1);
                for (int i = 0; i < 3; ++i)
                    if (oscEnabled(i))
                        if (const auto dest = schema_.modDest("oscillator" + std::to_string(i) + ".fine"))
                            addRoute(l + 6, *dest, depth, true, wheel);
                lines.push_back("LFO " + std::to_string(l + 1) + ": vibrato on the mod wheel");
            }
        }
        for (auto& line : lines)
            summary(std::move(line));
    }

    void macros(Rng& rng)
    {
        auto names = recipeMacroNames();
        const auto fromRecipe = recipeRoutes(Group::Macros, rng);
        std::array<bool, serum2::kNumMacros> used {};
        for (const int source : fromRecipe)
        {
            const int m = source - 25;
            used[static_cast<std::size_t>(m)] = true;
            if (!names[static_cast<std::size_t>(m)].empty())
                ed_.setMacroName(m, names[static_cast<std::size_t>(m)]);
        }

        const int count = std::clamp(knobs_.integer("macro.count", rng, 0), 0, serum2::kNumMacros);
        auto themes = knobs_.choices("macro.theme");
        std::vector<std::string> assigned;
        int m = 0;
        for (int n = 0; n < count && !themes.empty(); ++n)
        {
            while (m < serum2::kNumMacros && used[static_cast<std::size_t>(m)])
                ++m;
            if (m >= serum2::kNumMacros)
                break;
            std::vector<double> weights;
            for (const auto& t : themes)
                weights.push_back(t.second);
            const auto pickIndex = rng.weighted(weights);
            const auto themeId = themes[pickIndex].first;
            themes.erase(themes.begin() + static_cast<std::ptrdiff_t>(pickIndex));
            const auto& theme = content_.macroThemes().contains(themeId) ? content_.macroThemes()[themeId] : Json::object();
            if (!theme.contains("targets"))
                continue;

            int routed = 0;
            for (const auto& target : theme["targets"])
                if (const auto dest = resolveDest(target.value("dest", ""), rng))
                    routed += addRoute(25 + m, *dest, sampleSpec(target.value("amount", Json(50.0)), rng, 50.0)) ? 1 : 0;
            if (routed == 0)
                continue;

            std::string name = themeId;
            if (theme.contains("names") && !theme["names"].empty())
                name = theme["names"][rng.index(theme["names"].size())].get<std::string>();
            ed_.setMacroName(m, name);
            ed_.set(Module::macro(m), "kParamValue", knobs_.real("macro.value", rng, 0.0));
            used[static_cast<std::size_t>(m)] = true;
            assigned.push_back(name);
        }
        for (int i = 0; i < serum2::kNumMacros; ++i)
            if (used[static_cast<std::size_t>(i)] && !ed_.macroName(i).empty()
                && std::find(assigned.begin(), assigned.end(), ed_.macroName(i)) == assigned.end())
                assigned.insert(assigned.begin(), ed_.macroName(i));
        if (!assigned.empty())
        {
            std::string line = "Macros: ";
            for (std::size_t i = 0; i < assigned.size(); ++i)
                line += (i ? ", " : "") + assigned[i];
            summary(line);
        }
    }

    void modMatrix(Rng& rng)
    {
        recipeRoutes(Group::ModMatrix, rng);
        const auto source = [&](const char* name) { return schema_.modSource(name).value_or(0); };

        if (knobs_.chance("expr.velCutoff", rng))
            if (const auto dest = resolveDest("filter*.cutoff", rng))
                addRoute(source("velocity"), *dest, knobs_.real("expr.velCutoffAmount", rng, 20.0));
        if (knobs_.chance("expr.velAmp", rng))
        {
            const double amount = knobs_.real("expr.velAmpAmount", rng, 25.0);
            for (int i = 0; i < 5; ++i)
                if (oscEnabled(i))
                    if (const auto dest = schema_.modDest("oscillator" + std::to_string(i) + ".volume"))
                        addRoute(source("velocity"), *dest, amount);
        }
        if (knobs_.chance("expr.atCutoff", rng))
            if (const auto dest = resolveDest("filter*.cutoff", rng))
                addRoute(source("aftertouch"), *dest, knobs_.real("expr.atCutoffAmount", rng, 20.0));
        if (knobs_.chance("expr.wheelCutoff", rng))
            if (const auto dest = resolveDest("filter*.cutoff", rng))
                addRoute(source("mod_wheel"), *dest, knobs_.real("expr.wheelCutoffAmount", rng, 30.0));

        const int extra = std::clamp(knobs_.integer("mod.count", rng, 0), 0, 8);
        for (int n = 0; n < extra; ++n)
        {
            const auto src = schema_.modSource(knobs_.pick("mod.source", rng, "velocity"));
            const auto dest = pickDest("mod.dest", rng);
            if (src && dest)
                addRoute(*src, *dest, knobs_.real("mod.amount", rng, 25.0));
        }
    }

    // ------------------------------------------------------- route summary

    std::string sourceLabel(int id) const
    {
        if (id >= 2 && id <= 5)
            return "Env " + std::to_string(id - 1);
        if (id >= 6 && id <= 15)
            return "LFO " + std::to_string(id - 5);
        if (id >= 25 && id <= 32)
        {
            const auto name = ed_.macroName(id - 25);
            return name.empty() ? "Macro " + std::to_string(id - 24) : name;
        }
        auto name = schema_.modSourceName(id).value_or("source " + std::to_string(id));
        std::replace(name.begin(), name.end(), '_', ' ');
        if (!name.empty())
            name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
        return name;
    }

    std::string destLabel(const ModDest& d) const
    {
        static const std::map<std::string, std::string, std::less<>> params {
            { "kParamFreq", "cutoff" },   { "kParamReso", "resonance" }, { "kParamTablePos", "WT position" },
            { "kParamWarp", "warp" },     { "kParamVolume", "level" },   { "kParamPitch", "pitch" },
            { "kParamOctave", "octave" }, { "kParamFine", "fine tune" }, { "kParamPan", "pan" },
            { "kParamDetune", "detune" }, { "kParamWet", "mix" },        { "kParamDrive", "drive" },
            { "kParamRate", "rate" },     { "kParamAttack", "attack" },  { "kParamDecay", "decay" },
            { "kParamRelease", "release" }, { "kParamWidth", "width" },
        };
        std::string module;
        if (d.type == "Oscillator" || d.type == "WTOsc")
            module = d.id < 3 ? std::string("Osc ") + static_cast<char>('A' + d.id) : d.id == 3 ? "Noise" : "Sub";
        else if (d.type == "VoiceFilter")
            module = "Filter " + std::to_string(d.id + 1);
        else if (d.type == "Env")
            module = d.id == 0 ? "Amp env" : "Env " + std::to_string(d.id + 1);
        else if (d.type == "LFO")
            module = "LFO " + std::to_string(d.id + 1);
        else if (d.type == "Macro")
            module = "Macro " + std::to_string(d.id + 1);
        else if (d.type.rfind("FX", 0) == 0)
            module = prettyFx(d.type);
        else
            module = d.type;
        const auto it = params.find(d.paramName);
        return module + " " + (it != params.end() ? it->second : d.paramName);
    }

    void summarizeRoutes()
    {
        for (const int slot : ed_.usedModSlots())
        {
            const auto r = ed_.modRoute(slot);
            if (!r)
                continue;
            std::string line = "Mod: " + sourceLabel(r->source);
            if (r->aux != 0)
                line += " x " + sourceLabel(r->aux);
            line += " > " + destLabel(r->dest) + " " + (r->amount >= 0 ? "+" : "") + std::to_string(static_cast<int>(std::lround(r->amount))) + "%";
            summary(line);
        }
    }

    // --------------------------------------------------------------- mutate

    double nudge(double value, double lo, double hi, double amount, Rng& rng, bool logScale = false)
    {
        if (logScale && value > 0.0 && lo > 0.0)
            return std::clamp(value * std::exp(rng.gaussian() * amount), lo, hi);
        return std::clamp(value + rng.gaussian() * amount * 0.25 * (hi - lo), lo, hi);
    }

    void nudgeParam(const Module& m, std::string_view key, double amount, Rng& rng, bool logScale = false,
                    double logFloor = 0.0005)
    {
        const auto* def = schema_.param(m.schemaModule, key);
        if (!def || !def->min || !def->max)
            return;
        const double lo = logScale ? std::max(*def->min, logFloor) : *def->min;
        ed_.set(m, key, nudge(std::max(ed_.number(m, key), lo), lo, *def->max, amount, rng, logScale));
    }

    void nudgeRoutes(Group g, double amount, Rng& rng)
    {
        for (const int slot : ed_.usedModSlots())
        {
            const auto route = ed_.modRoute(slot);
            if (!route || sourceGroup(route->source) != g)
                continue;
            auto& p = preset_.data["ModSlot" + std::to_string(slot)]["plainParams"];
            if (!p.is_object())
                p = Json::object();
            p["kParamAmount"] = nudge(route->amount, -100.0, 100.0, amount, rng);
        }
    }

    void mutate()
    {
        preset_.data = baseData_;
        const double amount = std::clamp(settings_.chaos, 0.05, 1.0);
        for (const auto g : kAllGroups)
        {
            if (settings_.isLocked(g))
                continue;
            auto& rng = stageRngs_[static_cast<std::size_t>(g)];
            switch (g)
            {
                case Group::Oscillators:
                    for (int i = 0; i < 3; ++i)
                    {
                        if (!oscEnabled(i))
                            continue;
                        const auto osc = Module::osc(i);
                        nudgeParam(osc, "kParamVolume", amount * 0.5, rng);
                        nudgeParam(osc, "kParamFine", amount * 0.3, rng);
                        if (ed_.number(osc, "kParamUnison") > 1.0)
                            nudgeParam(osc, "kParamDetune", amount, rng);
                        if (oscIsWavetable(i))
                        {
                            nudgeParam(Module::wavetable(i), "kParamTablePos", amount, rng);
                            if (ed_.has(Module::wavetable(i), "kParamWarp"))
                                nudgeParam(Module::wavetable(i), "kParamWarp", amount, rng);
                            if (rng.chance(amount * 0.25))
                            {
                                const auto& tables = schema_.wavetables();
                                ed_.setWavetable(i, tables[rng.index(tables.size())]);
                            }
                        }
                    }
                    summary("Oscillators: varied");
                    break;
                case Group::NoiseSub:
                    for (int i = 3; i < 5; ++i)
                        if (oscEnabled(i))
                            nudgeParam(Module::osc(i), "kParamVolume", amount * 0.5, rng);
                    break;
                case Group::Filters:
                    for (int f = 0; f < 2; ++f)
                    {
                        if (!filterEnabled(f))
                            continue;
                        const auto flt = Module::filter(f);
                        nudgeParam(flt, "kParamFreq", amount, rng);
                        nudgeParam(flt, "kParamReso", amount * 0.6, rng);
                        nudgeParam(flt, "kParamDrive", amount * 0.6, rng);
                        if (rng.chance(amount * 0.2))
                            ed_.setText(flt, "kParamType", knobs_.pick("filter.type", rng, "MgL24"));
                    }
                    summary("Filters: varied");
                    break;
                case Group::Envelopes:
                    for (int e = 0; e < serum2::kNumEnvelopes; ++e)
                    {
                        const auto env = Module::env(e);
                        nudgeParam(env, "kParamAttack", amount, rng, true);
                        nudgeParam(env, "kParamDecay", amount, rng, true, 0.01);
                        nudgeParam(env, "kParamSustain", amount * 0.5, rng);
                        nudgeParam(env, "kParamRelease", amount, rng, true, 0.005);
                    }
                    nudgeRoutes(Group::Envelopes, amount * 0.5, rng);
                    summary("Envelopes: varied");
                    break;
                case Group::Lfos:
                    for (int l = 0; l < serum2::kNumLfos; ++l)
                    {
                        const auto lfo = Module::lfo(l);
                        if (!ed_.flag(lfo, "kParamBeatSync") && ed_.has(lfo, "kParamRate"))
                            ed_.set(lfo, "kParamRate", nudge(ed_.number(lfo, "kParamRate"), 0.05, 30.0, amount, rng, true));
                    }
                    nudgeRoutes(Group::Lfos, amount * 0.5, rng);
                    summary("LFOs: varied");
                    break;
                case Group::ModMatrix:
                    nudgeRoutes(Group::ModMatrix, amount * 0.5, rng);
                    break;
                case Group::Macros:
                    for (int m = 0; m < serum2::kNumMacros; ++m)
                        if (ed_.has(Module::macro(m), "kParamValue"))
                            nudgeParam(Module::macro(m), "kParamValue", amount, rng);
                    nudgeRoutes(Group::Macros, amount * 0.5, rng);
                    break;
                case Group::Fx:
                {
                    auto units = ed_.fxRack(0);
                    for (auto& unit : units)
                        if (const auto* table = schema_.fxParams(unit.type))
                            for (const auto& [key, def] : *table)
                            {
                                if (!unit.params.contains(key) || !unit.params[key].is_number() || !def.min || !def.max)
                                    continue;
                                if (key.rfind("kParamModuleCount", 0) == 0)
                                    continue;
                                unit.params[key] = nudge(unit.params[key].get<double>(), *def.min, *def.max, amount * 0.6, rng);
                            }
                    ed_.setFxRack(0, units);
                    summary("FX: varied");
                    break;
                }
                case Group::Global:
                    if (ed_.number(Module::global(), "kParamPortamentoTime") > 0.0)
                        nudgeParam(Module::global(), "kParamPortamentoTime", amount, rng, true, 0.001);
                    break;
            }
        }
    }

    // --------------------------------------------------------------- sanity

    void sanity()
    {
        // Something must make sound.
        bool anySource = false;
        for (int i = 0; i < 5; ++i)
            anySource = anySource || (oscEnabled(i) && ed_.number(Module::osc(i), "kParamVolume") > 0.05);
        if (!anySource)
        {
            ed_.setFlag(Module::osc(0), "kParamEnable", true);
            ed_.set(Module::osc(0), "kParamVolume", 0.75);
        }

        // A low-pass filter parked nearly shut with nothing opening it is silence.
        for (int f = 0; f < 2; ++f)
        {
            if (!filterEnabled(f))
                continue;
            const auto flt = Module::filter(f);
            const auto type = ed_.text(flt, "kParamType");
            const bool lowpass = !type.empty() && (type[0] == 'L' || type.rfind("Mg", 0) == 0 || type.rfind("Ladder", 0) == 0);
            bool opened = false;
            for (const int slot : ed_.usedModSlots())
                if (const auto r = ed_.modRoute(slot); r && r->dest.type == "VoiceFilter" && r->dest.id == f
                                                       && r->dest.paramName == "kParamFreq" && r->amount > 10.0)
                    opened = true;
            const double floor = opened ? 0.08 : 0.22;
            if (lowpass && ed_.number(flt, "kParamFreq") < floor)
                ed_.set(flt, "kParamFreq", floor);
        }

        // Sustained categories shouldn't die out.
        const double minSustain = knobs_.real("sanity.minSustain", *pickRng_, 0.0);
        if (settings_.mode != Mode::Mutate && ed_.number(Module::env(0), "kParamSustain") < minSustain)
            ed_.set(Module::env(0), "kParamSustain", minSustain);

        // Keep stacked unison from clipping: scale master volume by a rough
        // loudness estimate.
        double loudness = 0.0;
        for (int i = 0; i < 5; ++i)
            if (oscEnabled(i))
                loudness += ed_.number(Module::osc(i), "kParamVolume") * (1.0 + 0.08 * (ed_.number(Module::osc(i), "kParamUnison") - 1.0));
        for (const auto& unit : ed_.fxRack(0))
            if (unit.type == "FXDistortion")
                loudness *= 1.2;
        if (!settings_.isLocked(Group::Global) && settings_.mode != Mode::Mutate)
            ed_.set(Module::global(), "kParamMasterVolume", std::clamp(0.5 * 1.3 / std::max(loudness, 0.5), 0.25, 0.5));

        // Heavy unison times many voices is a CPU trap.
        if (!ed_.flag(Module::global(), "kParamMonoToggle"))
        {
            double maxUnison = 1.0;
            for (int i = 0; i < 3; ++i)
                if (oscEnabled(i))
                    maxUnison = std::max(maxUnison, ed_.number(Module::osc(i), "kParamUnison"));
            const double voices = ed_.number(Module::global(), "kParamPolyCount");
            if (maxUnison * voices > 96.0)
                ed_.set(Module::global(), "kParamPolyCount", std::max(1.0, std::floor(96.0 / maxUnison)));
        }
    }

    // ------------------------------------------------------------- metadata

    std::string pickWord(const Json& table, const std::string& key, Rng& rng, double ownChance) const
    {
        const bool useOwn = table.contains(key) && !table[key].empty() && rng.chance(ownChance);
        const auto& list = useOwn ? table[key] : table.value("any", Json::array());
        if (list.empty())
            return {};
        return list[rng.index(list.size())].get<std::string>();
    }

    std::string makeName()
    {
        auto& rng = *nameRng_;
        const auto& names = content_.names();
        if (settings_.mode == Mode::Mutate)
        {
            auto baseName = baseData_.value("presetName", std::string("Preset"));
            if (const auto tilde = baseName.find(" ~"); tilde != std::string::npos)
                baseName.resize(tilde);
            return baseName + " ~" + std::to_string(settings_.seed % 1000);
        }
        const auto prefixes = names.value("prefix", Json::object());
        const auto prefix = prefixes.value(result_.category, std::string("SY"));
        const auto adjectives = names.value("adjectives", Json::object());
        const auto nouns = names.value("nouns", Json::object());
        auto adjective = pickWord(adjectives, result_.genre, rng, 0.7);
        if (adjective.empty())
            adjective = pickWord(adjectives, "any", rng, 1.0);
        const auto noun = pickWord(nouns, result_.category, rng, 0.8);
        std::string name = prefix;
        if (!adjective.empty())
            name += " " + adjective;
        if (!noun.empty())
            name += " " + noun;
        return name;
    }

    void finish()
    {
        if (settings_.mode != Mode::Mutate)
            summarizeRoutes();
        result_.name = makeName();
        auto& meta = preset_.metadata;
        auto& data = preset_.data;

        std::string description = "Generated by Baastik Serum Preset Generator.";
        const auto labelOf = [&](const auto& list, const std::string& id) {
            for (const auto& item : list)
                if (item.id == id)
                    return item.name;
            return id;
        };
        const auto categoryName = labelOf(content_.categories(), result_.category);
        const auto genreName = labelOf(content_.genres(), result_.genre);
        description += " " + std::string(modeId(settings_.mode)) + " / " + categoryName;
        if (!result_.genre.empty())
            description += " / " + genreName;
        description += " / seed " + std::to_string(settings_.seed);

        Json tags = Json::array();
        bool multisample = false;
        for (int i = 0; i < 3; ++i)
            multisample = multisample || (oscEnabled(i) && !oscIsWavetable(i));
        tags.push_back(multisample ? "Multisample" : "Wavetable");
        tags.push_back(ed_.flag(Module::global(), "kParamMonoToggle") ? "Mono" : "Poly");
        tags.push_back("Baastik");
        if (settings_.mode == Mode::Guided)
        {
            tags.push_back(categoryName);
            if (!result_.genre.empty())
                tags.push_back(genreName);
        }

        for (auto* j : { &meta, &data })
        {
            (*j)["presetName"] = result_.name;
            (*j)["presetAuthor"] = settings_.author;
            (*j)["presetDescription"] = description;
            (*j)["tags"] = tags;
        }
        result_.preset = std::move(preset_);
    }

    const ContentLibrary& content_;
    const Schema& schema_;
    const Settings& settings_;
    Json baseData_;
    PatchEditor baseEd_;
    Preset preset_;
    PatchEditor ed_;
    KnobSet knobs_;
    Json recipe_ = Json::object();
    Result result_;

    std::array<Rng, kAllGroups.size()> stageRngs_ { Rng(0), Rng(0), Rng(0), Rng(0), Rng(0), Rng(0), Rng(0), Rng(0), Rng(0) };
    std::unique_ptr<Rng> nameRng_;
    std::unique_ptr<Rng> pickRng_;
    std::array<bool, serum2::kNumEnvelopes> envUsed_ {};
    std::array<bool, serum2::kNumLfos> lfoUsed_ {};
};
} // namespace

std::string_view groupId(Group g)
{
    return kGroupIds[static_cast<std::size_t>(g)];
}

std::string_view groupLabel(Group g)
{
    return kGroupLabels[static_cast<std::size_t>(g)];
}

std::optional<Group> groupFromId(std::string_view id)
{
    for (const auto g : kAllGroups)
        if (groupId(g) == id)
            return g;
    return std::nullopt;
}

std::string_view modeId(Mode m)
{
    switch (m)
    {
        case Mode::Random:
            return "random";
        case Mode::Guided:
            return "guided";
        case Mode::Mutate:
            return "mutate";
    }
    return "guided";
}

std::optional<Mode> modeFromId(std::string_view id)
{
    for (const auto m : { Mode::Random, Mode::Guided, Mode::Mutate })
        if (modeId(m) == id)
            return m;
    return std::nullopt;
}

Generator::Generator(const ContentLibrary& content, const Schema& schema)
    : content_(content), schema_(schema)
{
}

Result Generator::generate(const Settings& settings, const Preset* base) const
{
    const auto init = Schema::initPreset();
    Rng rng(settings.seed);
    Build build(content_, schema_, settings, base ? *base : init, rng);
    return build.run();
}

std::string presetFileName(const std::string& presetName)
{
    std::string out;
    for (const char c : presetName)
        if (std::string_view("<>:\"/\\|?*").find(c) == std::string_view::npos && static_cast<unsigned char>(c) >= 32)
            out += c;
    while (!out.empty() && (out.back() == ' ' || out.back() == '.'))
        out.pop_back();
    if (out.empty())
        out = "Preset";
    return out + ".SerumPreset";
}
} // namespace serumgen
