// Validates the JSON content (base, categories, genres, recipes, macro
// themes) against the Serum schema so a typo in a profile fails CI instead of
// producing a broken preset.

#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <algorithm>
#include <regex>
#include <set>

#include "serum2/Schema.h"
#include "serumgen/Content.h"

using serum2::Json;
using serumgen::ContentLibrary;

namespace
{
const auto& schema()
{
    return serum2::Schema::builtin();
}

std::vector<std::string> choiceKeys(const Json& knob)
{
    std::vector<std::string> keys;
    if (knob.is_string())
        keys.push_back(knob.get<std::string>());
    else if (knob.is_object() && knob.contains("choose"))
        for (const auto& [k, w] : knob["choose"].items())
            keys.push_back(k);
    return keys;
}

bool enumContains(std::string_view module, std::string_view key, const std::string& value)
{
    const auto* def = schema().param(module, key);
    return def && std::find(def->enumValues.begin(), def->enumValues.end(), value) != def->enumValues.end();
}

bool validDest(const std::string& name)
{
    if (name.rfind("fx:", 0) == 0)
    {
        const auto dot = name.find('.');
        return dot != std::string::npos && schema().fxModDest(name.substr(3, dot - 3), 0, 0, name.substr(dot + 1)).has_value();
    }
    auto concrete = std::regex_replace(name, std::regex("\\*"), "0");
    return schema().modDest(concrete).has_value();
}

// "osc1.warpMode" -> "osc.warpMode" (the knob's family, for validation).
std::string family(const std::string& knob)
{
    return std::regex_replace(knob, std::regex("^([a-zA-Z]+?)(\\d+|B)\\."), "$1.");
}

void checkKnobs(const Json& knobs, const std::string& where, const ContentLibrary& content)
{
    static const std::set<std::string> lfoShapes { "triangle", "sine",  "saw_up", "saw_down", "square", "pulse",
                                                   "decay",    "stairs", "gate",  "random",   "lorenz", "rossler" };
    for (const auto& [name, knob] : knobs.items())
    {
        INFO(where << ": " << name << " = " << knob.dump());
        const auto f = family(name);
        const auto keys = choiceKeys(knob);
        const auto each = [&](const std::function<bool(const std::string&)>& ok) {
            for (const auto& k : keys)
            {
                INFO("value " << k);
                CHECK(ok(k));
            }
        };

        if (f == "filter.type")
            each([](const auto& v) { return enumContains("VoiceFilter", "kParamType", v); });
        else if (f == "osc.warpMode")
            each([](const auto& v) { return enumContains("WTOsc", "kParamWarpMenu", v); });
        else if (f == "osc.wavetable")
            each([](const auto& v) { return schema().wavetable(v) != nullptr; });
        else if (f == "osc.multisample")
            each([](const auto& v) { return schema().multisample(v) != nullptr; });
        else if (f == "osc.wtTag")
            each([](const auto& v) {
                const auto& t = schema().wavetables();
                return std::any_of(t.begin(), t.end(), [&](const auto& w) { return w.hasTag(v); });
            });
        else if (f == "osc.msTag")
            each([](const auto& v) {
                const auto& m = schema().multisamples();
                return std::any_of(m.begin(), m.end(), [&](const auto& s) { return s.hasTag(v); });
            });
        else if (f == "osc.engine")
            each([](const auto& v) { return v == "wavetable" || v == "multisample"; });
        else if (f == "osc.frame")
            each([](const auto& v) { return v == "sine" || v == "saw"; });
        else if (f == "sub.shape")
            each([](const auto& v) { return v == "sine" || enumContains("SubOsc", "kParamShape", v); });
        else if (f == "noise.type")
            each([](const auto& v) { return enumContains("NoiseOsc", "kParamNoiseType", v); });
        else if (f == "lfo.shape")
            each([&](const auto& v) { return lfoShapes.count(v) > 0; });
        else if (f == "lfo.mode")
            each([](const auto& v) { return v == "Free" || v == "Retrig" || v == "Envelope"; });
        else if (f == "lfo.syncRate")
            each([](const auto& v) { return schema().lfoSyncRates().count(v) > 0; });
        else if (f == "env.dest" || f == "lfo.dest" || f == "mod.dest")
            each([](const auto& v) { return validDest(v); });
        else if (f == "mod.source")
            each([](const auto& v) { return schema().modSource(v).has_value(); });
        else if (name == "recipe")
            each([&](const auto& v) { return v == "none" || !content.recipe(v).empty(); });
        else if (name == "macro.theme")
            each([&](const auto& v) { return content.macroThemes().contains(v); });
        else if (name.rfind("fx.", 0) == 0)
        {
            const auto rest = name.substr(3);
            const auto dot = rest.find('.');
            const auto type = rest.substr(0, dot);
            if (type == "max" || type == "shuffle")
                continue;
            CHECK(schema().fxTypeId(type).has_value());
            if (dot != std::string::npos)
            {
                const auto param = rest.substr(dot + 1);
                const auto* def = schema().param(type, param);
                CHECK(def != nullptr);
                if (def && !def->enumValues.empty())
                    each([&](const auto& v) { return enumContains(type, param, v); });
            }
        }
    }
}
} // namespace

TEST_CASE("content knobs reference real Serum values")
{
    const auto& content = ContentLibrary::builtin();
    checkKnobs(content.base(), "base", content);
    for (const auto& c : content.categories())
        checkKnobs(content.categoryData(c.id)["knobs"], "category " + c.id, content);
    for (const auto& g : content.genres())
    {
        const auto& genre = content.genreData(g.id);
        checkKnobs(genre["knobs"], "genre " + g.id, content);
        const auto perCategory = genre.value("perCategory", Json::object());
        for (const auto& [cat, knobs] : perCategory.items())
        {
            INFO("genre " << g.id << " perCategory " << cat);
            CHECK(!content.categoryData(cat).empty());
            checkKnobs(knobs, "genre " + g.id + "/" + cat, content);
        }
        for (const auto& [cat, weight] : genre["categories"].items())
        {
            INFO("genre " << g.id << " lists category " << cat);
            CHECK(!content.categoryData(cat).empty());
        }
    }
    for (const auto& id : content.recipeIds())
    {
        const auto& recipe = content.recipe(id);
        checkKnobs(recipe.value("knobs", Json::object()), "recipe " + id, content);
        const auto routes = recipe.value("routes", Json::array());
        for (const auto& route : routes)
        {
            INFO("recipe " << id << " route " << route.dump());
            CHECK(schema().modSource(route["source"].get<std::string>()).has_value());
            CHECK(validDest(route["dest"].get<std::string>()));
        }
    }
    for (const auto& [id, theme] : content.macroThemes().items())
        for (const auto& target : theme["targets"])
        {
            INFO("macro theme " << id << " target " << target.dump());
            CHECK(validDest(target["dest"].get<std::string>()));
        }
}

TEST_CASE("content bundle round-trips")
{
    const auto bundle = ContentLibrary::builtinBundle();
    const auto lib = ContentLibrary::fromBundle(bundle);
    CHECK(lib.categories().size() == ContentLibrary::builtin().categories().size());
    CHECK(lib.genres().size() == ContentLibrary::builtin().genres().size());
    CHECK(lib.version() >= 1);

    auto future = bundle;
    future["minEngine"] = ContentLibrary::kEngineVersion + 1;
    CHECK_THROWS(ContentLibrary::fromBundle(future));
}
