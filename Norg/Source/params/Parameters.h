#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <cmath>

namespace norg
{
    // One enum entry per parameter, generated from ParamList.def.
    enum class P : int
    {
       #define NORG_PARAM(enumName, ...) enumName,
       #include "ParamList.def"
       #undef NORG_PARAM
        count
    };

    inline constexpr int numParams = static_cast<int> (P::count);
    inline constexpr int numPanels = 2; // Panel A and Panel B (Stage mode)

    enum class ParamKind { Float, Bool, Choice, Int };
    enum class ParamScope { Global, Panel };

    struct ParamSpec
    {
        P id;
        const char* key;
        const char* name;
        ParamKind kind;
        float minValue, maxValue, defaultValue, centre;
        const char* choices;
        const char* unit;
        int versionHint;
        ParamScope scope;
    };

    const ParamSpec& spec (P);

    // The host-facing parameter ID. Panel B copies of panel-scoped parameters are prefixed "b_".
    juce::String paramId (P, int panel = 0);

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Plain values of every parameter, captured once per audio block. Engines render from a
    // snapshot rather than the live parameters, so an engine that is fading out after a program
    // change can keep its old sound.
    class ParamSnapshot
    {
    public:
        static constexpr int numSlots = numParams * numPanels;

        static int slot (P p, int panel)
        {
            return spec (p).scope == ParamScope::Global ? static_cast<int> (p)
                                                        : static_cast<int> (p) + numParams * panel;
        }

        float get (P p, int panel = 0) const       { return values[static_cast<size_t> (slot (p, panel))]; }
        bool getBool (P p, int panel = 0) const    { return get (p, panel) >= 0.5f; }
        int getInt (P p, int panel = 0) const      { return static_cast<int> (std::lround (get (p, panel))); }
        void set (P p, int panel, float v)         { values[static_cast<size_t> (slot (p, panel))] = v; }

        void resetToDefaults();

        std::array<float, numSlots> values {};
    };

    // Binds snapshots to the live APVTS parameters.
    class ParamTable
    {
    public:
        explicit ParamTable (juce::AudioProcessorValueTreeState&);

        void capture (ParamSnapshot&) const;
        juce::RangedAudioParameter* parameter (P, int panel = 0) const;

    private:
        std::array<std::atomic<float>*, ParamSnapshot::numSlots> raw {};
        std::array<juce::RangedAudioParameter*, ParamSnapshot::numSlots> params {};
    };
}
