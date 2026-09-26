#include "SectionPanels.h"
#include "FxPanels.h"
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
        // The instrument sections along the top, then both rows of effects underneath.
        void layoutPanel (juce::Rectangle<int> area, std::initializer_list<std::pair<juce::Component*, int>> topRow,
                          juce::Component& effects)
        {
            effects.setBounds (area.removeFromBottom (EffectsRows::height));
            area.removeFromBottom (12);

            const int gap = 14;
            for (const auto& [component, width] : topRow)
            {
                component->setBounds (area.removeFromLeft (width));
                area.removeFromLeft (gap);
            }
        }
    }

    StagePanel::StagePanel (NorgProcessor& p)
        : organ (std::make_unique<OrganPanel> (p.state(), DrawbarStyle::sliding)),
          piano (std::make_unique<PianoPanel> (p)),
          synth (p.state(), "Synth", paramId (P::synthOn), paramId (P::synthVolume)),
          effects (std::make_unique<EffectsRows> (p.state(), false))
    {
        synth.setNote ("Synth coming soon");

        for (auto* c : std::initializer_list<juce::Component*> { organ.get(), piano.get(), &synth, effects.get() })
            addAndMakeVisible (c);
    }

    StagePanel::~StagePanel() = default;

    void StagePanel::resized()
    {
        const int width = getWidth() - 28;
        const int organWidth = width * 48 / 100;
        const int pianoWidth = (width - organWidth) / 2;
        layoutPanel (getLocalBounds(), { { organ.get(), organWidth }, { piano.get(), pianoWidth }, { &synth, width - organWidth - pianoWidth } },
                     *effects);
    }

    //==============================================================================
    ElectroPanel::ElectroPanel (NorgProcessor& p)
        : organ (std::make_unique<OrganPanel> (p.state(), DrawbarStyle::leds)),
          piano (std::make_unique<PianoPanel> (p)),
          sample (p.state(), "Sample", paramId (P::sampleOn), paramId (P::sampleVolume)),
          effects (std::make_unique<EffectsRows> (p.state(), true))
    {
        sample.setNote ("Tape strings, flute and choir coming soon");

        for (auto* c : std::initializer_list<juce::Component*> { organ.get(), piano.get(), &sample, effects.get() })
            addAndMakeVisible (c);
    }

    ElectroPanel::~ElectroPanel() = default;

    void ElectroPanel::resized()
    {
        const int width = getWidth() - 28;
        const int organWidth = width * 52 / 100;
        const int pianoWidth = (width - organWidth) / 2;
        layoutPanel (getLocalBounds(), { { organ.get(), organWidth }, { piano.get(), pianoWidth }, { &sample, width - organWidth - pianoWidth } },
                     *effects);
    }
}
