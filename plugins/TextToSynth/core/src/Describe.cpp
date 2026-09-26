#include "tts/Describe.h"

#include <cmath>
#include <map>
#include <sstream>

#include "serum2/Patch.h"
#include "tts/Edits.h"

namespace tts
{
using serum2::Module;

namespace
{
class Describer
{
public:
    Describer(const serum2::Preset& preset, const TargetResolver& resolver)
        : preset_(preset), data_(preset.data), resolver_(resolver), ed_(data_, resolver.schema())
    {
    }

    std::string run()
    {
        header();
        voicing();
        oscillators();
        filters();
        envelopes();
        lfos();
        routes();
        macros();
        fx();
        return out_.str();
    }

private:
    std::string num(double v) const { return formatValue(v); }

    void line(const std::string& text) { out_ << text << "\n"; }

    std::string join(const std::vector<std::string>& parts) const
    {
        std::string s;
        for (const auto& p : parts)
            s += (s.empty() ? "" : " · ") + p;
        return s;
    }

    void header()
    {
        line("Name: " + preset_.metadata.value("presetName", std::string("(unnamed)")));
        const auto author = preset_.metadata.value("presetAuthor", std::string());
        if (!author.empty())
            line("Author: " + author);
        const auto desc = preset_.metadata.value("presetDescription", std::string());
        if (!desc.empty())
            line("Description: " + desc);
    }

    void voicing()
    {
        const auto g = Module::global();
        std::vector<std::string> parts;
        if (ed_.flag(g, "kParamMonoToggle"))
        {
            parts.push_back("mono");
            if (ed_.flag(g, "kParamLegato"))
                parts.push_back("legato");
        }
        else
            parts.push_back("poly " + num(ed_.number(g, "kParamPolyCount")) + " voices");
        const double glide = ed_.number(g, "kParamPortamentoTime");
        if (glide > 0)
            parts.push_back("glide " + num(glide) + "s" + (ed_.flag(g, "kParamPortaAlways") ? " (always)" : ""));
        parts.push_back("volume " + num(ed_.number(g, "kParamMasterVolume")));
        const double transpose = ed_.number(g, "kParamTranspose");
        if (transpose != 0)
            parts.push_back("transpose " + num(transpose));
        line("global: " + join(parts));
    }

    std::string engineOf(int osc)
    {
        const auto type = ed_.text(Module::osc(osc), "kParamType");
        if (type.empty() || type == "kOsc_WT")
        {
            const auto* c = ed_.find(Module::wavetable(osc));
            std::string table = "embedded table";
            if (c && c->contains("relativePathToWT"))
                table = (*c)["relativePathToWT"].get<std::string>();
            else if (c && c->contains("tableDisplayName") && (*c)["tableDisplayName"].is_string())
                table = (*c)["tableDisplayName"].get<std::string>() + " (embedded)";
            const auto wt = Module::wavetable(osc);
            std::string s = "wavetable \"" + table + "\" · wt_pos " + num(ed_.number(wt, "kParamTablePos"));
            const double warp = ed_.number(wt, "kParamWarp");
            if (warp > 0)
                s += " · warp " + ed_.text(wt, "kParamWarpMenu") + " " + num(warp);
            const double warp2 = ed_.number(wt, "kParamWarp2");
            if (warp2 > 0)
                s += " · warp2 " + ed_.text(wt, "kParamWarpMenu2") + " " + num(warp2);
            return s;
        }
        if (type == "kOsc_MultiSample")
        {
            const auto* c = ed_.find(Module::multisample(osc));
            std::string name = "multisample";
            if (c && c->contains("sfzPathRelative") && (*c)["sfzPathRelative"].is_string())
                name = "multisample \"" + (*c)["sfzPathRelative"].get<std::string>() + "\"";
            return name;
        }
        if (type == "kOsc_Sample")
            return "sample";
        if (type == "kOsc_Granular")
            return "granular";
        if (type == "kOsc_Spectral")
            return "spectral";
        return type;
    }

    void oscillators()
    {
        line("");
        line("Oscillators:");
        for (int i = 0; i < 5; ++i)
        {
            const auto m = Module::osc(i);
            const auto label = resolver_.ownerLabel("Oscillator", i);
            if (!ed_.flag(m, "kParamEnable"))
            {
                line("  " + label + ": off");
                continue;
            }
            std::vector<std::string> parts { "on" };
            if (i < 3)
                parts.push_back(engineOf(i));
            else if (i == 3)
            {
                const auto* c = ed_.find(Module::osc(3));
                std::string sample;
                if (c && c->contains("NoiseOsc3") && (*c)["NoiseOsc3"].contains("relativePathToNoiseSample"))
                    sample = (*c)["NoiseOsc3"]["relativePathToNoiseSample"].get<std::string>();
                parts.push_back(sample.empty() ? "type " + ed_.text(Module::noise(), "kParamNoiseType") : "sample \"" + sample + "\"");
                parts.push_back("color " + num(ed_.number(Module::noise(), "kParamColor")));
            }
            else
            {
                const auto shape = ed_.text(Module::sub(), "kParamShape");
                parts.push_back("shape " + (shape.empty() ? std::string("sine") : shape));
            }
            parts.push_back("level " + num(ed_.number(m, "kParamVolume")));
            const double oct = ed_.number(m, "kParamOctave");
            const double semi = ed_.number(m, "kParamCoarsePit");
            const double fine = ed_.number(m, "kParamFine");
            if (oct != 0)
                parts.push_back("octave " + num(oct));
            if (semi != 0)
                parts.push_back("semi " + num(semi));
            if (fine != 0)
                parts.push_back("fine " + num(fine));
            const double unison = ed_.number(m, "kParamUnison");
            if (unison > 1)
                parts.push_back("unison " + num(unison) + " (detune " + num(ed_.number(m, "kParamDetune")) + ", stereo "
                                + num(ed_.number(m, "kParamUnisonStereo")) + ")");
            const double pan = ed_.number(m, "kParamPan");
            if (pan != 0)
                parts.push_back("pan " + num(pan));
            line("  " + label + ": " + join(parts));
        }
    }

    void filters()
    {
        line("");
        line("Filters:");
        for (int i = 0; i < serum2::kNumFilters; ++i)
        {
            const auto m = Module::filter(i);
            const auto label = "filter" + std::to_string(i + 1);
            if (!ed_.flag(m, "kParamEnable"))
            {
                line("  " + label + ": off");
                continue;
            }
            std::vector<std::string> parts { "on", ed_.text(m, "kParamType"), "cutoff " + num(ed_.number(m, "kParamFreq")),
                                             "resonance " + num(ed_.number(m, "kParamReso")) };
            if (const double d = ed_.number(m, "kParamDrive"); d > 0)
                parts.push_back("drive " + num(d));
            if (const double w = ed_.number(m, "kParamWet"); w < 100)
                parts.push_back("mix " + num(w));
            if (ed_.flag(m, "kParamKeyTrack"))
                parts.push_back("keytrack");
            line("  " + label + ": " + join(parts));
        }
    }

    bool sourceUsed(const std::string& name)
    {
        const auto source = resolver_.modSource(name);
        for (const int slot : ed_.usedModSlots())
            if (const auto r = ed_.modRoute(slot); r && source && (r->source == *source || r->aux == *source))
                return true;
        return false;
    }

    void envelopes()
    {
        line("");
        line("Envelopes:");
        for (int i = 0; i < serum2::kNumEnvelopes; ++i)
        {
            const auto label = "env" + std::to_string(i + 1);
            if (i > 0 && !sourceUsed(label))
                continue;
            const auto m = Module::env(i);
            line("  " + label + (i == 0 ? " (amp)" : "") + ": attack " + num(ed_.number(m, "kParamAttack")) + "s · hold "
                 + num(ed_.number(m, "kParamHold")) + "s · decay " + num(ed_.number(m, "kParamDecay")) + "s · sustain "
                 + num(ed_.number(m, "kParamSustain")) + " · release " + num(ed_.number(m, "kParamRelease")) + "s");
        }
    }

    std::string lfoShape(int l)
    {
        const auto* c = ed_.find(Module::lfo(l));
        const auto type = ed_.text(Module::lfo(l), "kParamType");
        if (!type.empty() && type != "Path")
            return type == "RandomSH" ? "random" : type;
        if (c && c->contains("curveDisplayName") && (*c)["curveDisplayName"].is_string())
            return (*c)["curveDisplayName"].get<std::string>() == "Custom" ? "custom curve"
                                                                          : (*c)["curveDisplayName"].get<std::string>();
        if (!c || !c->contains("curveData") || (*c)["curveData"].empty())
            return "triangle";
        return "custom curve";
    }

    std::string lfoRate(int l)
    {
        const auto m = Module::lfo(l);
        const double rate = ed_.number(m, "kParamRate");
        if (!ed_.flag(m, "kParamBeatSync"))
            return num(rate) + " Hz (free)";
        std::string best;
        double bestDist = 1e9;
        for (const auto& [name, value] : resolver_.schema().lfoSyncRates())
            if (std::abs(value - rate) < bestDist)
            {
                bestDist = std::abs(value - rate);
                best = name;
            }
        return (bestDist < 0.05 ? best : "≈" + best + " (raw " + num(rate) + ")") + " synced";
    }

    void lfos()
    {
        std::vector<std::string> lines;
        for (int l = 0; l < serum2::kNumLfos; ++l)
        {
            const auto label = "lfo" + std::to_string(l + 1);
            if (!sourceUsed(label))
                continue;
            const auto m = Module::lfo(l);
            std::vector<std::string> parts { lfoShape(l), lfoRate(l) };
            const auto mode = ed_.text(m, "kParamMode");
            if (!mode.empty())
                parts.push_back(mode);
            if (const double rise = ed_.number(m, "kParamRise"); rise > 0)
                parts.push_back("rise " + num(rise) + "s");
            if (const double delay = ed_.number(m, "kParamDelay"); delay > 0)
                parts.push_back("delay " + num(delay) + "s");
            lines.push_back("  " + label + ": " + join(parts));
        }
        if (lines.empty())
            return;
        line("");
        line("LFOs (routed):");
        for (const auto& l : lines)
            line(l);
    }

    // "fx.reverb" style owner for the unit at rack/position, matching edit targets.
    std::string fxOwnerAt(int rack, int position)
    {
        serum2::Preset copy = preset_;
        const PresetEditor reader(copy, resolver_);
        std::map<std::string, int> seen;
        for (const auto& [at, type] : reader.fxUnits())
        {
            const int instance = seen[type]++;
            if (at.rack == rack && at.position == position)
                return "fx." + TargetResolver::fxFriendly(type) + (instance > 0 ? std::to_string(instance + 1) : "");
        }
        return "fx(missing unit)";
    }

    std::string destLabel(const serum2::ModDest& d)
    {
        if (resolver_.schema().fxParams(d.type))
            return fxOwnerAt(d.id / 100, d.id % 100) + "." + resolver_.paramLabel(d.type, d.paramName);
        return resolver_.ownerLabel(d.type, d.id) + "." + resolver_.paramLabel(d.type, d.paramName);
    }

    void routes()
    {
        const auto slots = ed_.usedModSlots();
        if (slots.empty())
            return;
        line("");
        line("Mod routes:");
        for (const int slot : slots)
        {
            const auto r = ed_.modRoute(slot);
            if (!r)
                continue;
            std::string text = "  " + resolver_.modSourceLabel(r->source) + " → " + destLabel(r->dest) + " " + num(r->amount) + "%";
            if (r->bipolar)
                text += " bipolar";
            if (r->aux != 0)
                text += " (scaled by " + resolver_.modSourceLabel(r->aux) + ")";
            line(text);
        }
    }

    void macros()
    {
        std::vector<std::string> lines;
        for (int i = 0; i < serum2::kNumMacros; ++i)
        {
            const auto label = "macro" + std::to_string(i + 1);
            const auto name = ed_.macroName(i);
            const double value = ed_.number(Module::macro(i), "kParamValue");
            if (name.empty() && !sourceUsed(label) && value == 0)
                continue;
            lines.push_back("  " + label + (name.empty() ? "" : " \"" + name + "\"") + " = " + num(value));
        }
        if (lines.empty())
            return;
        line("");
        line("Macros:");
        for (const auto& l : lines)
            line(l);
    }

    void fx()
    {
        serum2::Preset copy = preset_;
        const PresetEditor reader(copy, resolver_);
        const auto units = reader.fxUnits();
        line("");
        if (units.empty())
        {
            line("FX: none");
            return;
        }
        int lastRack = -1;
        std::map<std::string, int> seen;
        for (const auto& [at, type] : units)
        {
            if (at.rack != lastRack)
            {
                line(at.rack == 0 ? "FX (main rack, in order):" : "FX bus " + std::to_string(at.rack) + ":");
                lastRack = at.rack;
            }
            const int instance = seen[type]++;
            const auto owner = "fx." + TargetResolver::fxFriendly(type) + (instance > 0 ? std::to_string(instance + 1) : "");
            std::vector<std::string> parts;
            const auto& entry = copy.data["FXRack" + std::to_string(at.rack)]["FX"][static_cast<std::size_t>(at.position)];
            bool hasWet = false;
            if (entry.contains(type) && entry[type].contains("plainParams") && entry[type]["plainParams"].is_object())
                for (const auto& [k, v] : entry[type]["plainParams"].items())
                {
                    if (!resolver_.schema().param(type, k))
                        continue; // internal values we don't expose
                    hasWet = hasWet || k == "kParamWet";
                    parts.push_back(resolver_.paramLabel(type, k) + " " + formatValue(v));
                }
            if (!hasWet && resolver_.schema().param(type, "kParamWet"))
                parts.push_back("mix 100");
            line("  " + std::to_string(at.position + 1) + ". " + owner + (parts.empty() ? " (defaults)" : ": " + join(parts)));
        }
    }

    const serum2::Preset& preset_;
    serum2::Json data_;
    const TargetResolver& resolver_;
    serum2::PatchEditor ed_;
    std::ostringstream out_;
};
} // namespace

std::string describePreset(const serum2::Preset& preset, const TargetResolver& resolver)
{
    return Describer(preset, resolver).run();
}
} // namespace tts
