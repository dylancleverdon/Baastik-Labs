#include "Program.h"

namespace norg::perform
{
    const juce::Identifier Program::type { "Program" };
    const juce::Identifier Program::nameId { "name" };

    juce::String partName (Part part)
    {
        switch (part)
        {
            case Part::organ:   return "Organ";
            case Part::piano:   return "Piano";
            case Part::synth:   return "Synth";
            case Part::sample:  return "Sample";
            case Part::effects: return "Effects";
        }
        return {};
    }

    Part partOf (P p)
    {
        const juce::String key (spec (p).key);
        if (key.startsWith ("organ_"))  return Part::organ;
        if (key.startsWith ("piano_"))  return Part::piano;
        if (key.startsWith ("synth_"))  return Part::synth;
        if (key.startsWith ("sample_")) return Part::sample;
        return Part::effects;
    }

    Program::Program() : tree (type) {}

    Program::Program (const juce::ValueTree& stored)
        : tree (stored.hasType (type) ? stored.createCopy() : juce::ValueTree (type))
    {
    }

    bool Program::isInProgram (P p)
    {
        // Global settings stay with the instrument, not the sound.
        return p != P::masterVolume && p != P::clockHostSync;
    }

    float Program::get (P p, int panel) const
    {
        const auto& s = spec (p);
        const auto* value = tree.getPropertyPointer (juce::Identifier (paramId (p, panel)));
        return value != nullptr ? static_cast<float> (*value) : s.defaultValue;
    }

    void Program::set (P p, int panel, float plainValue)
    {
        const auto& s = spec (p);
        const juce::Identifier id (paramId (p, panel));
        const float v = juce::jlimit (s.minValue, s.maxValue, plainValue);

        if (std::abs (v - s.defaultValue) <= 1.0e-6f)
            tree.removeProperty (id, nullptr);
        else
            tree.setProperty (id, v, nullptr);
    }

    const std::initializer_list<const char*>& Program::libraryKeys()
    {
        static const std::initializer_list<const char*> keys { "lib_grand", "lib_upright", "lib_sample",
                                                               "b_lib_grand", "b_lib_upright", "b_lib_sample" };
        return keys;
    }

    juce::String Program::getLibrary (const juce::Identifier& key) const
    {
        return tree.getProperty (key).toString();
    }

    void Program::setLibrary (const juce::Identifier& key, const juce::String& choice)
    {
        if (choice.isEmpty())
            tree.removeProperty (key, nullptr);
        else
            tree.setProperty (key, choice, nullptr);
    }

    void Program::copyPart (Part part, const Program& from, int fromPanel, int toPanel)
    {
        for (int i = 0; i < numParams; ++i)
        {
            const auto p = static_cast<P> (i);
            if (spec (p).scope == ParamScope::Panel && isInProgram (p) && partOf (p) == part)
                set (p, toPanel, from.get (p, fromPanel));
        }

        if (part == Part::piano || part == Part::sample)
        {
            const juce::String fromPrefix = fromPanel == 1 ? "b_" : "", toPrefix = toPanel == 1 ? "b_" : "";
            const auto keys = part == Part::piano ? juce::StringArray { "lib_grand", "lib_upright" } : juce::StringArray { "lib_sample" };
            for (const auto& key : keys)
                setLibrary (juce::Identifier (toPrefix + key), from.getLibrary (juce::Identifier (fromPrefix + key)));
        }
    }

    bool Program::sameSoundAs (const Program& other) const
    {
        auto a = tree.createCopy(), b = other.tree.createCopy();
        a.removeProperty (nameId, nullptr);
        b.removeProperty (nameId, nullptr);
        return a.isEquivalentTo (b);
    }

    Program capture (juce::AudioProcessorValueTreeState& state)
    {
        Program program;
        for (int i = 0; i < numParams; ++i)
        {
            const auto p = static_cast<P> (i);
            if (! Program::isInProgram (p))
                continue;

            const int panels = spec (p).scope == ParamScope::Panel ? numPanels : 1;
            for (int panel = 0; panel < panels; ++panel)
                if (auto* param = state.getParameter (paramId (p, panel)))
                    program.set (p, panel, param->convertFrom0to1 (param->getValue()));
        }

        for (const auto* key : Program::libraryKeys())
            program.setLibrary (key, state.state.getProperty (key).toString());
        return program;
    }

    void apply (const Program& program, juce::AudioProcessorValueTreeState& state)
    {
        for (int i = 0; i < numParams; ++i)
        {
            const auto p = static_cast<P> (i);
            if (! Program::isInProgram (p))
                continue;

            const int panels = spec (p).scope == ParamScope::Panel ? numPanels : 1;
            for (int panel = 0; panel < panels; ++panel)
                if (auto* param = state.getParameter (paramId (p, panel)))
                {
                    const float normalised = param->convertTo0to1 (program.get (p, panel));
                    if (std::abs (param->getValue() - normalised) > 1.0e-7f)
                        param->setValueNotifyingHost (normalised);
                }
        }

        for (const auto* key : Program::libraryKeys())
            state.state.setProperty (key, program.getLibrary (key), nullptr);
    }
}
