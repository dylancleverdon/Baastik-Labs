#pragma once

#include "Widgets.h"

namespace norg
{
    class NorgProcessor;
}

namespace norg::ui
{
    // A section frame with its ON button and VOLUME knob; later phases add each engine's controls.
    class InstrumentSection : public SectionFrame
    {
    public:
        InstrumentSection (juce::AudioProcessorValueTreeState&, juce::String title,
                           const juce::String& onParamId, const juce::String& volumeParamId);

        void resized() override;

    protected:
        // The area to the right of the ON/VOLUME column, for the engine's own controls.
        juce::Rectangle<int> controlsArea() const;

        ParamLedButton onButton;
        Knob volume;
    };

    class OrganPanel;
    class RotaryPanel;
    class PianoPanel;

    // Norg Stage: Organ | Piano | Synth, with the rotary and effects strip underneath.
    class StagePanel final : public juce::Component
    {
    public:
        explicit StagePanel (NorgProcessor&);
        ~StagePanel() override;
        void resized() override;

    private:
        std::unique_ptr<OrganPanel> organ;
        std::unique_ptr<PianoPanel> piano;
        InstrumentSection synth;
        std::unique_ptr<RotaryPanel> rotary;
        SectionFrame effects { "Effects" };
    };

    // Norg Electro: Organ | Piano | Sample, with the rotary and effects row along the bottom.
    class ElectroPanel final : public juce::Component
    {
    public:
        explicit ElectroPanel (NorgProcessor&);
        ~ElectroPanel() override;
        void resized() override;

    private:
        std::unique_ptr<OrganPanel> organ;
        std::unique_ptr<PianoPanel> piano;
        InstrumentSection sample;
        std::unique_ptr<RotaryPanel> rotary;
        SectionFrame effects { "Effects" };
    };
}
