#include "serum2/Patch.h"

#include <algorithm>
#include <cmath>

namespace serum2
{
namespace
{
std::string indexed(std::string_view name, int i)
{
    return std::string(name) + std::to_string(i);
}

bool sameValue(const Json& a, const Json& b)
{
    if (a.is_string() || b.is_string())
        return a == b;
    const auto toDouble = [](const Json& v) {
        return v.is_boolean() ? (v.get<bool>() ? 1.0 : 0.0) : v.get<double>();
    };
    if (!(a.is_number() || a.is_boolean()) || !(b.is_number() || b.is_boolean()))
        return false;
    return std::abs(toDouble(a) - toDouble(b)) < 1e-9;
}

double toNumber(const Json& v)
{
    if (v.is_boolean())
        return v.get<bool>() ? 1.0 : 0.0;
    if (v.is_number())
        return v.get<double>();
    return 0.0;
}
} // namespace

Module Module::osc(int i)
{
    return { { indexed("Oscillator", i) }, "Oscillator", i };
}

Module Module::wavetable(int i)
{
    return { { indexed("Oscillator", i), indexed("WTOsc", i) }, "WTOsc", i };
}

Module Module::multisample(int i)
{
    return { { indexed("Oscillator", i), indexed("MultiSampleOsc", i) }, "MultiSampleOsc", i };
}

Module Module::noise()
{
    return { { "Oscillator3", "NoiseOsc3" }, "NoiseOsc", 3 };
}

Module Module::sub()
{
    return { { "Oscillator4", "SubOsc4" }, "SubOsc", 4 };
}

Module Module::filter(int i)
{
    return { { indexed("VoiceFilter", i) }, "VoiceFilter", i };
}

Module Module::env(int i)
{
    return { { indexed("Env", i) }, "Env", i };
}

Module Module::lfo(int i)
{
    return { { indexed("LFO", i) }, "LFO", i };
}

Module Module::macro(int i)
{
    return { { indexed("Macro", i) }, "Macro", i };
}

Module Module::global()
{
    return { { "Global0" }, "Global", 0 };
}

Module Module::routing(int i)
{
    return { { indexed("RoutingSlot", i) }, "RoutingSlot", i };
}

PatchEditor::PatchEditor(Json& data, const Schema& schema)
    : data_(data), schema_(schema)
{
}

Json& PatchEditor::container(const Module& m)
{
    Json* node = &data_;
    for (const auto& key : m.path)
    {
        auto& child = (*node)[key];
        if (!child.is_object())
            child = Json::object();
        node = &child;
    }
    return *node;
}

const Json* PatchEditor::find(const Module& m) const
{
    const Json* node = &data_;
    for (const auto& key : m.path)
    {
        if (!node->is_object() || !node->contains(key))
            return nullptr;
        node = &(*node)[key];
    }
    return node;
}

Json& PatchEditor::params(const Module& m)
{
    auto& c = container(m);
    auto& p = c["plainParams"];
    if (!p.is_object())
        p = Json::object();
    return p;
}

const Json* PatchEditor::paramsIfPresent(const Module& m) const
{
    const auto* c = find(m);
    if (!c || !c->contains("plainParams"))
        return nullptr;
    const auto& p = (*c)["plainParams"];
    return p.is_object() ? &p : nullptr;
}

const ParamDef* PatchEditor::def(const Module& m, std::string_view key) const
{
    return schema_.param(m.schemaModule, key);
}

void PatchEditor::tidy(const Module& m)
{
    auto& c = container(m);
    if (c["plainParams"].is_object() && c["plainParams"].empty())
        c["plainParams"] = "default";
}

bool PatchEditor::has(const Module& m, std::string_view key) const
{
    const auto* p = paramsIfPresent(m);
    return p && p->contains(key);
}

double PatchEditor::number(const Module& m, std::string_view key) const
{
    if (const auto* p = paramsIfPresent(m); p && p->contains(key))
        return toNumber((*p)[std::string(key)]);
    if (const auto* d = def(m, key))
        if (auto v = d->defaultFor(m.slot))
            return toNumber(*v);
    return 0.0;
}

bool PatchEditor::flag(const Module& m, std::string_view key) const
{
    return number(m, key) >= 0.5;
}

std::string PatchEditor::text(const Module& m, std::string_view key) const
{
    if (const auto* p = paramsIfPresent(m); p && p->contains(key) && (*p)[std::string(key)].is_string())
        return (*p)[std::string(key)].get<std::string>();
    if (const auto* d = def(m, key))
        if (auto v = d->defaultFor(m.slot); v && v->is_string())
            return v->get<std::string>();
    return {};
}

void PatchEditor::set(const Module& m, std::string_view key, double value)
{
    const auto* d = def(m, key);
    if (d)
    {
        value = d->clamp(value);
        if (auto fallback = d->defaultFor(m.slot); fallback && sameValue(*fallback, value))
        {
            erase(m, key);
            return;
        }
    }
    params(m)[std::string(key)] = value;
}

void PatchEditor::setFlag(const Module& m, std::string_view key, bool value)
{
    set(m, key, value ? 1.0 : 0.0);
}

void PatchEditor::setText(const Module& m, std::string_view key, std::string_view value)
{
    if (const auto* d = def(m, key))
        if (auto fallback = d->defaultFor(m.slot); fallback && fallback->is_string() && fallback->get_ref<const std::string&>() == value)
        {
            erase(m, key);
            return;
        }
    params(m)[std::string(key)] = std::string(value);
}

void PatchEditor::force(const Module& m, std::string_view key, Json value)
{
    if (value.is_boolean())
        value = value.get<bool>() ? 1.0 : 0.0;
    else if (value.is_number())
        value = value.get<double>();
    params(m)[std::string(key)] = std::move(value);
}

void PatchEditor::erase(const Module& m, std::string_view key)
{
    auto* c = find(m) ? &container(m) : nullptr;
    if (!c || !(*c)["plainParams"].is_object())
        return;
    (*c)["plainParams"].erase(std::string(key));
    tidy(m);
}

void PatchEditor::setWavetable(int osc, const Wavetable& table)
{
    erase(Module::osc(osc), "kParamType"); // kOsc_WT is the default engine
    auto& c = container(Module::wavetable(osc));
    // Leave any embedded table data behind: the file reference replaces it.
    c.erase("embeddedWTData");
    c.erase("interpolateAfterLoad");
    c.erase("tableDisplayName");
    if (!c.contains("flex"))
        c["flex"] = Json::object();
    c["relativePathToWT"] = table.path;
    c["numFrames"] = table.numFrames;
    c["sampleRate"] = table.sampleRate;
    c["numChannels"] = table.numChannels;
    if (!c.contains("plainParams"))
        c["plainParams"] = "default";
}

void PatchEditor::setMultisample(int osc, const MultiSample& instrument)
{
    force(Module::osc(osc), "kParamType", "kOsc_MultiSample");
    auto& c = container(Module::multisample(osc));
    for (const auto& [key, value] : instrument.container.items())
        c[key] = value;
    if (!c.contains("plainParams"))
        c["plainParams"] = "default";
}

std::string PatchEditor::wavetablePath(int osc) const
{
    const auto* c = find(Module::wavetable(osc));
    if (!c || !c->contains("relativePathToWT"))
        return {};
    return (*c)["relativePathToWT"].get<std::string>();
}

void PatchEditor::setNoiseType(std::string_view type)
{
    // Choosing a generated noise type drops the noise sample reference, as
    // Serum does when you pick one in the UI.
    container(Module::noise()).erase("relativePathToNoiseSample");
    force(Module::noise(), "kParamNoiseType", std::string(type));
}

void PatchEditor::setLfoCurve(int lfo, std::span<const CurvePoint> points)
{
    auto& c = container(Module::lfo(lfo));
    Json xs = Json::array();
    Json ys = Json::array();
    Json tensions = Json::array();
    for (const auto& p : points)
    {
        xs.push_back(p.x);
        ys.push_back(1.0 - p.y);
        tensions.push_back(p.tension);
    }
    c["curveData"] = Json::object();
    c["curveData"]["curveVals"] = std::move(tensions);
    c["curveData"]["numPoints"] = static_cast<int>(points.size()) - 1;
    c["curveData"]["xVals"] = std::move(xs);
    c["curveData"]["yVals"] = std::move(ys);
    c["curveDisplayName"] = "Custom";
    if (!c.contains("pathData"))
        c["pathData"] = Json::object();
}

void PatchEditor::clearLfoCurve(int lfo)
{
    auto& c = container(Module::lfo(lfo));
    c["curveData"] = Json::object();
    c.erase("curveDisplayName");
}

void PatchEditor::setLfoChaos(int lfo, std::string_view type)
{
    clearLfoCurve(lfo);
    force(Module::lfo(lfo), "kParamType", std::string(type));
}

void PatchEditor::setMacroName(int macro, std::string_view name)
{
    container(Module::macro(macro))["name"] = std::string(name);
}

std::string PatchEditor::macroName(int macro) const
{
    const auto* c = find(Module::macro(macro));
    if (!c || !c->contains("name") || !(*c)["name"].is_string())
        return {};
    return (*c)["name"].get<std::string>();
}

bool PatchEditor::isModSlotUsed(int slot) const
{
    const auto key = indexed("ModSlot", slot);
    return data_.contains(key) && data_[key].contains("source");
}

int PatchEditor::addModRoute(const ModRoute& route)
{
    for (int slot = 0; slot < kNumModSlots; ++slot)
    {
        if (isModSlotUsed(slot))
            continue;
        Json entry = Json::object();
        entry["destModuleID"] = route.dest.id;
        entry["destModuleParamID"] = route.dest.paramId;
        entry["destModuleParamName"] = route.dest.paramName;
        entry["destModuleTypeString"] = route.dest.type;
        entry["plainParams"] = Json::object();
        entry["plainParams"]["kParamAmount"] = std::clamp(route.amount, -100.0, 100.0);
        if (route.bipolar)
            entry["plainParams"]["kParamBipolar"] = 1.0;
        entry["source"] = Json::array({ route.source, route.aux });
        data_[indexed("ModSlot", slot)] = std::move(entry);
        return slot;
    }
    return -1;
}

std::vector<int> PatchEditor::usedModSlots() const
{
    std::vector<int> used;
    for (int slot = 0; slot < kNumModSlots; ++slot)
        if (isModSlotUsed(slot))
            used.push_back(slot);
    return used;
}

void PatchEditor::clearModSlot(int slot)
{
    data_[indexed("ModSlot", slot)] = Json { { "plainParams", "default" } };
}

std::optional<ModRoute> PatchEditor::modRoute(int slot) const
{
    if (!isModSlotUsed(slot))
        return std::nullopt;
    const auto& e = data_[indexed("ModSlot", slot)];
    ModRoute r;
    r.source = e["source"][0].get<int>();
    r.aux = e["source"].size() > 1 ? e["source"][1].get<int>() : 0;
    r.dest.type = e.value("destModuleTypeString", "");
    r.dest.id = e.value("destModuleID", 0);
    r.dest.paramName = e.value("destModuleParamName", "");
    r.dest.paramId = e.value("destModuleParamID", 0);
    if (e["plainParams"].is_object())
    {
        r.amount = toNumber(e["plainParams"].value("kParamAmount", Json(0.0)));
        r.bipolar = toNumber(e["plainParams"].value("kParamBipolar", Json(0.0))) >= 0.5;
    }
    return r;
}

std::vector<FxUnit> PatchEditor::fxRack(int rack) const
{
    std::vector<FxUnit> units;
    const auto key = indexed("FXRack", rack);
    if (!data_.contains(key) || !data_[key].contains("FX"))
        return units;
    for (const auto& entry : data_[key]["FX"])
    {
        const auto name = schema_.fxTypeName(entry.value("type", -1));
        if (!name)
            continue;
        FxUnit unit { *name, Json::object() };
        if (entry.contains(*name) && entry[*name]["plainParams"].is_object())
            unit.params = entry[*name]["plainParams"];
        units.push_back(std::move(unit));
    }
    return units;
}

void PatchEditor::setFxRack(int rack, const std::vector<FxUnit>& units)
{
    auto& r = data_[indexed("FXRack", rack)];
    if (!r.is_object())
        r = Json { { "FX", Json::array() }, { "displayName", "" }, { "plainParams", "default" } };
    Json list = Json::array();
    for (const auto& unit : units)
    {
        const auto typeId = schema_.fxTypeId(unit.type);
        if (!typeId)
            continue;
        Json p = Json::object();
        for (const auto& [key, value] : unit.params.items())
        {
            if (key == "kParamWet" && value.is_number() && std::abs(value.get<double>() - kFxFullyWet) < 1e-9)
                continue;
            if (const auto* d = schema_.param(unit.type, key); d && value.is_number())
                p[key] = d->clamp(value.get<double>());
            else if (value.is_boolean())
                p[key] = value.get<bool>() ? 1.0 : 0.0;
            else
                p[key] = value;
        }
        Json entry = Json::object();
        entry[unit.type] = Json { { "plainParams", p.empty() ? Json("default") : p } };
        if (unit.type != "FXEQ")
            entry["kUIParamMixOrGain"] = 0.0;
        entry["type"] = *typeId;
        list.push_back(std::move(entry));
    }
    r["FX"] = std::move(list);
}
} // namespace serum2
