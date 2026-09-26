#pragma once

#include "Section.h"
#include "sfz/SampleLibrary.h"

#include <array>
#include <memory>

namespace norg
{
    enum class SectionId : int { organ, piano, synth, sample, count };
    inline constexpr int numSections = static_cast<int> (SectionId::count);

    // One complete instrument: every section of one panel, their mixing and effects.
    class NorgEngine
    {
    public:
        explicit NorgEngine (int panelIndex = 0, sfz::LibraryManager* libraries = nullptr);
        ~NorgEngine();

        void prepare (double sampleRate, int maxBlockSize);
        void reset();

        // Renders one block, replacing the contents of `output` (stereo).
        void process (juce::AudioBuffer<float>& output, const juce::MidiBuffer& midi, const ParamSnapshot&);

        bool isActive() const;
        int panel() const { return panelIndex; }

    private:
        struct Slot
        {
            std::unique_ptr<Section> section;
            juce::AudioBuffer<float> buffer;
            juce::SmoothedValue<float> gain;
            P onParam = P::organOn;
            P volumeParam = P::organVolume;
            bool enabled = false;
        };

        void handleMidi (const juce::MidiMessage&);
        void renderRange (int start, int num);
        bool sectionAvailable (SectionId, const ParamSnapshot&) const;

        int panelIndex;
        double sampleRate = 44100.0;
        int maxBlockSize = 512;
        std::array<Slot, numSections> slots;
        juce::SmoothedValue<float> masterGain;
    };
}
