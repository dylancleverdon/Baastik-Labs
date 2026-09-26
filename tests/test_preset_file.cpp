#include <catch2/catch_test_macros.hpp>

#include "Fixtures.h"
#include "serum2/Patch.h"
#include "serum2/PresetFile.h"
#include "serum2/Schema.h"

using namespace serum2;

TEST_CASE("md5 matches RFC 1321 test vectors")
{
    const auto md5 = [](std::string_view s) {
        return md5Hex({ reinterpret_cast<const std::uint8_t*>(s.data()), s.size() });
    };
    CHECK(md5("") == "d41d8cd98f00b204e9800998ecf8427e");
    CHECK(md5("abc") == "900150983cd24fb0d6963f7d28e17f72");
    CHECK(md5("12345678901234567890123456789012345678901234567890123456789012345678901234567890")
          == "57edf4a22be3c955ac49da2e2107b67a");
}

TEST_CASE("init preset encodes and decodes")
{
    const auto init = Schema::initPreset();
    const auto bytes = encodePreset(init);
    const auto decoded = decodePreset(bytes);
    CHECK(decoded.data == init.data);
    CHECK(decoded.metadata["presetName"] == "Init");
    CHECK(decoded.metadata["hash"].get<std::string>().size() == 32);
}

TEST_CASE("garbage is rejected")
{
    const Bytes junk { 1, 2, 3, 4 };
    CHECK_THROWS_AS(decodePreset(junk), FormatError);
}

TEST_CASE("Serum-saved presets round-trip byte for byte")
{
    for (const auto& path : fixturePresets())
    {
        INFO(path.string());
        const auto original = readFile(path);
        const auto preset = decodePreset(original);
        const auto reencoded = encodePreset(preset);

        // The engine state must come back identical, float widths included.
        CHECK(extractCbor(reencoded) == extractCbor(original));
        // Metadata comes back identical apart from the recomputed hash.
        auto metadata = decodePreset(reencoded).metadata;
        metadata["hash"] = preset.metadata["hash"];
        CHECK(metadata == preset.metadata);
    }
}

TEST_CASE("editor writes Serum's wire types and omits defaults")
{
    auto init = Schema::initPreset();
    PatchEditor ed(init.data);

    ed.set(Module::osc(1), "kParamVolume", 0.5);
    ed.setFlag(Module::osc(1), "kParamEnable", true);
    CHECK(init.data["Oscillator1"]["plainParams"]["kParamEnable"].is_number_float());
    CHECK(init.data["Oscillator1"]["plainParams"]["kParamEnable"] == 1.0);

    // Setting a value back to its default removes the key again.
    ed.set(Module::osc(1), "kParamVolume", 0.75);
    CHECK_FALSE(init.data["Oscillator1"]["plainParams"].contains("kParamVolume"));
    ed.setFlag(Module::osc(1), "kParamEnable", false);
    CHECK(init.data["Oscillator1"]["plainParams"] == "default");

    // Osc A is on by default, so enabling it writes nothing.
    ed.setFlag(Module::osc(0), "kParamEnable", true);
    CHECK(init.data["Oscillator0"]["plainParams"] == "default");

    // Values are clamped to the schema range.
    ed.set(Module::filter(0), "kParamFreq", 3.0);
    CHECK(ed.number(Module::filter(0), "kParamFreq") == 1.0);
}

TEST_CASE("mod routes fill free slots")
{
    auto init = Schema::initPreset();
    PatchEditor ed(init.data);
    const auto& schema = Schema::builtin();
    const auto dest = schema.modDest("filter0.cutoff");
    REQUIRE(dest);
    const int slot = ed.addModRoute({ schema.modSource("env1").value(), 0, *dest, 40.0, false });
    CHECK(slot == 0);
    const auto route = ed.modRoute(slot);
    REQUIRE(route);
    CHECK(route->dest.paramName == "kParamFreq");
    CHECK(init.data["ModSlot0"]["source"][0].is_number_integer());
    CHECK(ed.addModRoute({ 7, 0, *dest, 10.0, false }) == 1);
}
