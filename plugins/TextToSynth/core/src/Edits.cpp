#include "tts/Edits.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>
#include <stdexcept>

namespace tts
{
using serum2::Module;
using serum2::ParamDef;

namespace
{
constexpr double kFxAbsentWet = serum2::kFxFullyWet; // an FX unit without kParamWet is 100% wet

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::optional<std::string> matchEnum(const ParamDef& def, const std::string& value)
{
    const auto n = normalize(value);
    for (const auto& v : def.enumValues)
        if (normalize(v) == n || normalize(v) == "k" + n)
            return v;
    return std::nullopt;
}

std::optional<bool> parseBool(const Json& v)
{
    if (v.is_boolean())
        return v.get<bool>();
    if (v.is_number())
        return v.get<double>() >= 0.5;
    if (v.is_string())
    {
        const auto s = normalize(v.get<std::string>());
        if (s == "on" || s == "true" || s == "yes" || s == "enabled" || s == "1")
            return true;
        if (s == "off" || s == "false" || s == "no" || s == "disabled" || s == "0")
            return false;
    }
    return std::nullopt;
}

double toNumber(const Json& v)
{
    if (v.is_boolean())
        return v.get<bool>() ? 1.0 : 0.0;
    if (v.is_number())
        return v.get<double>();
    return 0.0;
}

int parseSlot(const Json& v, int count, const std::string& what)
{
    int slot = 0;
    if (v.is_number_integer())
        slot = v.get<int>();
    else if (v.is_string())
    {
        const auto s = normalize(v.get<std::string>());
        const auto digits = s.find_first_of("0123456789");
        if (digits == std::string::npos)
            throw std::invalid_argument(what + " needs a number, like 1");
        slot = std::stoi(s.substr(digits));
    }
    if (slot < 1 || slot > count)
        throw std::invalid_argument(what + " must be 1-" + std::to_string(count));
    return slot - 1;
}

int parseOsc(const Json& v)
{
    std::string s = v.is_string() ? normalize(v.get<std::string>()) : v.is_number_integer() ? std::to_string(v.get<int>()) : "";
    if (s.rfind("osc", 0) == 0)
        s = s.substr(3);
    if (s == "a" || s == "1")
        return 0;
    if (s == "b" || s == "2")
        return 1;
    if (s == "c" || s == "3")
        return 2;
    throw std::invalid_argument("osc must be A, B or C");
}

std::vector<serum2::CurvePoint> shapePoints(const std::string& shape)
{
    std::vector<serum2::CurvePoint> pts;
    if (shape == "sine")
        for (int i = 0; i <= 16; ++i)
        {
            const double x = i / 16.0;
            pts.push_back({ x, 0.5 - 0.5 * std::cos(6.283185307179586 * x) });
        }
    else if (shape == "saw_up" || shape == "ramp_up")
        pts = { { 0, 0 }, { 0.5, 0.5 }, { 1, 1 } };
    else if (shape == "saw_down" || shape == "saw" || shape == "ramp_down")
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
        for (int s = 0; s < 8; ++s)
        {
            // Trance gate: on/off in eighths, first beat of each pair on.
            const double y = (s % 2 == 0 || s == 3 || s == 6) ? 1.0 : 0.0;
            const double x0 = s / 8.0;
            const double x1 = (s + 1) / 8.0;
            pts.push_back({ x0, y });
            pts.push_back({ x1 - 0.02, y });
            pts.push_back({ s == 7 ? 1.0 : x1 - 0.02, 0.0 });
        }
    return pts;
}

std::string describeSlot(const std::string& source, const std::string& target, double amount)
{
    return source + " → " + target + " (" + formatValue(amount) + "%)";
}
} // namespace

std::string formatValue(const Json& value)
{
    if (value.is_null())
        return "none";
    if (value.is_boolean())
        return value.get<bool>() ? "on" : "off";
    if (value.is_string())
        return value.get<std::string>();
    if (value.is_number())
    {
        const double v = value.get<double>();
        std::ostringstream out;
        if (std::abs(v) >= 1000.0)
            out << static_cast<long long>(std::llround(v));
        else
        {
            out.precision(std::abs(v) < 1.0 ? 3 : 4);
            out << v;
        }
        return out.str();
    }
    return value.dump();
}

const TargetResolver& PresetEditor::defaultResolver()
{
    static const TargetResolver resolver;
    return resolver;
}

std::vector<std::string> PresetEditor::lfoShapes()
{
    return { "sine", "triangle", "saw_up", "saw_down", "square", "pulse", "decay", "stairs", "gate", "random", "lorenz", "rossler" };
}

PresetEditor::PresetEditor(serum2::Preset& preset, const TargetResolver& resolver)
    : preset_(preset), resolver_(resolver), ed_(preset.data, resolver.schema())
{
}

EditReport PresetEditor::apply(const Json& edits)
{
    EditReport report;
    if (!edits.is_array())
        throw std::invalid_argument("edits must be a list");
    for (std::size_t i = 0; i < edits.size(); ++i)
    {
        try
        {
            applyOne(edits[i], report);
        }
        catch (const std::exception& e)
        {
            throw std::invalid_argument("Edit " + std::to_string(i + 1) + " (" + edits[i].dump() + "): " + e.what());
        }
    }
    return report;
}

void PresetEditor::applyOne(const Json& edit, EditReport& report)
{
    if (!edit.is_object())
        throw std::invalid_argument("each edit must be an object");
    if (edit.contains("target"))
        return setTarget(edit["target"].get<std::string>(), edit, report);
    if (edit.contains("add_fx"))
        return addFx(edit, report);
    if (edit.contains("remove_fx"))
        return removeFx(edit, report);
    if (edit.contains("mod"))
        return addMod(edit["mod"], report);
    if (edit.contains("unmod"))
        return removeMod(edit["unmod"], report);
    if (edit.contains("wavetable"))
        return setWavetable(edit["wavetable"], report);
    if (edit.contains("lfo_shape"))
    {
        const auto& s = edit["lfo_shape"];
        return setLfoShape(parseSlot(s.value("lfo", Json(1)), serum2::kNumLfos, "lfo"), s.value("shape", std::string()), report);
    }
    if (edit.contains("rename"))
        return rename(edit["rename"].get<std::string>(), report);
    throw std::invalid_argument("unknown edit; use target+set/add/scale, add_fx, remove_fx, mod, unmod, wavetable, lfo_shape or rename");
}

// ------------------------------------------------------------------ reading

Json* PresetEditor::fxEntry(const FxLocation& at)
{
    auto& rack = preset_.data["FXRack" + std::to_string(at.rack)];
    if (!rack.is_object() || !rack.contains("FX") || at.position >= static_cast<int>(rack["FX"].size()))
        return nullptr;
    return &rack["FX"][static_cast<std::size_t>(at.position)];
}

const Json* PresetEditor::fxEntry(const FxLocation& at) const
{
    const auto key = "FXRack" + std::to_string(at.rack);
    if (!preset_.data.contains(key))
        return nullptr;
    const auto& rack = preset_.data[key];
    if (!rack.is_object() || !rack.contains("FX") || at.position >= static_cast<int>(rack["FX"].size()))
        return nullptr;
    return &rack["FX"][static_cast<std::size_t>(at.position)];
}

std::vector<std::pair<FxLocation, std::string>> PresetEditor::fxUnits() const
{
    std::vector<std::pair<FxLocation, std::string>> units;
    for (int rack = 0; rack < serum2::kNumFxRacks; ++rack)
    {
        const auto key = "FXRack" + std::to_string(rack);
        if (!preset_.data.contains(key) || !preset_.data[key].is_object() || !preset_.data[key].contains("FX"))
            continue;
        const auto& list = preset_.data[key]["FX"];
        for (std::size_t i = 0; i < list.size(); ++i)
            if (const auto name = resolver_.schema().fxTypeName(list[i].value("type", -1)))
                units.push_back({ { rack, static_cast<int>(i) }, *name });
    }
    return units;
}

std::optional<FxLocation> PresetEditor::findFx(const std::string& fxType, int instance) const
{
    int seen = 0;
    for (const auto& [at, type] : fxUnits())
        if (type == fxType && seen++ == instance)
            return at;
    return std::nullopt;
}

Json& PresetEditor::fxParams(const FxLocation& at, const std::string& fxType)
{
    auto* entry = fxEntry(at);
    auto& unit = (*entry)[fxType];
    if (!unit.is_object())
        unit = Json::object();
    auto& p = unit["plainParams"];
    if (!p.is_object())
        p = Json::object();
    return p;
}

Json PresetEditor::read(const Target& t) const
{
    if (t.kind == Target::Kind::Fx)
    {
        const auto at = findFx(t.fxType, t.fxInstance);
        if (!at)
            return nullptr;
        const auto* entry = fxEntry(*at);
        if (entry->contains(t.fxType) && (*entry)[t.fxType].contains("plainParams"))
        {
            const auto& p = (*entry)[t.fxType]["plainParams"];
            if (p.is_object() && p.contains(t.key))
            {
                const auto& v = p[t.key];
                if (t.def && t.def->kind == ParamDef::Kind::Bool)
                    return toNumber(v) >= 0.5;
                return v;
            }
        }
        if (t.key == "kParamWet")
            return kFxAbsentWet;
        if (t.def && t.def->defaultValue)
            return *t.def->defaultValue;
        return nullptr;
    }
    if (t.def && t.def->kind == ParamDef::Kind::Enum)
        return ed_.text(t.module, t.key);
    if (t.def && t.def->kind == ParamDef::Kind::Bool)
        return ed_.flag(t.module, t.key);
    return ed_.number(t.module, t.key);
}

// ------------------------------------------------------------------ writing

void PresetEditor::write(const Target& t, const Json& value, EditReport& report)
{
    const auto before = read(t);
    Json stored;
    if (t.def && t.def->kind == ParamDef::Kind::Enum)
    {
        if (!value.is_string())
            throw std::invalid_argument(t.label + " is a choice; use one of: " + [&] {
                std::string s;
                for (const auto& v : t.def->enumValues)
                    s += (s.empty() ? "" : ", ") + v;
                return s;
            }());
        const auto match = matchEnum(*t.def, value.get<std::string>());
        if (!match)
        {
            std::string s;
            for (const auto& v : t.def->enumValues)
                s += (s.empty() ? "" : ", ") + v;
            throw std::invalid_argument("\"" + value.get<std::string>() + "\" isn't a " + t.label + " option. Options: " + s);
        }
        stored = *match;
    }
    else if (t.def && t.def->kind == ParamDef::Kind::Bool)
    {
        const auto b = parseBool(value);
        if (!b)
            throw std::invalid_argument(t.label + " is on/off");
        stored = *b;
    }
    else
    {
        if (!value.is_number())
            throw std::invalid_argument(t.label + " needs a number");
        double v = value.get<double>();
        if (t.def)
        {
            const double clamped = t.def->clamp(v);
            if (std::abs(clamped - v) > 1e-9)
                report.warnings.push_back(t.label + ": " + formatValue(v) + " is out of range, used " + formatValue(clamped));
            v = clamped;
        }
        stored = v;
    }

    if (t.kind == Target::Kind::Fx)
    {
        const auto at = findFx(t.fxType, t.fxInstance);
        if (!at)
            throw std::invalid_argument("this preset has no " + t.owner + " unit; add one first with {\"add_fx\": \""
                                        + TargetResolver::fxFriendly(t.fxType) + "\"}");
        auto& p = fxParams(*at, t.fxType);
        if (t.key == "kParamWet" && std::abs(toNumber(stored) - kFxAbsentWet) < 1e-9)
            p.erase(t.key);
        else if (stored.is_boolean())
            p[t.key] = stored.get<bool>() ? 1.0 : 0.0;
        else
            p[t.key] = stored;
        if (p.empty())
            (*fxEntry(*at))[t.fxType]["plainParams"] = "default";
    }
    else if (stored.is_string())
    {
        ed_.setText(t.module, t.key, stored.get<std::string>());
        // Serum writes kParamDefaultMode alongside an explicit LFO mode.
        if (t.module.schemaModule == "LFO" && t.key == "kParamMode")
            ed_.force(t.module, "kParamDefaultMode", 0.0);
    }
    else if (stored.is_boolean())
        ed_.setFlag(t.module, t.key, stored.get<bool>());
    else
        ed_.set(t.module, t.key, stored.get<double>());

    const auto after = read(t);
    if (before != after)
        report.changes.push_back(t.label + ": " + formatValue(before) + " → " + formatValue(after));
    else
        report.changes.push_back(t.label + ": already " + formatValue(after));
    warnIfInactive(t, report);
}

void PresetEditor::warnIfInactive(const Target& t, EditReport& report) const
{
    if (t.kind != Target::Kind::Module || t.key == "kParamEnable")
        return;
    const auto& m = t.module;
    if (m.schemaModule == "VoiceFilter" && !ed_.flag(m, "kParamEnable"))
        report.warnings.push_back(t.owner + " is off, so this has no effect until {\"target\": \"" + t.owner + ".on\", \"set\": true}");
    if ((m.schemaModule == "Oscillator" || m.schemaModule == "WTOsc" || m.schemaModule == "MultiSampleOsc"
         || m.schemaModule == "NoiseOsc" || m.schemaModule == "SubOsc")
        && !ed_.flag(Module::osc(m.slot), "kParamEnable"))
        report.warnings.push_back(t.owner + " is off, so this has no effect until {\"target\": \"" + t.owner + ".on\", \"set\": true}");
    if (m.schemaModule == "LFO" || (m.schemaModule == "Env" && m.slot > 0))
    {
        const auto source = resolver_.modSource(t.owner);
        bool used = false;
        for (const int slot : ed_.usedModSlots())
            if (const auto r = ed_.modRoute(slot); r && source && r->source == *source)
                used = true;
        if (!used)
            report.warnings.push_back(t.owner + " isn't routed anywhere yet; add a {\"mod\": {...}} to hear it");
    }
}

void PresetEditor::setTarget(const std::string& name, const Json& edit, EditReport& report)
{
    const bool isSet = edit.contains("set");
    const bool isAdd = edit.contains("add");
    const bool isScale = edit.contains("scale");
    if (int(isSet) + int(isAdd) + int(isScale) != 1)
        throw std::invalid_argument("give exactly one of set, add or scale");

    const auto dot = name.rfind('.');
    const auto ownerName = dot == std::string::npos ? name : name.substr(0, dot);
    const auto param = dot == std::string::npos ? std::string() : normalize(name.substr(dot + 1));
    const auto ownerNorm = normalize(ownerName);

    // Pseudo-parameters that aren't plain kParams.
    if (ownerNorm.rfind("macro", 0) == 0 && param == "name")
    {
        if (!isSet || !edit["set"].is_string())
            throw std::invalid_argument("set a macro name with {\"target\": \"macro1.name\", \"set\": \"Brightness\"}");
        const auto m = resolver_.resolve(ownerName + ".value").module;
        const auto before = ed_.macroName(m.slot);
        ed_.setMacroName(m.slot, edit["set"].get<std::string>());
        report.changes.push_back(ownerName + ".name: \"" + before + "\" → \"" + edit["set"].get<std::string>() + "\"");
        return;
    }
    if (ownerNorm.rfind("lfo", 0) == 0 && param == "shape")
    {
        if (!isSet || !edit["set"].is_string())
            throw std::invalid_argument("set an LFO shape by name, e.g. \"sine\"");
        return setLfoShape(resolver_.resolve(ownerName + ".rate").module.slot, edit["set"].get<std::string>(), report);
    }
    if (ownerNorm.rfind("lfo", 0) == 0 && param == "hz")
    {
        if (!isSet || !edit["set"].is_number())
            throw std::invalid_argument("lfoN.hz takes a number of Hz with set");
        const auto m = resolver_.resolve(ownerName + ".rate").module;
        ed_.setFlag(m, "kParamBeatSync", false);
        ed_.set(m, "kParamRate", edit["set"].get<double>());
        report.changes.push_back(resolver_.resolve(ownerName + ".rate").owner + ".rate: free " + formatValue(ed_.number(m, "kParamRate")) + " Hz");
        return;
    }
    if (param == "wavetable" || param == "table")
    {
        if (!isSet)
            throw std::invalid_argument("set a wavetable by id");
        Json spec = { { "osc", ownerName }, { "table", edit["set"] } };
        return setWavetable(spec, report);
    }

    const auto t = resolver_.resolve(name);

    // Named values: synced LFO rates and wavetable frames.
    if (isSet && edit["set"].is_string() && t.kind == Target::Kind::Module)
    {
        const auto text = edit["set"].get<std::string>();
        if (t.module.schemaModule == "LFO" && t.key == "kParamRate")
        {
            const auto& rates = resolver_.schema().lfoSyncRates();
            const auto it = rates.find(text);
            if (it == rates.end())
            {
                std::string names;
                for (const auto& [n, v] : rates)
                    names += (names.empty() ? "" : ", ") + n;
                throw std::invalid_argument("synced rates are: " + names + " (or use " + t.owner + ".hz for free-running)");
            }
            const auto before = ed_.flag(t.module, "kParamBeatSync") ? formatValue(read(t)) : "free";
            ed_.setFlag(t.module, "kParamBeatSync", true);
            ed_.set(t.module, "kParamRate", it->second);
            report.changes.push_back(t.label + ": " + before + " → " + text + " (synced)");
            warnIfInactive(t, report);
            return;
        }
        if (t.module.schemaModule == "WTOsc" && t.key == "kParamTablePos")
        {
            const auto path = ed_.wavetablePath(t.module.slot);
            for (const auto& wt : resolver_.schema().wavetables())
                if (wt.path == path)
                    if (const auto it = wt.knownFrames.find(lower(text)); it != wt.knownFrames.end())
                        return write(t, it->second, report);
            throw std::invalid_argument("no known frame \"" + text + "\" in this wavetable; use a number (table frame)");
        }
    }

    Json value;
    if (isSet)
        value = edit["set"];
    else
    {
        if (t.def && t.def->kind != ParamDef::Kind::Float)
            throw std::invalid_argument(t.label + " isn't a number; use set");
        const auto delta = isAdd ? edit["add"] : edit["scale"];
        if (!delta.is_number())
            throw std::invalid_argument("add/scale need a number");
        const auto current = read(t);
        if (current.is_null())
            throw std::invalid_argument("this preset has no " + t.owner + " unit; add one first with {\"add_fx\": \""
                                        + TargetResolver::fxFriendly(t.fxType) + "\"}");
        const double cur = toNumber(current);
        value = isAdd ? cur + delta.get<double>() : cur * delta.get<double>();
        if (isScale && std::abs(cur) < 1e-12)
            report.warnings.push_back(t.label + " is 0, so scaling does nothing; use set or add");
    }
    write(t, value, report);
}

// ----------------------------------------------------------------- FX units

void PresetEditor::shiftFxModRoutes(int rack, int fromPosition, int delta)
{
    for (const int slot : ed_.usedModSlots())
    {
        auto& entry = preset_.data["ModSlot" + std::to_string(slot)];
        const auto type = entry.value("destModuleTypeString", std::string());
        if (!resolver_.schema().fxParams(type))
            continue;
        const int id = entry.value("destModuleID", 0);
        if (id / 100 != rack || id % 100 < fromPosition)
            continue;
        entry["destModuleID"] = id + delta;
    }
}

void PresetEditor::addFx(const Json& edit, EditReport& report)
{
    const auto name = edit["add_fx"].get<std::string>();
    const auto type = resolver_.fxType(name);
    if (!type || type->rfind("FXSplit", 0) == 0)
        throw std::invalid_argument("unknown FX \"" + name + "\"; use distortion, chorus, flanger, phaser, delay, reverb, "
                                    "compressor, eq, filter, bode, hyper, utility");
    const int rack = edit.value("rack", 0);
    if (rack < 0 || rack >= serum2::kNumFxRacks)
        throw std::invalid_argument("rack is 0 (main), 1 or 2 (FX buses)");

    auto& r = preset_.data["FXRack" + std::to_string(rack)];
    if (!r.is_object())
        r = Json { { "FX", Json::array() }, { "displayName", "" }, { "plainParams", "default" } };
    if (!r["FX"].is_array())
        r["FX"] = Json::array();
    auto& list = r["FX"];
    const int count = static_cast<int>(list.size());
    int position = edit.value("position", count);
    position = std::clamp(position, 0, count);

    Json unit = Json::object();
    unit[*type] = Json { { "plainParams", "default" } };
    if (*type == "FXHyperD")
    {
        unit["kUIParamMixOrGainDimE"] = 0.0;
        unit["kUIParamMixOrGainHyper"] = 0.0;
    }
    else if (*type != "FXEQ")
        unit["kUIParamMixOrGain"] = 0.0;
    unit["type"] = *resolver_.schema().fxTypeId(*type);

    shiftFxModRoutes(rack, position, +1);
    list.insert(list.begin() + position, std::move(unit));

    // Find which instance of this type the new unit is, to address it.
    int instance = 0;
    for (const auto& [at, t] : fxUnits())
    {
        if (at.rack == rack && at.position == position)
            break;
        if (t == *type)
            ++instance;
    }
    const auto owner = "fx." + TargetResolver::fxFriendly(*type) + (instance > 0 ? std::to_string(instance + 1) : "");
    report.changes.push_back("added " + owner + (rack == 0 ? "" : " on FX bus " + std::to_string(rack)) + " at position "
                             + std::to_string(position + 1));

    // A unit without kParamWet is fully wet. Start from the schema's usual mix
    // (e.g. reverb 30%) unless the caller sets one.
    Json params = edit.value("params", Json::object());
    bool hasMix = false;
    for (const auto& [k, v] : params.items())
        hasMix = hasMix || resolver_.resolve(owner + "." + k).key == "kParamWet";
    if (!hasMix)
        if (const auto* def = resolver_.schema().param(*type, "kParamWet"); def && def->defaultValue)
            params["mix"] = *def->defaultValue;
    for (const auto& [k, v] : params.items())
        write(resolver_.resolve(owner + "." + k), v, report);
}

void PresetEditor::removeFx(const Json& edit, EditReport& report)
{
    const auto name = edit["remove_fx"].get<std::string>();
    const auto type = resolver_.fxType(name);
    if (!type)
        throw std::invalid_argument("unknown FX \"" + name + "\"");
    const int instance = std::max(1, edit.value("instance", 1)) - 1;
    const auto at = findFx(*type, instance);
    if (!at)
    {
        report.warnings.push_back("no " + TargetResolver::fxFriendly(*type) + " to remove");
        return;
    }
    // Drop routes into the unit, then close the gap in later positions.
    const int destId = at->rack * 100 + at->position;
    for (const int slot : ed_.usedModSlots())
    {
        const auto& entry = preset_.data["ModSlot" + std::to_string(slot)];
        if (entry.value("destModuleTypeString", std::string()) == *type && entry.value("destModuleID", -1) == destId)
            ed_.clearModSlot(slot);
    }
    auto& list = preset_.data["FXRack" + std::to_string(at->rack)]["FX"];
    list.erase(list.begin() + at->position);
    shiftFxModRoutes(at->rack, at->position + 1, -1);
    report.changes.push_back("removed fx." + TargetResolver::fxFriendly(*type) + (instance > 0 ? std::to_string(instance + 1) : ""));
}

// --------------------------------------------------------------- mod matrix

void PresetEditor::addMod(const Json& spec, EditReport& report)
{
    const auto sourceName = spec.value("source", std::string());
    const auto source = resolver_.modSource(sourceName);
    if (!source)
        throw std::invalid_argument("unknown mod source \"" + sourceName + "\"; use lfo1-10, env2-4, macro1-8, velocity, "
                                    "modwheel, aftertouch, keytrack, random");
    const auto t = resolver_.resolve(spec.value("target", std::string()));
    FxLocation at;
    if (t.kind == Target::Kind::Fx)
    {
        const auto found = findFx(t.fxType, t.fxInstance);
        if (!found)
            throw std::invalid_argument("this preset has no " + t.owner + " unit");
        at = *found;
    }
    const auto dest = resolver_.modDest(t, at.rack, at.position);
    if (!dest)
        throw std::invalid_argument(t.label + " can't be a mod destination yet (unknown mod ID); pick a nearby target");
    if (!spec.contains("amount") || !spec["amount"].is_number())
        throw std::invalid_argument("mod needs an amount from -100 to 100");
    const double amount = std::clamp(spec["amount"].get<double>(), -100.0, 100.0);
    const bool bipolar = spec.value("bipolar", false);

    for (const int slot : ed_.usedModSlots())
    {
        const auto r = ed_.modRoute(slot);
        if (r && r->source == *source && r->aux == 0 && r->dest.type == dest->type && r->dest.id == dest->id
            && r->dest.paramName == dest->paramName)
        {
            auto& p = preset_.data["ModSlot" + std::to_string(slot)]["plainParams"];
            if (!p.is_object())
                p = Json::object();
            p["kParamAmount"] = amount;
            if (bipolar)
                p["kParamBipolar"] = 1.0;
            else
                p.erase("kParamBipolar");
            report.changes.push_back(describeSlot(resolver_.modSourceLabel(*source), t.label, r->amount) + " → "
                                     + formatValue(amount) + "%");
            return;
        }
    }
    serum2::ModRoute route;
    route.source = *source;
    route.dest = *dest;
    route.amount = amount;
    route.bipolar = bipolar;
    if (ed_.addModRoute(route) < 0)
        throw std::invalid_argument("the mod matrix is full (64 slots)");
    report.changes.push_back("new route " + describeSlot(resolver_.modSourceLabel(*source), t.label, amount)
                             + (bipolar ? ", bipolar" : ""));
}

void PresetEditor::removeMod(const Json& spec, EditReport& report)
{
    std::optional<int> source;
    if (spec.contains("source"))
    {
        source = resolver_.modSource(spec["source"].get<std::string>());
        if (!source)
            throw std::invalid_argument("unknown mod source");
    }
    std::optional<Target> t;
    std::optional<serum2::ModDest> dest;
    if (spec.contains("target"))
    {
        t = resolver_.resolve(spec["target"].get<std::string>());
        FxLocation at;
        if (t->kind == Target::Kind::Fx)
            if (const auto found = findFx(t->fxType, t->fxInstance))
                at = *found;
        dest = resolver_.modDest(*t, at.rack, at.position);
    }
    if (!source && !t)
        throw std::invalid_argument("unmod needs a source, a target or both");
    int removed = 0;
    for (const int slot : ed_.usedModSlots())
    {
        const auto r = ed_.modRoute(slot);
        if (!r || (source && r->source != *source))
            continue;
        if (t && (!dest || r->dest.type != dest->type || r->dest.id != dest->id || r->dest.paramName != dest->paramName))
            continue;
        ed_.clearModSlot(slot);
        ++removed;
    }
    if (removed == 0)
        report.warnings.push_back("no matching mod routes to remove");
    else
        report.changes.push_back("removed " + std::to_string(removed) + " mod route" + (removed == 1 ? "" : "s"));
}

// ---------------------------------------------------------- engines & misc

void PresetEditor::setWavetable(const Json& spec, EditReport& report)
{
    const int osc = parseOsc(spec.value("osc", Json("A")));
    const auto id = normalize(spec.value("table", std::string()));
    const serum2::Wavetable* table = nullptr;
    for (const auto& wt : resolver_.schema().wavetables())
        if (normalize(wt.id) == id || normalize(wt.path) == id + "wav")
            table = &wt;
    if (!table)
    {
        std::string ids;
        for (const auto& wt : resolver_.schema().wavetables())
            ids += (ids.empty() ? "" : ", ") + wt.id;
        throw std::invalid_argument("unknown wavetable; available: " + ids);
    }
    const auto label = std::string("osc") + static_cast<char>('A' + osc);
    const auto before = ed_.wavetablePath(osc);
    ed_.setWavetable(osc, *table);
    ed_.setFlag(Module::osc(osc), "kParamEnable", true);
    report.changes.push_back(label + ".wavetable: " + (before.empty() ? "embedded" : before) + " → " + table->path);
    if (spec.contains("frame"))
    {
        Json edit = { { "target", label + ".wt_pos" }, { "set", spec["frame"] } };
        setTarget(label + ".wt_pos", edit, report);
    }
}

void PresetEditor::setLfoShape(int lfo, const std::string& shapeName, EditReport& report)
{
    const auto shape = lower(shapeName);
    const auto m = Module::lfo(lfo);
    const auto label = "lfo" + std::to_string(lfo + 1);
    if (shape == "triangle" || shape == "tri")
    {
        ed_.erase(m, "kParamType");
        ed_.clearLfoCurve(lfo);
    }
    else if (shape == "random" || shape == "sample_hold" || shape == "s&h" || shape == "lorenz" || shape == "rossler")
        ed_.setLfoChaos(lfo, shape == "lorenz" ? "Lorenz" : shape == "rossler" ? "Rossler" : "RandomSH");
    else
    {
        const auto pts = shapePoints(shape);
        if (pts.empty())
        {
            std::string names;
            for (const auto& s : lfoShapes())
                names += (names.empty() ? "" : ", ") + s;
            throw std::invalid_argument("unknown LFO shape \"" + shapeName + "\"; use " + names);
        }
        ed_.erase(m, "kParamType");
        ed_.setLfoCurve(lfo, pts);
    }
    report.changes.push_back(label + ".shape: " + shape);
}

void PresetEditor::rename(const std::string& name, EditReport& report)
{
    if (name.empty())
        throw std::invalid_argument("name can't be empty");
    const auto before = preset_.metadata.value("presetName", std::string());
    preset_.metadata["presetName"] = name;
    preset_.data["presetName"] = name;
    report.changes.push_back("name: \"" + before + "\" → \"" + name + "\"");
}
} // namespace tts
