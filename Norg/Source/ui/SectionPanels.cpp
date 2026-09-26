#include "SectionPanels.h"
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
    StagePanel::StagePanel (juce::AudioProcessorValueTreeState& state)
        : organ (state, "Organ", paramId (P::organOn), paramId (P::organVolume)),
          piano (state, "Piano", paramId (P::pianoOn), paramId (P::pianoVolume)),
          synth (state, "Synth", paramId (P::synthOn), paramId (P::synthVolume))
    {
        organ.setNote ("Tonewheel, Vox, Farf and Pipe organs arrive in the next update");
        piano.setNote ("Grand, EPs and Clav coming soon");
        synth.setNote ("Synth coming soon");
        effects.setNote ("Rotary, effects and reverb coming soon");

        for (auto* c : std::initializer_list<juce::Component*> { &organ, &piano, &synth, &effects })
            addAndMakeVisible (c);
    }

    void StagePanel::resized()
    {
        auto area = getLocalBounds();
        auto fx = area.removeFromBottom (136);
        area.removeFromBottom (12);

        const int gap = 14;
        const int total = area.getWidth() - 2 * gap;
        organ.setBounds (area.removeFromLeft (total * 40 / 100));
        area.removeFromLeft (gap);
        piano.setBounds (area.removeFromLeft (total * 30 / 100));
        area.removeFromLeft (gap);
        synth.setBounds (area);
        effects.setBounds (fx);
    }

    //==============================================================================
    ElectroPanel::ElectroPanel (juce::AudioProcessorValueTreeState& state)
        : organ (state, "Organ", paramId (P::organOn), paramId (P::organVolume)),
          piano (state, "Piano", paramId (P::pianoOn), paramId (P::pianoVolume)),
          sample (state, "Sample", paramId (P::sampleOn), paramId (P::sampleVolume))
    {
        organ.setNote ("Drawbars and rotary arrive in the next update");
        piano.setNote ("Pianos coming soon");
        sample.setNote ("Tape strings, flute and choir coming soon");
        effects.setNote ("Effects coming soon");

        for (auto* c : std::initializer_list<juce::Component*> { &organ, &piano, &sample, &effects })
            addAndMakeVisible (c);
    }

    void ElectroPanel::resized()
    {
        auto area = getLocalBounds();
        auto fx = area.removeFromBottom (136);
        area.removeFromBottom (12);

        const int gap = 14;
        const int total = area.getWidth() - 2 * gap;
        organ.setBounds (area.removeFromLeft (total * 48 / 100));
        area.removeFromLeft (gap);
        piano.setBounds (area.removeFromLeft (total * 27 / 100));
        area.removeFromLeft (gap);
        sample.setBounds (area);
        effects.setBounds (fx);
    }
}
