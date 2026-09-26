#pragma once

#include <optional>
#include <string>
#include <vector>

#include "serum2/Patch.h"
#include "tts/Targets.h"

namespace tts
{
struct EditReport
{
    std::vector<std::string> changes;  // "filter1.cutoff: 0.62 -> 0.45"
    std::vector<std::string> warnings; // "filter1 is off, so cutoff has no effect"
};

// Where an FX unit sits: rack 0 is the main rack, 1-2 are the FX buses.
struct FxLocation
{
    int rack = 0;
    int position = 0;
};

// Applies structured edits to a preset. Claude turns words into these;
// everything here is deterministic and schema-checked.
//
// Edits (a JSON array; each object is one of):
//   {"target": "filter1.cutoff", "set": 0.4}        absolute value (number, on/off, or choice)
//   {"target": "fx.reverb.mix", "add": -10}         relative change, clamped to range
//   {"target": "fx.reverb.mix", "scale": 0.5}       multiply, clamped to range
//   {"add_fx": "reverb", "params": {"size": 60}, "position": 0, "rack": 1}
//   {"remove_fx": "distortion", "instance": 1}
//   {"mod": {"source": "lfo1", "target": "filter1.cutoff", "amount": 30, "bipolar": false}}
//   {"unmod": {"source": "lfo1", "target": "filter1.cutoff"}}   source optional
//   {"wavetable": {"osc": "A", "table": "analog_basic", "frame": "saw"}}
//   {"lfo_shape": {"lfo": 1, "shape": "sine"}}
//   {"rename": "Dark Reese"}
// Pseudo-targets: macroN.name (text), lfoN.rate "1/8" (synced), lfoN.hz (free Hz),
// lfoN.shape, oscX.wavetable (table id), oscX.wt_pos "saw" (named frame).
class PresetEditor
{
public:
    explicit PresetEditor(serum2::Preset& preset, const TargetResolver& resolver = defaultResolver());

    // Applies every edit or throws std::invalid_argument naming the edit that
    // failed. Callers work on a copy so a failed batch leaves nothing half-done.
    EditReport apply(const Json& edits);
    void applyOne(const Json& edit, EditReport& report);

    // Current value of a target: number, bool or string. Null if it refers to
    // an FX unit the preset doesn't have.
    Json read(const Target& t) const;

    std::optional<FxLocation> findFx(const std::string& fxType, int instance) const;
    // Every FX unit in rack order: (location, schema type).
    std::vector<std::pair<FxLocation, std::string>> fxUnits() const;

    static const TargetResolver& defaultResolver();
    static std::vector<std::string> lfoShapes();

private:
    void setTarget(const std::string& name, const Json& edit, EditReport& report);
    void write(const Target& t, const Json& value, EditReport& report);
    void addFx(const Json& edit, EditReport& report);
    void removeFx(const Json& edit, EditReport& report);
    void addMod(const Json& spec, EditReport& report);
    void removeMod(const Json& spec, EditReport& report);
    void setWavetable(const Json& spec, EditReport& report);
    void setLfoShape(int lfo, const std::string& shape, EditReport& report);
    void rename(const std::string& name, EditReport& report);
    void warnIfInactive(const Target& t, EditReport& report) const;
    void shiftFxModRoutes(int rack, int fromPosition, int delta);

    Json* fxEntry(const FxLocation& at);
    const Json* fxEntry(const FxLocation& at) const;
    Json& fxParams(const FxLocation& at, const std::string& fxType);

    serum2::Preset& preset_;
    const TargetResolver& resolver_;
    serum2::PatchEditor ed_;
};

std::string formatValue(const Json& value);
} // namespace tts
