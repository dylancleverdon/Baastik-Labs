#include "SectionPanels.h"
#include "OrganPanel.h"
#include "PianoPanel.h"
#include "PluginProcessor.h"
#include "params/Parameters.h"

namespace norg::ui
{
    InstrumentSection::InstrumentSection (juce::AudioProcessorValueTreeState& state, juce::String title,
                                          const juce::String& onParamId, const juce::String& volumeParamId)
        : SectionFrame (std::move (title)),
          onButton (state, onParamId, "On"),
          volume (state, volumeParamId, "Volume")
    {
        addAndMakeVisible (onButton);
        addAndMakeVisible (volume);
    }

    void InstrumentSection::resized()
    {
        auto area = content();
        auto column = area.removeFromLeft (76);
        onButton.setBounds (column.removeFromTop (50).withSizeKeepingCentre (56, 50));
        column.removeFromTop (8);
        volume.setBounds (column.removeFromTop (92));
    }

    juce::Rectangle<int> InstrumentSection::controlsArea() const
    {
        return content().withTrimmedLeft (86);
    }

    //==============================================================================
    namespace
    {
        // Top row of sections, then the rotary + effects strip underneath.
        void layoutPanel (juce::Rectangle<int> area, std::initializer_list<std::pair<juce::Component*, int>> topRow,
                          juce::Component& rotary, juce::Component& effects)
        {
            auto bottom = area.removeFromBottom (136);
            area.removeFromBottom (12);

            const int gap = 14;
            for (const auto& [component, width] : topRow)
            {
                component->setBounds (area.removeFromLeft (width));
                area.removeFromLeft (gap);
            }

            rotary.setBounds (bottom.removeFromLeft (330));
            bottom.removeFromLeft (gap);
            effects.setBounds (bottom);
        }
    }

    StagePanel::StagePanel (NorgProcessor& p)
        : organ (std::make_unique<OrganPanel> (p.state(), DrawbarStyle::sliding)),
          piano (std::make_unique<PianoPanel> (p)),
          synth (p.state(), "Synth", paramId (P::synthOn), paramId (P::synthVolume)),
          rotary (std::make_unique<RotaryPanel> (p.state()))
    {
        synth.setNote ("Synth coming soon");
        effects.setNote ("Effects and reverb coming soon");

        for (auto* c : std::initializer_list<juce::Component*> { organ.get(), piano.get(), &synth, rotary.get(), &effects })
            addAndMakeVisible (c);
    }

    StagePanel::~StagePanel() = default;

    void StagePanel::resized()
    {
        const int width = getWidth() - 28;
        const int organWidth = width * 48 / 100;
        const int pianoWidth = (width - organWidth) / 2;
        layoutPanel (getLocalBounds(), { { organ.get(), organWidth }, { piano.get(), pianoWidth }, { &synth, width - organWidth - pianoWidth } },
                     *rotary, effects);
    }

    //==============================================================================
    ElectroPanel::ElectroPanel (NorgProcessor& p)
        : organ (std::make_unique<OrganPanel> (p.state(), DrawbarStyle::leds)),
          piano (std::make_unique<PianoPanel> (p)),
          sample (p.state(), "Sample", paramId (P::sampleOn), paramId (P::sampleVolume)),
          rotary (std::make_unique<RotaryPanel> (p.state()))
    {
        sample.setNote ("Tape strings, flute and choir coming soon");
        effects.setNote ("Effects coming soon");

        for (auto* c : std::initializer_list<juce::Component*> { organ.get(), piano.get(), &sample, rotary.get(), &effects })
            addAndMakeVisible (c);
    }

    ElectroPanel::~ElectroPanel() = default;

    void ElectroPanel::resized()
    {
        const int width = getWidth() - 28;
        const int organWidth = width * 52 / 100;
        const int pianoWidth = (width - organWidth) / 2;
        layoutPanel (getLocalBounds(), { { organ.get(), organWidth }, { piano.get(), pianoWidth }, { &sample, width - organWidth - pianoWidth } },
                     *rotary, effects);
    }
}
