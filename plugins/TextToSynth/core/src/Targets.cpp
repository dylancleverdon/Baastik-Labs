#include "tts/Targets.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <stdexcept>

#include "serum2/embedded/Resources.h"

namespace tts
{
using serum2::Module;

namespace
{
// Friendly names for each schema module's parameters. The first name listed
// for a key is the one tools report back. Anything not listed is still
// reachable by its kParam name without the prefix ("kParamRate10x" -> "rate10x").
const std::map<std::string, std::vector<std::pair<std::string, std::string>>, std::less<>>& aliases()
{
    static const std::map<std::string, std::vector<std::pair<std::string, std::string>>, std::less<>> table {
        { "Oscillator",
          { { "on", "kParamEnable" }, { "enabled", "kParamEnable" }, { "enable", "kParamEnable" },
            { "level", "kParamVolume" }, { "volume", "kParamVolume" }, { "vol", "kParamVolume" },
            { "pan", "kParamPan" }, { "octave", "kParamOctave" }, { "oct", "kParamOctave" },
            { "semi", "kParamCoarsePit" }, { "semitones", "kParamCoarsePit" }, { "coarse", "kParamCoarsePit" },
            { "fine", "kParamFine" }, { "cents", "kParamFine" }, { "pitch", "kParamPitch" },
            { "unison", "kParamUnison" }, { "voices", "kParamUnison" },
            { "detune", "kParamDetune" }, { "unison_width", "kParamDetuneWid" }, { "blend", "kParamDetuneWid" },
            { "stereo", "kParamUnisonStereo" }, { "width", "kParamUnisonStereo" },
            { "pitch_track", "kParamPitchTrack" }, { "engine", "kParamType" } } },
        { "WTOsc",
          { { "wt_pos", "kParamTablePos" }, { "position", "kParamTablePos" }, { "wtpos", "kParamTablePos" },
            { "table_position", "kParamTablePos" }, { "frame", "kParamTablePos" },
            { "warp", "kParamWarp" }, { "warp_amount", "kParamWarp" }, { "warp_mode", "kParamWarpMenu" },
            { "warp2", "kParamWarp2" }, { "warp2_mode", "kParamWarpMenu2" },
            { "phase", "kParamInitialPhase" }, { "random_phase", "kParamRandomPhase" }, { "rand", "kParamRandomPhase" } } },
        { "MultiSampleOsc",
          { { "timbre", "kParamTimbreShift" }, { "attack", "kParamEnvAttack" }, { "decay", "kParamEnvDecay" },
            { "sustain", "kParamEnvSustain" }, { "release", "kParamEnvRelease" } } },
        { "NoiseOsc", { { "type", "kParamNoiseType" }, { "color", "kParamColor" }, { "colour", "kParamColor" }, { "one_shot", "kParamOneShot" } } },
        { "SubOsc", { { "shape", "kParamShape" } } },
        { "VoiceFilter",
          { { "on", "kParamEnable" }, { "enabled", "kParamEnable" }, { "enable", "kParamEnable" },
            { "type", "kParamType" }, { "cutoff", "kParamFreq" }, { "freq", "kParamFreq" }, { "frequency", "kParamFreq" },
            { "resonance", "kParamReso" }, { "res", "kParamReso" }, { "reso", "kParamReso" }, { "q", "kParamReso" },
            { "drive", "kParamDrive" }, { "var", "kParamVar" }, { "mix", "kParamWet" }, { "wet", "kParamWet" },
            { "level", "kParamLevelOut" }, { "stereo", "kParamStereo" }, { "spread", "kParamStereo" },
            { "keytrack", "kParamKeyTrack" }, { "key_track", "kParamKeyTrack" } } },
        { "Env",
          { { "attack", "kParamAttack" }, { "a", "kParamAttack" }, { "hold", "kParamHold" }, { "h", "kParamHold" },
            { "decay", "kParamDecay" }, { "d", "kParamDecay" }, { "sustain", "kParamSustain" }, { "s", "kParamSustain" },
            { "release", "kParamRelease" }, { "r", "kParamRelease" },
            { "attack_curve", "kParamCurve1" }, { "decay_curve", "kParamCurve2" }, { "release_curve", "kParamCurve3" } } },
        { "LFO",
          { { "rate", "kParamRate" }, { "speed", "kParamRate" }, { "sync", "kParamBeatSync" }, { "beat_sync", "kParamBeatSync" },
            { "mode", "kParamMode" }, { "rise", "kParamRise" }, { "fade_in", "kParamRise" }, { "smooth", "kParamSmooth" },
            { "delay", "kParamDelay" }, { "mono", "kParamMono" }, { "dotted", "kParamDotted" }, { "triplet", "kParamTriplets" },
            { "triplets", "kParamTriplets" } } },
        { "Macro", { { "value", "kParamValue" }, { "amount", "kParamValue" } } },
        { "Global",
          { { "volume", "kParamMasterVolume" }, { "master", "kParamMasterVolume" }, { "master_volume", "kParamMasterVolume" },
            { "mono", "kParamMonoToggle" }, { "voices", "kParamPolyCount" }, { "polyphony", "kParamPolyCount" },
            { "glide", "kParamPortamentoTime" }, { "portamento", "kParamPortamentoTime" }, { "porta", "kParamPortamentoTime" },
            { "glide_always", "kParamPortaAlways" }, { "legato", "kParamLegato" },
            { "bend_up", "kParamBendRangeUp" }, { "bend_down", "kParamBendRangeDn" },
            { "transpose", "kParamTranspose" }, { "tuning", "kParamGlobalTuning" }, { "oversampling", "kParamOversampling" } } },
    };
    return table;
}

// Friendly names for FX parameters, shared across FX types.
const std::vector<std::pair<std::string, std::string>>& fxAliases()
{
    static const std::vector<std::pair<std::string, std::string>> table {
        { "mix", "kParamWet" },       { "wet", "kParamWet" },           { "threshold", "kParamThresh" },
        { "thresh", "kParamThresh" }, { "makeup", "kParamMakeup" },     { "gain", "kParamMakeup" },
        { "time_l", "kParamTimeL" },  { "time_r", "kParamTimeR" },      { "cutoff", "kParamFreq" },
        { "res", "kParamReso" },      { "resonance", "kParamReso" },    { "low_freq", "kParamFreq1" },
        { "low_gain", "kParamGain1" }, { "high_freq", "kParamFreq2" },  { "high_gain", "kParamGain2" },
        { "low_q", "kParamReso1" },   { "high_q", "kParamReso2" },      { "hpf", "kParamHPF" },
        { "lpf", "kParamLPF" },       { "sync", "kParamBeatSync" },
    };
    return table;
}

const std::map<std::string, std::string, std::less<>>& fxTypeAliases()
{
    static const std::map<std::string, std::string, std::less<>> table {
        { "distortion", "FXDistortion" }, { "dist", "FXDistortion" }, { "drive", "FXDistortion" }, { "saturation", "FXDistortion" },
        { "chorus", "FXChorus" }, { "flanger", "FXFlanger" }, { "phaser", "FXPhaser" },
        { "delay", "FXDelay" }, { "echo", "FXDelay" }, { "reverb", "FXReverb" }, { "verb", "FXReverb" },
        { "compressor", "FXComp" }, { "comp", "FXComp" }, { "eq", "FXEQ" }, { "equalizer", "FXEQ" },
        { "filter", "FXFilter" }, { "bode", "FXBode" }, { "frequency_shifter", "FXBode" }, { "freqshift", "FXBode" },
        { "hyper", "FXHyperD" }, { "hyperdimension", "FXHyperD" }, { "dimension", "FXHyperD" },
        { "convolve", "FXConv" }, { "conv", "FXConv" }, { "utility", "FXUtils" }, { "utils", "FXUtils" },
        { "util", "FXUtils" },
    };
    return table;
}

const std::map<std::string, std::string, std::less<>>& sourceAliases()
{
    static const std::map<std::string, std::string, std::less<>> table {
        { "modwheel", "mod_wheel" },  { "mw", "mod_wheel" },           { "velocity", "velocity" },
        { "vel", "velocity" },        { "keytrack", "key_track" },     { "note", "key_track" },
        { "aftertouch", "aftertouch" }, { "at", "aftertouch" },        { "polyaftertouch", "poly_aftertouch" },
        { "random", "random1" },      { "rand", "random1" },           { "random2", "random2" },
        { "pitchbend", "pitch_bend" }, { "bend", "pitch_bend" },       { "releasevelocity", "release_velo" },
    };
    return table;
}

std::string stripPrefix(std::string_view key)
{
    constexpr std::string_view prefix = "kParam";
    if (key.substr(0, prefix.size()) == prefix)
        key.remove_prefix(prefix.size());
    return std::string(key);
}

// "kParamFreq" and "freq" both normalize to "freq".
std::string keyNorm(std::string_view key)
{
    return normalize(stripPrefix(key));
}

// Parses a trailing slot number: "filter2" -> ("filter", 2).
std::pair<std::string, int> splitSlot(const std::string& name)
{
    std::size_t i = name.size();
    while (i > 0 && std::isdigit(static_cast<unsigned char>(name[i - 1])))
        --i;
    if (i == name.size() || i == 0)
        return { name, 0 };
    return { name.substr(0, i), std::stoi(name.substr(i)) };
}

std::string join(const std::vector<std::string>& items, std::string_view sep = ", ")
{
    std::string out;
    for (const auto& s : items)
    {
        if (!out.empty())
            out += sep;
        out += s;
    }
    return out;
}

std::string oscLetter(int slot)
{
    switch (slot)
    {
        case 0: return "oscA";
        case 1: return "oscB";
        case 2: return "oscC";
        case 3: return "noise";
        case 4: return "sub";
        default: return "osc" + std::to_string(slot);
    }
}

Json paramInfo(const serum2::ParamDef& def)
{
    Json j = Json::object();
    switch (def.kind)
    {
        case serum2::ParamDef::Kind::Bool: j["kind"] = "on/off"; break;
        case serum2::ParamDef::Kind::Enum: j["kind"] = "choice"; break;
        case serum2::ParamDef::Kind::Float: j["kind"] = "number"; break;
    }
    if (def.min)
        j["min"] = *def.min;
    if (def.max)
        j["max"] = *def.max;
    if (def.defaultValue)
        j["default"] = *def.defaultValue;
    if (!def.enumValues.empty())
        j["values"] = def.enumValues;
    return j;
}
} // namespace

std::string normalize(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (const char c : text)
        if (std::isalnum(static_cast<unsigned char>(c)))
            out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return out;
}

TargetResolver::TargetResolver(const serum2::Schema& schema)
    : schema_(schema)
{
    if (const auto text = serum2::embedded::find("serum2_schema.json"))
        rawModDests_ = Json::parse(*text).value("modDests", Json::object());
}

std::optional<std::string> TargetResolver::fxType(std::string_view name) const
{
    const auto n = normalize(name);
    if (const auto it = fxTypeAliases().find(n); it != fxTypeAliases().end())
        return it->second;
    for (const auto& type : schema_.fxTypes())
        if (normalize(type) == n || normalize(type) == "fx" + n)
            return type;
    return std::nullopt;
}

std::string TargetResolver::fxFriendly(std::string_view fxType)
{
    static const std::map<std::string, std::string, std::less<>> names {
        { "FXDistortion", "distortion" }, { "FXChorus", "chorus" }, { "FXFlanger", "flanger" },
        { "FXPhaser", "phaser" },         { "FXDelay", "delay" },   { "FXReverb", "reverb" },
        { "FXComp", "compressor" },       { "FXEQ", "eq" },         { "FXFilter", "filter" },
        { "FXBode", "bode" },             { "FXHyperD", "hyper" },  { "FXConv", "convolve" },
        { "FXUtils", "utility" },         { "FXSplit", "split" },   { "FXSplit3", "split3" },
        { "FXSplitMS", "split_ms" },
    };
    if (const auto it = names.find(fxType); it != names.end())
        return it->second;
    return normalize(fxType);
}

std::optional<TargetResolver::Owner> TargetResolver::owner(std::string_view name) const
{
    const auto n = normalize(name);
    // Oscillators: oscA/oscB/oscC, osc1-3, "a"/"b"/"c", noise, sub.
    if (n == "noise" || n == "noiseosc")
        return Owner { "noise", { Module::osc(3), Module::noise() } };
    if (n == "sub" || n == "subosc")
        return Owner { "sub", { Module::osc(4), Module::sub() } };
    if (n.rfind("osc", 0) == 0 || n.rfind("oscillator", 0) == 0 || n.size() == 1)
    {
        std::string rest = n.rfind("oscillator", 0) == 0 ? n.substr(10) : n.rfind("osc", 0) == 0 ? n.substr(3) : n;
        int slot = -1;
        if (rest == "a" || rest == "1")
            slot = 0;
        else if (rest == "b" || rest == "2")
            slot = 1;
        else if (rest == "c" || rest == "3")
            slot = 2;
        if (slot >= 0)
            return Owner { oscLetter(slot), { Module::osc(slot), Module::wavetable(slot), Module::multisample(slot) } };
        return std::nullopt;
    }

    const auto [base, slot] = splitSlot(n);
    const int one = slot == 0 ? 1 : slot; // "filter" means filter1
    if ((base == "filter" || base == "voicefilter" || base == "flt") && one >= 1 && one <= serum2::kNumFilters)
        return Owner { "filter" + std::to_string(one), { Module::filter(one - 1) } };
    if ((base == "env" || base == "envelope") && one >= 1 && one <= serum2::kNumEnvelopes)
        return Owner { "env" + std::to_string(one), { Module::env(one - 1) } };
    if ((base == "amp" || base == "ampenv") && slot == 0)
        return Owner { "env1", { Module::env(0) } };
    if (base == "lfo" && one >= 1 && one <= serum2::kNumLfos)
        return Owner { "lfo" + std::to_string(one), { Module::lfo(one - 1) } };
    if (base == "macro" && one >= 1 && one <= serum2::kNumMacros)
        return Owner { "macro" + std::to_string(one), { Module::macro(one - 1) } };
    if (n == "global" || n == "master" || n == "voicing")
        return Owner { "global", { Module::global() } };
    return std::nullopt;
}

std::optional<Target> TargetResolver::findParam(const Owner& o, std::string_view param) const
{
    const auto p = keyNorm(param);
    // Aliases first, in the owner's module order, then raw kParam names.
    for (int pass = 0; pass < 2; ++pass)
        for (const auto& m : o.modules)
        {
            const auto* table = schema_.module(m.schemaModule);
            if (!table)
                continue;
            std::string key;
            if (pass == 0)
            {
                if (const auto it = aliases().find(m.schemaModule); it != aliases().end())
                    for (const auto& [alias, k] : it->second)
                        if (normalize(alias) == p && table->count(k))
                        {
                            key = k;
                            break;
                        }
            }
            else
            {
                for (const auto& [k, def] : *table)
                    if (keyNorm(k) == p)
                    {
                        key = k;
                        break;
                    }
            }
            if (key.empty())
                continue;
            Target t;
            t.kind = Target::Kind::Module;
            t.module = m;
            t.key = key;
            t.def = schema_.param(m.schemaModule, key);
            t.owner = o.label;
            t.label = o.label + "." + paramLabel(m.schemaModule, key);
            return t;
        }
    return std::nullopt;
}

std::optional<Target> TargetResolver::findFxParam(const std::string& fxType, int instance, std::string_view param) const
{
    const auto* table = schema_.fxParams(fxType);
    if (!table)
        return std::nullopt;
    const auto p = keyNorm(param);
    std::string key;
    for (const auto& [alias, k] : fxAliases())
        if (normalize(alias) == p && table->count(k))
        {
            key = k;
            break;
        }
    if (key.empty())
        for (const auto& [k, def] : *table)
            if (keyNorm(k) == p)
            {
                key = k;
                break;
            }
    if (key.empty())
        return std::nullopt;
    Target t;
    t.kind = Target::Kind::Fx;
    t.fxType = fxType;
    t.fxInstance = instance;
    t.key = key;
    t.def = schema_.param(fxType, key);
    t.owner = "fx." + fxFriendly(fxType) + (instance > 0 ? std::to_string(instance + 1) : "");
    t.label = t.owner + "." + paramLabel(fxType, key);
    return t;
}

Target TargetResolver::resolve(std::string_view name) const
{
    const std::string text(name);
    const auto dot = text.rfind('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 == text.size())
        throw std::invalid_argument("\"" + text + "\" isn't a parameter. Use owner.param, e.g. filter1.cutoff, "
                                    "oscA.level, env1.release, lfo1.rate, fx.reverb.mix.");
    const auto ownerName = text.substr(0, dot);
    const auto param = text.substr(dot + 1);

    // FX: "fx.reverb.mix", "fx.reverb2.mix", "reverb.mix".
    std::string fxName = ownerName;
    if (normalize(ownerName.substr(0, 3)) == "fx" && ownerName.size() > 3)
        fxName = ownerName.substr(3);
    const auto [fxBase, fxSlot] = splitSlot(normalize(fxName));
    const bool explicitFx = normalize(ownerName.substr(0, 2)) == "fx";
    // "filter3" is a typo for a voice filter, not the third FX filter: FX
    // names that clash with a synth section need the "fx." prefix.
    if (explicitFx || (!owner(ownerName) && fxBase != "filter"))
        if (const auto type = fxType(fxBase))
        {
            const int instance = fxSlot > 0 ? fxSlot - 1 : 0;
            if (auto t = findFxParam(*type, instance, param))
                return *t;
            std::vector<std::string> names;
            if (const auto* table = schema_.fxParams(*type))
                for (const auto& [k, def] : *table)
                    names.push_back(paramLabel(*type, k));
            throw std::invalid_argument("fx." + fxFriendly(*type) + " has no \"" + param + "\". Try: " + join(names) + ".");
        }

    const auto o = owner(ownerName);
    if (!o)
        throw std::invalid_argument("Unknown section \"" + ownerName + "\". Use oscA/oscB/oscC, noise, sub, "
                                    "filter1/filter2, env1-env4, lfo1-lfo10, macro1-macro8, global or fx.<type>.");
    if (auto t = findParam(*o, param))
        return *t;

    std::vector<std::string> names;
    for (const auto& m : o->modules)
        if (const auto* table = schema_.module(m.schemaModule))
            for (const auto& [k, def] : *table)
                names.push_back(paramLabel(m.schemaModule, k));
    throw std::invalid_argument(o->label + " has no \"" + param + "\". Try: " + join(names) + ".");
}

std::string TargetResolver::paramLabel(std::string_view schemaModule, std::string_view key) const
{
    if (const auto it = aliases().find(schemaModule); it != aliases().end())
        for (const auto& [alias, k] : it->second)
            if (k == key)
                return alias;
    if (schema_.fxParams(schemaModule))
        for (const auto& [alias, k] : fxAliases())
            if (k == key && schema_.param(schemaModule, k))
                return alias;
    std::string label = stripPrefix(key);
    // "TimeL" -> "timel": lowercase keeps it unambiguous and round-trips.
    std::transform(label.begin(), label.end(), label.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return label;
}

std::string TargetResolver::ownerLabel(const std::string& moduleType, int id) const
{
    if (moduleType == "Oscillator" || moduleType == "WTOsc" || moduleType == "MultiSampleOsc" || moduleType == "NoiseOsc"
        || moduleType == "SubOsc")
        return oscLetter(id);
    if (moduleType == "VoiceFilter")
        return "filter" + std::to_string(id + 1);
    if (moduleType == "Env")
        return "env" + std::to_string(id + 1);
    if (moduleType == "LFO")
        return "lfo" + std::to_string(id + 1);
    if (moduleType == "Macro")
        return "macro" + std::to_string(id + 1);
    if (moduleType == "Global")
        return "global";
    if (schema_.fxParams(moduleType))
        return "fx." + fxFriendly(moduleType);
    return normalize(moduleType) + std::to_string(id);
}

std::optional<int> TargetResolver::modSource(std::string_view name) const
{
    const auto n = normalize(name);
    const auto [base, slot] = splitSlot(n);
    std::string key;
    if ((base == "lfo" || base == "env" || base == "macro") && slot >= 1)
        key = base + std::to_string(slot - 1);
    else if (const auto it = sourceAliases().find(n); it != sourceAliases().end())
        key = it->second;
    else
        key = std::string(name);
    return schema_.modSource(key);
}

std::string TargetResolver::modSourceLabel(int id) const
{
    const auto name = schema_.modSourceName(id);
    if (!name)
        return "source " + std::to_string(id);
    // Schema sources are 0-based ("lfo0"); the UI and our names are 1-based.
    const auto [base, slot] = splitSlot(*name);
    if ((base == "lfo" || base == "env" || base == "macro") && *name != base)
        return base + std::to_string(slot + 1);
    // The longest alias is the most readable one ("pitchbend" over "bend").
    std::string best;
    for (const auto& [alias, key] : sourceAliases())
        if (key == *name && alias.size() > best.size())
            best = alias;
    return best.empty() ? *name : best;
}

std::optional<serum2::ModDest> TargetResolver::modDest(const Target& t, int fxRack, int fxPosition) const
{
    if (t.kind == Target::Kind::Fx)
    {
        if (t.key == "kParamWet")
            return schema_.fxModDest(t.fxType, fxRack, fxPosition, "wet");
        // Extra FX destinations are keyed by short names ("width", "hpf").
        for (const auto& shortName : { std::string("width"), std::string("balance"), std::string("level_out"),
                                       std::string("hpf"), std::string("lpf"), std::string("lf_xover"), std::string("freq2") })
            if (auto d = schema_.fxModDest(t.fxType, fxRack, fxPosition, shortName); d && d->paramName == t.key)
                return d;
        return std::nullopt;
    }
    for (const auto& [name, d] : rawModDests_.items())
        if (d.value("dest_type", "") == t.module.schemaModule && d.value("dest_id", -1) == t.module.slot
            && d.value("param_name", "") == t.key)
            return serum2::ModDest { d["dest_type"].get<std::string>(), d["dest_id"].get<int>(),
                                     d["param_name"].get<std::string>(), d["param_id"].get<int>() };
    return std::nullopt;
}

Json TargetResolver::reference() const
{
    Json out = Json::object();
    const auto addOwner = [&](const Owner& o, const std::string& note) {
        Json params = Json::object();
        for (const auto& m : o.modules)
            if (const auto* table = schema_.module(m.schemaModule))
                for (const auto& [key, def] : *table)
                {
                    auto info = paramInfo(def);
                    if (auto d = def.defaultFor(m.slot))
                        info["default"] = *d;
                    Target t;
                    t.module = m;
                    t.key = key;
                    info["modulatable"] = modDest(t).has_value();
                    params[paramLabel(m.schemaModule, key)] = std::move(info);
                }
        Json entry = { { "params", std::move(params) } };
        if (!note.empty())
            entry["note"] = note;
        out[o.label] = std::move(entry);
    };
    addOwner(*owner("oscA"), "Also oscB, oscC. Wavetable params (wt_pos, warp) apply in wavetable mode.");
    addOwner(*owner("noise"), "");
    addOwner(*owner("sub"), "");
    addOwner(*owner("filter1"), "Also filter2. Cutoff is normalized 0-1 (about 0.5 = mid-range).");
    addOwner(*owner("env1"), "env1 is the amp envelope. Also env2-env4 (mod envelopes).");
    addOwner(*owner("lfo1"), "Also lfo2-lfo10. Set rate to a note value (\"1/8\") for synced, or lfoN.hz for free-running Hz.");
    addOwner(*owner("macro1"), "Also macro2-macro8. Rename with macroN.name.");
    addOwner(*owner("global"), "");
    for (const auto& type : schema_.fxTypes())
    {
        if (type.rfind("FXSplit", 0) == 0)
            continue;
        Json params = Json::object();
        if (const auto* table = schema_.fxParams(type))
            for (const auto& [key, def] : *table)
                params[paramLabel(type, key)] = paramInfo(def);
        out["fx." + fxFriendly(type)] = { { "params", std::move(params) } };
    }
    return out;
}
} // namespace tts
