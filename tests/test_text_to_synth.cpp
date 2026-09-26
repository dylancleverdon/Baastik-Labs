#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <fstream>
#include <sstream>

#include "Fixtures.h"
#include "serum2/Patch.h"
#include "serum2/Schema.h"
#include "tts/Content.h"
#include "tts/Describe.h"
#include "tts/Edits.h"
#include "tts/Registration.h"
#include "tts/Server.h"
#include "tts/Store.h"

using namespace tts;
using serum2::Json;
using serum2::Module;
using Catch::Approx;

namespace
{
const TargetResolver& resolver()
{
    return PresetEditor::defaultResolver();
}

EditReport edit(serum2::Preset& p, const Json& edits)
{
    return PresetEditor(p).apply(edits);
}

double number(serum2::Preset& p, const Module& m, const char* key)
{
    return serum2::PatchEditor(p.data).number(m, key);
}

// A scratch folder per test, removed afterwards.
struct TempDir
{
    std::filesystem::path path;
    explicit TempDir(const std::string& name)
        : path(std::filesystem::temp_directory_path() / ("tts-test-" + name + "-" + std::to_string(std::rand())))
    {
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }
    ~TempDir() { std::filesystem::remove_all(path); }
};

Paths tempPaths(const TempDir& dir)
{
    return { dir.path / "presets", dir.path / "data" };
}

// Sets an environment variable for the lifetime of the object.
struct ScopedEnv
{
    std::string name;
    std::string old;
    bool had = false;
    ScopedEnv(const char* n, const std::string& value)
        : name(n)
    {
        if (const char* v = std::getenv(n))
        {
            had = true;
            old = v;
        }
        set(value);
    }
    ~ScopedEnv()
    {
        if (had)
            set(old);
        else
            unset();
    }
    void set(const std::string& v)
    {
#if defined(_WIN32)
        _putenv_s(name.c_str(), v.c_str());
#else
        setenv(name.c_str(), v.c_str(), 1);
#endif
    }
    void unset()
    {
#if defined(_WIN32)
        _putenv_s(name.c_str(), "");
#else
        unsetenv(name.c_str());
#endif
    }
};
} // namespace

TEST_CASE("targets resolve from friendly names")
{
    const auto& r = resolver();
    auto t = r.resolve("filter1.cutoff");
    CHECK(t.kind == Target::Kind::Module);
    CHECK(t.module.schemaModule == "VoiceFilter");
    CHECK(t.module.slot == 0);
    CHECK(t.key == "kParamFreq");
    CHECK(t.label == "filter1.cutoff");

    CHECK(r.resolve("Filter 2.Resonance").key == "kParamReso");
    CHECK(r.resolve("filter2.res").module.slot == 1);
    CHECK(r.resolve("oscB.level").module.path == Module::osc(1).path);
    CHECK(r.resolve("oscA.wt_pos").module.schemaModule == "WTOsc");
    CHECK(r.resolve("osc3.unison").module.slot == 2);
    CHECK(r.resolve("noise.color").module.schemaModule == "NoiseOsc");
    CHECK(r.resolve("sub.level").module.path == Module::osc(4).path);
    CHECK(r.resolve("env1.release").key == "kParamRelease");
    CHECK(r.resolve("lfo10.rate").module.slot == 9);
    CHECK(r.resolve("macro8.value").module.slot == 7);
    CHECK(r.resolve("global.glide").key == "kParamPortamentoTime");
    // Raw kParam names work too.
    CHECK(r.resolve("filter1.kParamDrive").key == "kParamDrive");

    auto fx = r.resolve("fx.reverb.mix");
    CHECK(fx.kind == Target::Kind::Fx);
    CHECK(fx.fxType == "FXReverb");
    CHECK(fx.key == "kParamWet");
    CHECK(r.resolve("reverb.size").fxType == "FXReverb");
    CHECK(r.resolve("fx.distortion2.drive").fxInstance == 1);
    CHECK(r.resolve("fx.filter.cutoff").fxType == "FXFilter");
    CHECK(r.resolve("fx.eq.high_gain").key == "kParamGain2");

    CHECK_THROWS_AS(r.resolve("filter1"), std::invalid_argument);
    CHECK_THROWS_AS(r.resolve("filter3.cutoff"), std::invalid_argument);
    CHECK_THROWS_AS(r.resolve("filter1.nonsense"), std::invalid_argument);
    CHECK_THROWS_AS(r.resolve("fx.reverb.nonsense"), std::invalid_argument);
}

TEST_CASE("mod sources and destinations resolve")
{
    const auto& r = resolver();
    CHECK(r.modSource("lfo1") == serum2::Schema::builtin().modSource("lfo0"));
    CHECK(r.modSource("macro8") == serum2::Schema::builtin().modSource("macro7"));
    CHECK(r.modSource("modwheel") == serum2::Schema::builtin().modSource("mod_wheel"));
    CHECK(r.modSourceLabel(*r.modSource("lfo3")) == "lfo3");
    CHECK(r.modSourceLabel(*r.modSource("velocity")) == "velocity");
    CHECK_FALSE(r.modSource("nonsense"));

    const auto dest = r.modDest(r.resolve("filter1.cutoff"));
    REQUIRE(dest);
    CHECK(dest->type == "VoiceFilter");
    CHECK(dest->paramName == "kParamFreq");
    const auto wt = r.modDest(r.resolve("oscB.wt_pos"));
    REQUIRE(wt);
    CHECK(wt->type == "WTOsc");
    CHECK(wt->id == 1);
    const auto wet = r.modDest(r.resolve("fx.reverb.mix"), 0, 2);
    REQUIRE(wet);
    CHECK(wet->id == 2);
}

TEST_CASE("set, add and scale edit parameters with clamping")
{
    auto p = serum2::Schema::initPreset();
    auto report = edit(p, Json::parse(R"([
        {"target": "filter1.on", "set": true},
        {"target": "filter1.cutoff", "set": 0.4},
        {"target": "filter1.cutoff", "add": -0.1},
        {"target": "filter1.resonance", "set": 40},
        {"target": "filter1.resonance", "scale": 0.5},
        {"target": "filter1.type", "set": "mgl24"},
        {"target": "env1.release", "set": 99}
    ])"));
    CHECK(number(p, Module::filter(0), "kParamFreq") == Approx(0.3));
    CHECK(number(p, Module::filter(0), "kParamReso") == Approx(20));
    CHECK(serum2::PatchEditor(p.data).text(Module::filter(0), "kParamType") == "MgL24");
    CHECK(number(p, Module::env(0), "kParamRelease") == Approx(32)); // clamped to max
    CHECK(report.changes.size() == 7);
    CHECK(report.warnings.size() == 1); // the clamp

    // Values are stored as doubles, never CBOR bools or ints.
    CHECK(p.data["VoiceFilter0"]["plainParams"]["kParamEnable"].is_number_float());

    // Writing a default removes the key again (Serum treats absence specially).
    edit(p, Json::parse(R"([{"target": "filter1.type", "set": "MgL12"}])"));
    CHECK_FALSE(p.data["VoiceFilter0"]["plainParams"].contains("kParamType"));
}

TEST_CASE("bad edits throw and name the edit")
{
    auto p = serum2::Schema::initPreset();
    const auto original = p;
    CHECK_THROWS_AS(edit(p, Json::parse(R"([{"target": "filter1.type", "set": "nope"}])")), std::invalid_argument);
    CHECK_THROWS_AS(edit(p, Json::parse(R"([{"target": "filter1.cutoff", "set": "loud"}])")), std::invalid_argument);
    CHECK_THROWS_AS(edit(p, Json::parse(R"([{"target": "filter1.type", "add": 1}])")), std::invalid_argument);
    CHECK_THROWS_AS(edit(p, Json::parse(R"([{"target": "filter1.cutoff", "set": 1, "add": 1}])")), std::invalid_argument);
    CHECK_THROWS_AS(edit(p, Json::parse(R"([{"frobnicate": 1}])")), std::invalid_argument);
    CHECK_THROWS_AS(edit(p, Json::parse(R"([{"target": "fx.reverb.mix", "set": 10}])")), std::invalid_argument);
    try
    {
        edit(p, Json::parse(R"([{"target": "filter1.on", "set": true}, {"target": "filter1.bogus", "set": 1}])"));
        FAIL("expected a throw");
    }
    catch (const std::invalid_argument& e)
    {
        CHECK(std::string(e.what()).find("Edit 2") != std::string::npos);
    }
}

TEST_CASE("warnings point at inactive modules")
{
    auto p = serum2::Schema::initPreset();
    const auto report = edit(p, Json::parse(R"([{"target": "filter1.cutoff", "set": 0.3}, {"target": "lfo2.rate", "set": "1/8"}])"));
    REQUIRE(report.warnings.size() == 2);
    CHECK(report.warnings[0].find("filter1 is off") != std::string::npos);
    CHECK(report.warnings[1].find("lfo2 isn't routed") != std::string::npos);
}

TEST_CASE("FX units are added, edited and removed in place")
{
    auto p = serum2::Schema::initPreset();
    edit(p, Json::parse(R"([
        {"add_fx": "distortion", "params": {"drive": 40}},
        {"add_fx": "reverb", "params": {"size": 60}},
        {"mod": {"source": "macro1", "target": "fx.reverb.mix", "amount": 50}}
    ])"));
    auto& list = p.data["FXRack0"]["FX"];
    REQUIRE(list.size() == 2);
    CHECK(list[1]["type"] == *serum2::Schema::builtin().fxTypeId("FXReverb"));
    // A new reverb starts at its usual mix, not the 100% an absent key means.
    CHECK(list[1]["FXReverb"]["plainParams"]["kParamWet"].get<double>() == Approx(30));
    CHECK(list[0]["FXDistortion"]["plainParams"]["kParamDrive"].get<double>() == Approx(40));

    // 100% wet is written by leaving the key out, as Serum does.
    edit(p, Json::parse(R"([{"target": "fx.distortion.mix", "set": 100}])"));
    CHECK_FALSE(list[0]["FXDistortion"]["plainParams"].contains("kParamWet"));
    CHECK(PresetEditor(p).read(resolver().resolve("fx.distortion.mix")).get<double>() == Approx(100));

    // Relative edits on FX.
    edit(p, Json::parse(R"([{"target": "fx.reverb.mix", "scale": 0.5}])"));
    CHECK(list[1]["FXReverb"]["plainParams"]["kParamWet"].get<double>() == Approx(15));

    // Mod routes into later units follow them when units move.
    serum2::PatchEditor ed(p.data);
    REQUIRE(ed.usedModSlots().size() == 1);
    CHECK(ed.modRoute(ed.usedModSlots()[0])->dest.id == 1);
    edit(p, Json::parse(R"([{"add_fx": "eq", "position": 0}])"));
    CHECK(ed.modRoute(ed.usedModSlots()[0])->dest.id == 2);
    edit(p, Json::parse(R"([{"remove_fx": "distortion"}])"));
    CHECK(ed.modRoute(ed.usedModSlots()[0])->dest.id == 1);
    edit(p, Json::parse(R"([{"remove_fx": "reverb"}])"));
    CHECK(ed.usedModSlots().empty()); // the route into the removed unit is gone
    CHECK(p.data["FXRack0"]["FX"].size() == 1);

    // Unknown params on a new unit fail the batch.
    CHECK_THROWS_AS(edit(p, Json::parse(R"([{"add_fx": "reverb", "params": {"nope": 1}}])")), std::invalid_argument);
}

TEST_CASE("existing FX data survives edits")
{
    auto p = serum2::Schema::initPreset();
    edit(p, Json::parse(R"([{"add_fx": "hyper", "params": {"rate": 20}}])"));
    auto& unit = p.data["FXRack0"]["FX"][0];
    unit["FXHyperD"]["lfo"] = Json::array({ 0.1, 0.2 }); // per-unit data Serum stores
    edit(p, Json::parse(R"([{"target": "fx.hyper.mix", "set": 40}])"));
    CHECK(unit["FXHyperD"]["lfo"] == Json::array({ 0.1, 0.2 }));
    CHECK(unit.contains("kUIParamMixOrGainDimE"));
}

TEST_CASE("mod routes are added, updated and removed")
{
    auto p = serum2::Schema::initPreset();
    edit(p, Json::parse(R"([{"mod": {"source": "lfo1", "target": "filter1.cutoff", "amount": 30}}])"));
    serum2::PatchEditor ed(p.data);
    REQUIRE(ed.usedModSlots().size() == 1);
    edit(p, Json::parse(R"([{"mod": {"source": "lfo1", "target": "filter1.cutoff", "amount": 150, "bipolar": true}}])"));
    REQUIRE(ed.usedModSlots().size() == 1);
    const auto route = ed.modRoute(ed.usedModSlots()[0]);
    CHECK(route->amount == Approx(100));
    CHECK(route->bipolar);
    edit(p, Json::parse(R"([{"mod": {"source": "env2", "target": "oscA.wt_pos", "amount": -20}}])"));
    CHECK(ed.usedModSlots().size() == 2);
    edit(p, Json::parse(R"([{"unmod": {"target": "filter1.cutoff"}}])"));
    CHECK(ed.usedModSlots().size() == 1);
    const auto report = edit(p, Json::parse(R"([{"unmod": {"source": "lfo9"}}])"));
    CHECK(report.warnings.size() == 1);
    CHECK_THROWS_AS(edit(p, Json::parse(R"([{"mod": {"source": "lfo1", "target": "filter1.type", "amount": 10}}])")),
                    std::invalid_argument);
}

TEST_CASE("LFO rates, shapes, wavetables and names")
{
    auto p = serum2::Schema::initPreset();
    serum2::PatchEditor ed(p.data);
    edit(p, Json::parse(R"([{"target": "lfo1.rate", "set": "1/8"}])"));
    CHECK(ed.flag(Module::lfo(0), "kParamBeatSync"));
    CHECK(ed.number(Module::lfo(0), "kParamRate") == Approx(serum2::Schema::builtin().lfoSyncRates().at("1/8")));
    edit(p, Json::parse(R"([{"target": "lfo1.hz", "set": 2.5}])"));
    CHECK_FALSE(ed.flag(Module::lfo(0), "kParamBeatSync"));
    CHECK(ed.number(Module::lfo(0), "kParamRate") == Approx(2.5));
    CHECK_THROWS_AS(edit(p, Json::parse(R"([{"target": "lfo1.rate", "set": "1/7"}])")), std::invalid_argument);

    edit(p, Json::parse(R"([{"lfo_shape": {"lfo": 1, "shape": "sine"}}])"));
    CHECK(p.data["LFO0"]["curveData"]["numPoints"] == 16);
    edit(p, Json::parse(R"([{"target": "lfo1.shape", "set": "random"}])"));
    CHECK(ed.text(Module::lfo(0), "kParamType") == "RandomSH");
    edit(p, Json::parse(R"([{"lfo_shape": {"lfo": 1, "shape": "square"}}])"));
    CHECK_FALSE(ed.has(Module::lfo(0), "kParamType"));
    CHECK_THROWS_AS(edit(p, Json::parse(R"([{"lfo_shape": {"lfo": 1, "shape": "wiggle"}}])")), std::invalid_argument);

    edit(p, Json::parse(R"([{"wavetable": {"osc": "B", "table": "default", "frame": "sine"}}])"));
    CHECK(ed.flag(Module::osc(1), "kParamEnable"));
    CHECK(ed.wavetablePath(1) == "S2 Tables/Default Shapes.wav");
    CHECK(ed.number(Module::wavetable(1), "kParamTablePos") == Approx(32.875));
    CHECK_THROWS_AS(edit(p, Json::parse(R"([{"wavetable": {"osc": "B", "table": "nope"}}])")), std::invalid_argument);

    edit(p, Json::parse(R"([{"target": "macro2.name", "set": "Space"}, {"rename": "Test Patch"}])"));
    CHECK(ed.macroName(1) == "Space");
    CHECK(p.metadata["presetName"] == "Test Patch");
    CHECK(p.data["presetName"] == "Test Patch");
}

TEST_CASE("edited presets encode and decode cleanly")
{
    auto p = serum2::Schema::initPreset();
    edit(p, Json::parse(R"([
        {"target": "oscA.unison", "set": 5}, {"target": "oscA.detune", "set": 0.2},
        {"add_fx": "chorus"}, {"add_fx": "delay", "params": {"feedback": 40, "mix": 20}},
        {"mod": {"source": "lfo1", "target": "fx.chorus.mix", "amount": 20}}
    ])"));
    const auto bytes = serum2::encodePreset(p);
    const auto decoded = serum2::decodePreset(bytes);
    CHECK(decoded.data == p.data);
    CHECK(serum2::encodePreset(decoded) == bytes);
}

TEST_CASE("every vocabulary move uses valid names")
{
    const auto& vocab = Content::builtin();
    REQUIRE(Content::isValid(vocab));
    auto p = serum2::Schema::initPreset();
    int checked = 0;
    for (const auto& [word, entry] : vocab["descriptors"].items())
    {
        INFO(word);
        for (const auto& move : entry["moves"])
        {
            if (move.contains("target"))
            {
                const auto target = move["target"].get<std::string>();
                const auto dot = target.rfind('.');
                const auto param = target.substr(dot + 1);
                // Pseudo-targets are handled by the editor, not the resolver.
                if (param != "shape" && param != "hz" && param != "name")
                    CHECK_NOTHROW(resolver().resolve(target));
                ++checked;
            }
            if (move.contains("add_fx"))
            {
                CHECK(resolver().fxType(move["add_fx"].get<std::string>()));
                auto copy = p;
                CHECK_NOTHROW(edit(copy, Json::array({ Json { { "add_fx", move["add_fx"] }, { "params", move.value("params", Json::object()) } } })));
                ++checked;
            }
            if (move.contains("mod"))
            {
                auto copy = p;
                CHECK_NOTHROW(edit(copy, Json::array({ Json { { "mod", move["mod"] } } })));
                ++checked;
            }
            if (move.contains("lfo_shape"))
            {
                auto copy = p;
                CHECK_NOTHROW(edit(copy, Json::array({ move })));
            }
        }
    }
    CHECK(checked > 40);
    for (const auto& [target, ranges] : vocab["typical"].items())
        CHECK_NOTHROW(resolver().resolve(target));
}

TEST_CASE("describe covers the main sections")
{
    auto p = serum2::Schema::initPreset();
    edit(p, Json::parse(R"([
        {"target": "filter1.on", "set": true}, {"add_fx": "reverb"},
        {"mod": {"source": "lfo1", "target": "filter1.cutoff", "amount": 25}}, {"target": "macro1.name", "set": "Air"}
    ])"));
    const auto text = describePreset(p, resolver());
    CHECK(text.find("oscA: on") != std::string::npos);
    CHECK(text.find("filter1: on") != std::string::npos);
    CHECK(text.find("lfo1 → filter1.cutoff 25%") != std::string::npos);
    CHECK(text.find("fx.reverb: mix 30") != std::string::npos);
    CHECK(text.find("macro1 \"Air\"") != std::string::npos);
}

TEST_CASE("store versions presets and never overwrites")
{
    TempDir dir("store");
    PresetStore store(tempPaths(dir));
    auto p = serum2::Schema::initPreset();
    setPresetName(p, "Warm Pad");
    const auto file = store.create(p, "created");
    CHECK(file.filename() == "Warm Pad.SerumPreset");
    CHECK(store.owns(file));
    CHECK(store.resolve("warm pad") == file);
    CHECK(store.resolve("Warm Pad.SerumPreset") == file);

    // Same name again: a second preset, not an overwrite.
    const auto second = store.create(p, "created");
    CHECK(second.filename() == "Warm Pad 2.SerumPreset");
    CHECK(presetNameOf(store.load(second)) == "Warm Pad 2");

    auto v2 = store.load(file);
    edit(v2, Json::parse(R"([{"target": "env1.attack", "set": 0.5}])"));
    CHECK(store.commit(file, v2, "slower attack") == 2);

    // Tweaked in Serum and saved over the file: that state becomes a version.
    auto tweaked = store.load(file);
    edit(tweaked, Json::parse(R"([{"target": "env1.release", "set": 2}])"));
    serum2::writePresetFile(file, tweaked);
    auto v4 = store.load(file);
    edit(v4, Json::parse(R"([{"target": "global.volume", "set": 0.6}])"));
    CHECK(store.commit(file, v4, "louder") == 4);
    const auto versions = store.versions(file);
    REQUIRE(versions.size() == 4);
    CHECK(versions[2].note == "changes saved in Serum");
    CHECK(number(v4, Module::env(0), "kParamRelease") == Approx(2));

    CHECK(store.restore(file, 1) == 5);
    auto restored = store.load(file);
    CHECK(number(restored, Module::env(0), "kParamAttack") == Approx(0.0005));
    CHECK_THROWS_AS(store.restore(file, 99), std::invalid_argument);
    CHECK_THROWS_AS(store.resolve("does not exist"), std::invalid_argument);
    CHECK(store.list().size() == 2);
}

TEST_CASE("MCP protocol basics")
{
    TempDir dir("mcp");
    PresetStore store(tempPaths(dir));
    McpServer server(store, "1.2.3");

    const auto init = server.handle(Json::parse(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26"}})"));
    REQUIRE(init);
    CHECK((*init)["result"]["protocolVersion"] == "2025-03-26");
    CHECK((*init)["result"]["serverInfo"]["version"] == "1.2.3");
    CHECK((*init)["result"]["capabilities"].contains("tools"));
    const auto odd = server.handle(Json::parse(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"1999-01-01"}})"));
    CHECK((*odd)["result"]["protocolVersion"] == "2025-06-18");

    CHECK_FALSE(server.handle(Json::parse(R"({"jsonrpc":"2.0","method":"notifications/initialized"})")));
    const auto tools = server.handle(Json::parse(R"({"jsonrpc":"2.0","id":"a","method":"tools/list"})"));
    CHECK((*tools)["id"] == "a");
    CHECK((*tools)["result"]["tools"].size() == McpServer::tools().size());
    for (const auto& t : (*tools)["result"]["tools"])
    {
        CHECK(t["inputSchema"]["type"] == "object");
        CHECK_FALSE(t["description"].get<std::string>().empty());
    }
    const auto missing = server.handle(Json::parse(R"({"jsonrpc":"2.0","id":2,"method":"nope"})"));
    CHECK((*missing)["error"]["code"] == -32601);
    const auto ping = server.handle(Json::parse(R"({"jsonrpc":"2.0","id":3,"method":"ping"})"));
    CHECK((*ping)["result"] == Json::object());

    std::istringstream in("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"}\n\nnot json\n");
    std::ostringstream out;
    server.run(in, out);
    std::istringstream lines(out.str());
    std::string line;
    std::vector<Json> responses;
    while (std::getline(lines, line))
        responses.push_back(Json::parse(line));
    REQUIRE(responses.size() == 2);
    CHECK(responses[1]["error"]["code"] == -32700);
}

TEST_CASE("tools create, edit, undo and protect other presets")
{
    TempDir dir("tools");
    PresetStore store(tempPaths(dir));
    McpServer server(store, "1.0.0");
    const auto call = [&](const char* name, const char* args) { return server.callTool(name, Json::parse(args)); };
    const auto text = [](const Json& r) { return r["content"][0]["text"].get<std::string>(); };

    auto r = call("create_preset", R"({"name": "BA Test Reese", "description": "dark reese", "start": {"recipe": "reese", "seed": 3},
                                        "edits": [{"add_fx": "reverb", "params": {"mix": 20}}]})");
    INFO(text(r));
    REQUIRE_FALSE(r["isError"].get<bool>());
    const auto file = store.resolve("BA Test Reese");
    auto created = store.load(file);
    CHECK(created.metadata["presetAuthor"] == "Text To Synth · Baastik Labs");
    CHECK(created.metadata["presetDescription"] == "dark reese");

    r = call("edit_preset", R"({"preset": "BA Test Reese", "note": "drier", "edits": [{"target": "fx.reverb.mix", "scale": 0.5}]})");
    REQUIRE_FALSE(r["isError"].get<bool>());
    CHECK(text(r).find("fx.reverb.mix: 20 → 10") != std::string::npos);

    // A failing batch saves nothing.
    const auto before = serum2::readFile(file);
    r = call("edit_preset", R"({"preset": "BA Test Reese", "edits": [{"target": "filter1.cutoff", "set": 0.2}, {"target": "bad.param", "set": 1}]})");
    CHECK(r["isError"].get<bool>());
    CHECK(serum2::readFile(file) == before);
    CHECK(store.versions(file).size() == 2);

    r = call("restore_version", R"({"preset": "BA Test Reese"})");
    REQUIRE_FALSE(r["isError"].get<bool>());
    CHECK(store.versions(file).size() == 3);

    // Presets outside the folder are copied, never modified.
    auto outside = serum2::Schema::initPreset();
    setPresetName(outside, "Factory Thing");
    const auto outsideFile = dir.path / "Factory Thing.SerumPreset";
    serum2::writePresetFile(outsideFile, outside);
    const auto outsideBytes = serum2::readFile(outsideFile);
    const auto args = Json { { "preset", outsideFile.string() }, { "edits", Json::parse(R"([{"target": "oscA.level", "set": 0.5}])") } };
    r = server.callTool("edit_preset", args);
    REQUIRE_FALSE(r["isError"].get<bool>());
    CHECK(serum2::readFile(outsideFile) == outsideBytes);
    CHECK(store.versions(store.resolve("Factory Thing")).size() == 2);

    CHECK(call("create_preset", R"({"name": "X", "start": {"recipe": "nope"}})")["isError"].get<bool>());
    CHECK(call("create_preset", R"({"description": "no name"})")["isError"].get<bool>());
    CHECK(call("frobnicate", "{}")["isError"].get<bool>());
    CHECK(text(call("sound_design_guide", R"({"section": "parameters"})")).find("filter1") != std::string::npos);
    CHECK(text(call("list_starting_points", "{}")).find("reese") != std::string::npos);
    CHECK(text(call("list_presets", "{}")).find("BA Test Reese") != std::string::npos);
}

TEST_CASE("update notices ride along with tool results")
{
    struct FakeUpdates : UpdateService
    {
        std::string notice() override { return "Text To Synth 9.9.9 is available"; }
        std::string checkNow() override { return "checked"; }
        std::string installUpdate() override { return "installing"; }
    } updates;
    TempDir dir("updates");
    PresetStore store(tempPaths(dir));
    McpServer server(store, "1.0.0", &updates);
    const auto r = server.callTool("list_presets", Json::object());
    CHECK(r["content"][0]["text"].get<std::string>().find("9.9.9 is available") != std::string::npos);
    CHECK(server.callTool("install_update", Json::object())["content"][0]["text"] == "installing");
}

TEST_CASE("content cache wins only when newer and valid")
{
    TempDir dir("content");
    const auto cache = Content::cacheFile(dir.path);
    CHECK(Content::load(cache) == Content::builtin());
    auto newer = Content::builtin();
    newer["version"] = newer["version"].get<int>() + 1;
    std::ofstream(cache) << newer.dump();
    CHECK(Content::load(cache)["version"] == newer["version"]);
    newer["minEngine"] = Content::kEngineVersion + 1;
    std::ofstream(cache) << newer.dump();
    CHECK(Content::load(cache) == Content::builtin());
    std::ofstream(cache) << "{broken";
    CHECK(Content::load(cache) == Content::builtin());
}

#if !defined(_WIN32)
TEST_CASE("registration merges into Claude configs and keeps the rest")
{
    TempDir dir("register");
    ScopedEnv home("HOME", dir.path.string());
    ScopedEnv xdg("XDG_CONFIG_HOME", (dir.path / ".config").string());
    ScopedEnv claudeDir("CLAUDE_CONFIG_DIR", (dir.path / "cc").string());

    // Claude Code isn't installed yet: only Claude Desktop is configured.
    auto results = registerWithClaude("/opt/tts/text-to-synth");
    REQUIRE(results.size() == 2);
    CHECK(results[0].status == "added");
    CHECK(results[1].status == "not installed");
    const auto desktop = claudeDesktopConfigPath();
    REQUIRE(std::filesystem::exists(desktop));
    auto config = Json::parse(std::ifstream(desktop));
    CHECK(config["mcpServers"][kServerId]["command"] == "/opt/tts/text-to-synth");

    // Existing Claude Code config: other keys and servers survive, in order.
    std::filesystem::create_directories(dir.path / "cc");
    std::ofstream(claudeCodeConfigPath()) << R"({"zeta": 1, "mcpServers": {"other": {"command": "x"}}, "alpha": 2})";
    results = registerWithClaude("/opt/tts/text-to-synth");
    CHECK(results[0].status == "already set");
    CHECK(results[1].status == "added");
    std::ifstream in(claudeCodeConfigPath());
    std::stringstream buf;
    buf << in.rdbuf();
    const auto text = buf.str();
    CHECK(text.find("\"zeta\"") < text.find("\"alpha\""));
    const auto code = Json::parse(text);
    CHECK(code["mcpServers"]["other"]["command"] == "x");
    CHECK(code["mcpServers"][kServerId]["type"] == "stdio");
    CHECK(std::filesystem::exists(std::filesystem::path(claudeCodeConfigPath()).concat(".before-text-to-synth")));

    // A moved binary updates the command; user env vars stay.
    {
        auto c = Json::parse(std::ifstream(claudeCodeConfigPath()));
        c["mcpServers"][kServerId]["env"] = { { "TEXT_TO_SYNTH_PRESETS_DIR", "/p" } };
        std::ofstream(claudeCodeConfigPath()) << c.dump();
    }
    results = registerWithClaude("/new/place/text-to-synth");
    CHECK(results[1].status == "updated");
    const auto updated = Json::parse(std::ifstream(claudeCodeConfigPath()));
    CHECK(updated["mcpServers"][kServerId]["command"] == "/new/place/text-to-synth");
    CHECK(updated["mcpServers"][kServerId]["env"]["TEXT_TO_SYNTH_PRESETS_DIR"] == "/p");

    results = registerWithClaude("", true);
    CHECK(results[0].status == "removed");
    CHECK(results[1].status == "removed");
    CHECK_FALSE(Json::parse(std::ifstream(claudeCodeConfigPath()))["mcpServers"].contains(kServerId));

    // A broken config is left alone.
    std::ofstream(claudeCodeConfigPath()) << "{not json";
    results = registerWithClaude("/opt/tts/text-to-synth");
    CHECK(results[1].status.rfind("error", 0) == 0);
}
#endif

TEST_CASE("vocabulary edits work on real presets")
{
    // Runs across SERUM_FIXTURES too: typical requests must never break a
    // preset (each edit that applies must encode, decode and describe).
    const auto requests = Json::parse(R"([
        [{"target": "filter1.cutoff", "add": -0.08}],
        [{"target": "fx.reverb.mix", "scale": 0.6}],
        [{"target": "filter1.resonance", "scale": 0.6}, {"target": "fx.distortion.drive", "scale": 0.7}],
        [{"add_fx": "reverb", "params": {"size": 50, "mix": 25}}],
        [{"target": "oscA.unison", "add": 2}, {"target": "oscA.detune", "add": 0.04}],
        [{"mod": {"source": "lfo1", "target": "filter1.cutoff", "amount": 20}}, {"lfo_shape": {"lfo": 1, "shape": "sine"}}],
        [{"target": "env1.release", "scale": 1.8}, {"target": "global.volume", "add": -0.05}],
        [{"remove_fx": "distortion"}]
    ])");
    int applied = 0;
    for (const auto& path : fixturePresets())
    {
        INFO(path.string());
        const auto original = serum2::readPresetFile(path);
        for (const auto& request : requests)
        {
            auto p = original;
            try
            {
                PresetEditor(p).apply(request);
            }
            catch (const std::invalid_argument&)
            {
                continue; // e.g. no reverb to scale: Claude would add one instead
            }
            const auto decoded = serum2::decodePreset(serum2::encodePreset(p));
            CHECK(decoded.data == p.data);
            CHECK_NOTHROW(describePreset(decoded, resolver()));
            ++applied;
        }
    }
    if (!fixturePresets().empty())
        CHECK(applied > 0);
}
