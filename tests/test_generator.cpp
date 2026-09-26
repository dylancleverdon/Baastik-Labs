#include <catch2/catch_test_macros.hpp>

#include <set>
#include <algorithm>

#include "serum2/Patch.h"
#include "serum2/PresetFile.h"
#include "serumgen/Content.h"
#include "serumgen/Generator.h"

using namespace serumgen;
using serum2::Json;
using serum2::Module;
using serum2::PatchEditor;

namespace
{
Settings guided(std::string category, std::string genre, std::uint64_t seed, double chaos = 0.0)
{
    Settings s;
    s.mode = Mode::Guided;
    s.category = std::move(category);
    s.genre = std::move(genre);
    s.seed = seed;
    s.chaos = chaos;
    return s;
}

// Every plainParams value must use Serum's wire types (doubles or enum
// strings) and sit inside the schema's range.
void checkWireTypes(const Json& node, const std::string& schemaModule, const serum2::Schema& schema, const std::string& where)
{
    if (!node.is_object() || !node.contains("plainParams") || !node["plainParams"].is_object())
        return;
    for (const auto& [key, value] : node["plainParams"].items())
    {
        INFO(where << "." << key << " = " << value.dump());
        CHECK((value.is_number_float() || value.is_string()));
        const auto* def = schema.param(schemaModule, key);
        if (!def)
            continue;
        if (value.is_number() && def->min)
            CHECK(value.get<double>() >= *def->min - 1e-9);
        if (value.is_number() && def->max)
            CHECK(value.get<double>() <= *def->max + 1e-9);
        if (value.is_string() && !def->enumValues.empty())
            CHECK(std::find(def->enumValues.begin(), def->enumValues.end(), value.get<std::string>()) != def->enumValues.end());
    }
}

void checkPreset(const serum2::Preset& preset)
{
    const auto& schema = serum2::Schema::builtin();
    const auto& d = preset.data;
    for (int i = 0; i < 5; ++i)
        checkWireTypes(d["Oscillator" + std::to_string(i)], "Oscillator", schema, "Oscillator" + std::to_string(i));
    for (int i = 0; i < 3; ++i)
        checkWireTypes(d["Oscillator" + std::to_string(i)]["WTOsc" + std::to_string(i)], "WTOsc", schema, "WTOsc");
    for (int i = 0; i < 2; ++i)
        checkWireTypes(d["VoiceFilter" + std::to_string(i)], "VoiceFilter", schema, "VoiceFilter");
    for (int i = 0; i < 4; ++i)
        checkWireTypes(d["Env" + std::to_string(i)], "Env", schema, "Env");
    for (int i = 0; i < 10; ++i)
        checkWireTypes(d["LFO" + std::to_string(i)], "LFO", schema, "LFO");
    for (int i = 0; i < 8; ++i)
        checkWireTypes(d["Macro" + std::to_string(i)], "Macro", schema, "Macro");
    checkWireTypes(d["Global0"], "Global", schema, "Global0");
    for (const auto& unit : d["FXRack0"]["FX"])
    {
        const auto type = schema.fxTypeName(unit["type"].get<int>());
        REQUIRE(type);
        checkWireTypes(unit[*type], *type, schema, *type);
    }

    // Round trip through the file format.
    const auto bytes = serum2::encodePreset(preset);
    CHECK(serum2::decodePreset(bytes).data == preset.data);
}

bool hasRouteFrom(const serum2::Preset& preset, std::string_view source)
{
    auto data = preset.data;
    PatchEditor ed(data);
    const auto id = serum2::Schema::builtin().modSource(source);
    for (const int slot : ed.usedModSlots())
        if (ed.modRoute(slot)->source == *id)
            return true;
    return false;
}
} // namespace

TEST_CASE("every category and genre produces valid presets")
{
    const Generator gen;
    const auto& content = gen.content();
    std::uint64_t seed = 1;
    for (const auto& category : content.categories())
    {
        INFO("category " << category.id);
        for (int i = 0; i < 4; ++i)
            checkPreset(gen.generate(guided(category.id, "none", seed++, 0.3)).preset);
    }
    for (const auto& genre : content.genres())
    {
        INFO("genre " << genre.id);
        for (int i = 0; i < 4; ++i)
        {
            const auto r = gen.generate(guided("any", genre.id, seed++, 0.3));
            CHECK(r.genre == genre.id);
            CHECK(!r.name.empty());
            checkPreset(r.preset);
        }
    }
    for (int i = 0; i < 20; ++i)
    {
        Settings s;
        s.mode = Mode::Random;
        s.seed = seed++;
        checkPreset(gen.generate(s).preset);
    }
}

TEST_CASE("the same seed gives byte-identical presets")
{
    const Generator gen;
    const auto a = gen.generate(guided("bass", "riddim", 1234, 0.4));
    const auto b = gen.generate(guided("bass", "riddim", 1234, 0.4));
    CHECK(serum2::encodePreset(a.preset) == serum2::encodePreset(b.preset));
    CHECK(a.name == b.name);
    const auto c = gen.generate(guided("bass", "riddim", 1235, 0.4));
    CHECK(c.preset.data != a.preset.data);
}

TEST_CASE("category profiles are respected at zero chaos")
{
    const Generator gen;
    for (std::uint64_t seed = 1; seed <= 25; ++seed)
    {
        INFO("seed " << seed);
        {
            auto r = gen.generate(guided("perc", "none", seed));
            PatchEditor ed(r.preset.data);
            CHECK(ed.number(Module::env(0), "kParamSustain") == 0.0);
        }
        {
            auto r = gen.generate(guided("pad", "ambient", seed));
            PatchEditor ed(r.preset.data);
            CHECK(ed.number(Module::env(0), "kParamAttack") >= 0.3);
            CHECK(ed.number(Module::env(0), "kParamSustain") >= 0.5);
        }
        {
            auto r = gen.generate(guided("bass", "none", seed));
            PatchEditor ed(r.preset.data);
            CHECK(ed.number(Module::osc(0), "kParamOctave") <= 0.0);
        }
        {
            auto r = gen.generate(guided("sub_bass", "trap", seed));
            PatchEditor ed(r.preset.data);
            CHECK(ed.flag(Module::global(), "kParamMonoToggle"));
        }
    }
}

TEST_CASE("acoustic genres play expressively and stay clean")
{
    const Generator gen;
    int expressive = 0;
    const int runs = 30;
    for (std::uint64_t seed = 1; seed <= runs; ++seed)
    {
        const auto r = gen.generate(guided("epiano", "jazz", seed));
        expressive += hasRouteFrom(r.preset, "velocity") ? 1 : 0;
        for (const auto& unit : r.preset.data["FXRack0"]["FX"])
            if (unit["type"] == 0) // saturation stays gentle
                CHECK(unit["FXDistortion"]["plainParams"].value("kParamDrive", 0.0) <= 40.0);
    }
    CHECK(expressive >= runs * 8 / 10);
}

TEST_CASE("locked groups are copied from the base preset")
{
    const Generator gen;
    const auto first = gen.generate(guided("lead", "synthwave", 99, 0.2));

    auto s = guided("bass", "riddim", 7, 0.5);
    s.setLocked(Group::Oscillators, true);
    s.setLocked(Group::Fx, true);
    const auto second = gen.generate(s, &first.preset);

    for (int i = 0; i < 3; ++i)
    {
        const auto key = "Oscillator" + std::to_string(i);
        CHECK(second.preset.data[key] == first.preset.data[key]);
    }
    CHECK(second.preset.data["FXRack0"] == first.preset.data["FXRack0"]);
    CHECK(second.preset.data["Env0"] != first.preset.data["Env0"]);
}

TEST_CASE("mutate varies a preset without replacing it")
{
    const Generator gen;
    const auto base = gen.generate(guided("pad", "ambient", 5, 0.1));
    Settings s;
    s.mode = Mode::Mutate;
    s.seed = 77;
    s.chaos = 0.3;
    s.setLocked(Group::Fx, true);
    const auto varied = gen.generate(s, &base.preset);
    checkPreset(varied.preset);
    CHECK(varied.preset.data != base.preset.data);
    CHECK(varied.preset.data["FXRack0"] == base.preset.data["FXRack0"]);
    CHECK(varied.name.find(" ~") != std::string::npos);
    // Wavetable choices mostly survive a light mutation.
    CHECK(varied.preset.data["Oscillator0"]["WTOsc0"].value("relativePathToWT", "")
          == base.preset.data["Oscillator0"]["WTOsc0"].value("relativePathToWT", ""));
}

TEST_CASE("file names are safe on every OS")
{
    CHECK(presetFileName("BA Wonky: Growl?") == "BA Wonky Growl.SerumPreset");
    CHECK(presetFileName("..") == "Preset.SerumPreset");
}
