#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "serum2/Schema.h"

namespace serum2
{
// Where a module lives in the payload and which schema table describes it.
struct Module
{
    std::vector<std::string> path; // e.g. {"Oscillator0", "WTOsc0"}
    std::string schemaModule;      // e.g. "WTOsc"
    int slot = -1;

    static Module osc(int i);         // 0-2 = A/B/C, 3 = noise, 4 = sub
    static Module wavetable(int i);   // wavetable engine of osc 0-2
    static Module multisample(int i); // multisample engine of osc 0-2
    static Module noise();
    static Module sub();
    static Module filter(int i);
    static Module env(int i);
    static Module lfo(int i);
    static Module macro(int i);
    static Module global();
    static Module routing(int i); // 0-4 oscillators, 5-6 filter outputs
};

struct CurvePoint
{
    double x = 0.0;       // 0..1
    double y = 0.0;       // 0 = bottom, 1 = top (Serum stores it inverted)
    double tension = 0.5; // 0.5 = straight segment
};

struct FxUnit
{
    std::string type;               // e.g. "FXReverb"
    Json params = Json::object();   // kParam* -> value
};

struct ModRoute
{
    int source = 0;
    int aux = 0; // 0 = no aux source
    ModDest dest;
    double amount = 0.0; // -100..100
    bool bipolar = false;
};

constexpr int kNumOscillators = 5;
constexpr int kNumFilters = 2;
constexpr int kNumEnvelopes = 4;
constexpr int kNumLfos = 10;
constexpr int kNumMacros = 8;
constexpr int kNumModSlots = 64;
constexpr int kNumFxRacks = 3;

// Edits a payload the way Serum writes it: numbers are doubles (never CBOR
// bools or ints), keys equal to Serum's absent-key default are removed
// (presence alone changes the sound for some params), and a module whose
// plainParams becomes empty goes back to the "default" marker.
class PatchEditor
{
public:
    explicit PatchEditor(Json& data, const Schema& schema = Schema::builtin());

    const Schema& schema() const { return schema_; }
    Json& data() { return data_; }

    Json& container(const Module& m);
    const Json* find(const Module& m) const;

    bool has(const Module& m, std::string_view key) const;
    double number(const Module& m, std::string_view key) const;
    bool flag(const Module& m, std::string_view key) const;
    std::string text(const Module& m, std::string_view key) const;

    void set(const Module& m, std::string_view key, double value);
    void setFlag(const Module& m, std::string_view key, bool value);
    void setText(const Module& m, std::string_view key, std::string_view value);
    // Writes the value even if it equals the default.
    void force(const Module& m, std::string_view key, Json value);
    void erase(const Module& m, std::string_view key);

    // Oscillator engines.
    void setWavetable(int osc, const Wavetable& table);
    void setMultisample(int osc, const MultiSample& instrument);
    std::string wavetablePath(int osc) const;
    void setNoiseType(std::string_view type);

    // LFOs.
    void setLfoCurve(int lfo, std::span<const CurvePoint> points);
    void clearLfoCurve(int lfo);
    void setLfoChaos(int lfo, std::string_view type); // "Lorenz", "Rossler", "RandomSH"

    // Macros.
    void setMacroName(int macro, std::string_view name);
    std::string macroName(int macro) const;

    // Mod matrix.
    bool isModSlotUsed(int slot) const;
    int addModRoute(const ModRoute& route); // returns slot index or -1 when full
    std::vector<int> usedModSlots() const;
    void clearModSlot(int slot);
    std::optional<ModRoute> modRoute(int slot) const;

    // FX racks (Serum runs racks 0-2 in parallel, units in series).
    std::vector<FxUnit> fxRack(int rack) const;
    void setFxRack(int rack, const std::vector<FxUnit>& units);

private:
    Json& params(const Module& m);
    const Json* paramsIfPresent(const Module& m) const;
    const ParamDef* def(const Module& m, std::string_view key) const;
    void tidy(const Module& m);

    Json& data_;
    const Schema& schema_;
};

// Serum's canonical wet value for FX units: kParamWet is only written when
// it differs from 100.
constexpr double kFxFullyWet = 100.0;
} // namespace serum2
