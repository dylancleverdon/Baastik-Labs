#include "serum2/Schema.h"

#include <algorithm>
#include <cmath>

#include "serum2/embedded/Resources.h"

namespace serum2
{
namespace
{
Json loadEmbedded(std::string_view name)
{
    const auto text = embedded::find(name);
    if (!text)
        throw std::runtime_error("missing embedded resource " + std::string(name));
    return Json::parse(*text);
}

ParamDef parseParam(const Json& j)
{
    ParamDef def;
    const auto kind = j.value("kind", "float");
    def.kind = kind == "bool" ? ParamDef::Kind::Bool
             : kind == "enum" ? ParamDef::Kind::Enum
                              : ParamDef::Kind::Float;
    if (j.contains("default") && !j["default"].is_null())
        def.defaultValue = j["default"];
    if (j.contains("min"))
        def.min = j["min"].get<double>();
    if (j.contains("max"))
        def.max = j["max"].get<double>();
    if (j.contains("enum"))
        def.enumValues = j["enum"].get<std::vector<std::string>>();
    if (j.contains("slotDefaults"))
        for (const auto& [slot, value] : j["slotDefaults"].items())
            def.slotDefaults[std::stoi(slot)] = value;
    return def;
}

ParamTable parseTable(const Json& j)
{
    ParamTable table;
    for (const auto& [key, value] : j.items())
        table.emplace(key, parseParam(value));
    return table;
}

bool containsTag(const std::vector<std::string>& tags, std::string_view tag)
{
    return std::find(tags.begin(), tags.end(), tag) != tags.end();
}
} // namespace

std::optional<Json> ParamDef::defaultFor(int slot) const
{
    if (const auto it = slotDefaults.find(slot); it != slotDefaults.end())
        return it->second;
    return defaultValue;
}

double ParamDef::clamp(double value) const
{
    if (min)
        value = std::max(value, *min);
    if (max)
        value = std::min(value, *max);
    return value;
}

bool Wavetable::hasTag(std::string_view tag) const
{
    return containsTag(tags, tag);
}

bool MultiSample::hasTag(std::string_view tag) const
{
    return containsTag(tags, tag);
}

const Schema& Schema::builtin()
{
    static const Schema schema = fromJson(loadEmbedded("serum2_schema.json"),
                                          loadEmbedded("multisamples.json"));
    return schema;
}

Preset Schema::initPreset()
{
    static const Json init = loadEmbedded("init_template.json");
    return Preset { init["metadata"], init["data"] };
}

Schema Schema::fromJson(const Json& j, const Json& multisamples)
{
    Schema s;
    for (const auto& [name, table] : j["modules"].items())
        s.modules_.emplace(name, parseTable(table));
    for (const auto& [name, table] : j["fx"].items())
        s.fx_.emplace(name, parseTable(table));
    for (const auto& [name, id] : j["fxTypeIds"].items())
        s.fxTypeIds_.emplace(name, id.get<int>());
    s.fxWetParamId_ = j.value("fxWetParamId", 1);
    for (const auto& [fxType, extras] : j["fxExtraModDests"].items())
        for (const auto& [shortName, dest] : extras.items())
            s.fxExtraDests_[fxType][shortName] = { dest["param_name"].get<std::string>(), dest["param_id"].get<int>() };
    for (const auto& [name, id] : j["modSources"].items())
        s.modSources_.emplace(name, id.get<int>());
    for (const auto& [name, d] : j["modDests"].items())
        s.modDests_.emplace(name, ModDest { d["dest_type"].get<std::string>(), d["dest_id"].get<int>(),
                                            d["param_name"].get<std::string>(), d["param_id"].get<int>() });
    for (const auto& [id, wt] : j["wavetables"].items())
    {
        Wavetable w;
        w.id = id;
        w.path = wt["path"].get<std::string>();
        w.numFrames = wt["numFrames"].get<int>();
        w.sampleRate = wt["sampleRate"].get<int>();
        w.numChannels = wt["numChannels"].get<int>();
        w.tags = wt.value("tags", std::vector<std::string> {});
        if (wt.contains("knownFrames"))
            for (const auto& [frame, pos] : wt["knownFrames"].items())
                w.knownFrames.emplace(frame, pos.get<double>());
        s.wavetables_.push_back(std::move(w));
    }
    for (const auto& [id, ms] : multisamples.items())
    {
        MultiSample m;
        m.id = id;
        m.container = Json::object();
        m.container["sfzPathRelative"] = ms["sfzPathRelative"];
        m.container["embedded_sfz"] = ms["embedded_sfz"];
        m.container["files"] = ms["files"];
        m.tags = ms.value("tags", std::vector<std::string> {});
        s.multisamples_.push_back(std::move(m));
    }
    for (const auto& [name, raw] : j["lfoSyncRates"].items())
        s.lfoSyncRates_.emplace(name, raw.get<double>());
    return s;
}

const ParamTable* Schema::module(std::string_view name) const
{
    const auto it = modules_.find(name);
    return it == modules_.end() ? nullptr : &it->second;
}

const ParamDef* Schema::param(std::string_view moduleName, std::string_view key) const
{
    const auto* table = module(moduleName);
    if (!table)
        table = fxParams(moduleName);
    if (!table)
        return nullptr;
    const auto it = table->find(key);
    return it == table->end() ? nullptr : &it->second;
}

const ParamTable* Schema::fxParams(std::string_view fxType) const
{
    const auto it = fx_.find(fxType);
    return it == fx_.end() ? nullptr : &it->second;
}

std::optional<int> Schema::fxTypeId(std::string_view fxType) const
{
    const auto it = fxTypeIds_.find(fxType);
    return it == fxTypeIds_.end() ? std::nullopt : std::optional<int>(it->second);
}

std::optional<std::string> Schema::fxTypeName(int typeId) const
{
    for (const auto& [name, id] : fxTypeIds_)
        if (id == typeId)
            return name;
    return std::nullopt;
}

std::vector<std::string> Schema::fxTypes() const
{
    std::vector<std::string> names;
    for (const auto& [name, id] : fxTypeIds_)
        names.push_back(name);
    return names;
}

std::optional<int> Schema::modSource(std::string_view name) const
{
    const auto it = modSources_.find(name);
    return it == modSources_.end() ? std::nullopt : std::optional<int>(it->second);
}

std::optional<std::string> Schema::modSourceName(int id) const
{
    for (const auto& [name, sourceId] : modSources_)
        if (sourceId == id)
            return name;
    return std::nullopt;
}

std::optional<ModDest> Schema::modDest(std::string_view name) const
{
    const auto it = modDests_.find(name);
    return it == modDests_.end() ? std::nullopt : std::optional<ModDest>(it->second);
}

std::optional<ModDest> Schema::fxModDest(std::string_view fxType, int rack, int position,
                                         std::string_view param) const
{
    const int destId = rack * 100 + position;
    if (param == "wet")
    {
        if (const auto* def = this->param(fxType, "kParamWet"); def == nullptr)
            return std::nullopt;
        return ModDest { std::string(fxType), destId, "kParamWet", fxWetParamId_ };
    }
    const auto typeIt = fxExtraDests_.find(fxType);
    if (typeIt == fxExtraDests_.end())
        return std::nullopt;
    const auto it = typeIt->second.find(param);
    if (it == typeIt->second.end())
        return std::nullopt;
    return ModDest { std::string(fxType), destId, it->second.first, it->second.second };
}

const Wavetable* Schema::wavetable(std::string_view id) const
{
    for (const auto& w : wavetables_)
        if (w.id == id)
            return &w;
    return nullptr;
}

const MultiSample* Schema::multisample(std::string_view id) const
{
    for (const auto& m : multisamples_)
        if (m.id == id)
            return &m;
    return nullptr;
}
} // namespace serum2
