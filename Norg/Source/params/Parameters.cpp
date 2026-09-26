#include "Parameters.h"

namespace norg
{
    namespace
    {
        const std::array<ParamSpec, numParams>& specs()
        {
            static const std::array<ParamSpec, numParams> table { {
               #define NORG_PARAM(enumName, key, name, kind, mn, mx, def, centre, choices, unit, version, scope) \
                   ParamSpec { P::enumName, key, name, ParamKind::kind, (float) (mn), (float) (mx), (float) (def), \
                               (float) (centre), choices, unit, version, ParamScope::scope },
               #include "ParamList.def"
               #undef NORG_PARAM
            } };
            return table;
        }

        std::unique_ptr<juce::RangedAudioParameter> makeParameter (const ParamSpec& s, int panel)
        {
            const juce::ParameterID id { paramId (s.id, panel), s.versionHint };
            const auto name = (s.scope == ParamScope::Panel && panel == 1) ? "B " + juce::String (s.name)
                                                                            : juce::String (s.name);
            switch (s.kind)
            {
                case ParamKind::Bool:
                    return std::make_unique<juce::AudioParameterBool> (id, name, s.defaultValue >= 0.5f);

                case ParamKind::Choice:
                    return std::make_unique<juce::AudioParameterChoice> (
                        id, name, juce::StringArray::fromTokens (s.choices, "|", ""),
                        static_cast<int> (s.defaultValue));

                case ParamKind::Int:
                    return std::make_unique<juce::AudioParameterInt> (
                        id, name, static_cast<int> (s.minValue), static_cast<int> (s.maxValue),
                        static_cast<int> (s.defaultValue),
                        juce::AudioParameterIntAttributes().withLabel (s.unit));

                case ParamKind::Float:
                    break;
            }

            juce::NormalisableRange<float> range (s.minValue, s.maxValue);
            if (s.centre > s.minValue && s.centre < s.maxValue)
                range.setSkewForCentre (s.centre);

            return std::make_unique<juce::AudioParameterFloat> (
                id, name, range, s.defaultValue, juce::AudioParameterFloatAttributes().withLabel (s.unit));
        }
    }

    const ParamSpec& spec (P p)
    {
        return specs()[static_cast<size_t> (p)];
    }

    juce::String paramId (P p, int panel)
    {
        const auto& s = spec (p);
        if (s.scope == ParamScope::Panel && panel == 1)
            return "b_" + juce::String (s.key);
        return s.key;
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        for (const auto& s : specs())
            if (s.scope == ParamScope::Global)
                layout.add (makeParameter (s, 0));

        for (int panel = 0; panel < numPanels; ++panel)
        {
            auto group = std::make_unique<juce::AudioProcessorParameterGroup> (
                panel == 0 ? "panelA" : "panelB", panel == 0 ? "Panel A" : "Panel B", " | ");

            for (const auto& s : specs())
                if (s.scope == ParamScope::Panel)
                    group->addChild (makeParameter (s, panel));

            layout.add (std::move (group));
        }

        return layout;
    }

    void ParamSnapshot::resetToDefaults()
    {
        for (const auto& s : specs())
            for (int panel = 0; panel < numPanels; ++panel)
                set (s.id, panel, s.defaultValue);
    }

    ParamTable::ParamTable (juce::AudioProcessorValueTreeState& state)
    {
        for (const auto& s : specs())
        {
            const int panels = s.scope == ParamScope::Panel ? numPanels : 1;
            for (int panel = 0; panel < panels; ++panel)
            {
                const auto slot = static_cast<size_t> (ParamSnapshot::slot (s.id, panel));
                const auto id = paramId (s.id, panel);
                raw[slot] = state.getRawParameterValue (id);
                params[slot] = state.getParameter (id);
                jassert (raw[slot] != nullptr && params[slot] != nullptr);
            }
        }
    }

    void ParamTable::capture (ParamSnapshot& snapshot) const
    {
        for (size_t i = 0; i < raw.size(); ++i)
            if (raw[i] != nullptr)
                snapshot.values[i] = raw[i]->load (std::memory_order_relaxed);
    }

    juce::RangedAudioParameter* ParamTable::parameter (P p, int panel) const
    {
        return params[static_cast<size_t> (ParamSnapshot::slot (p, panel))];
    }
}
