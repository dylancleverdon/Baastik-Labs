#pragma once

#include "Section.h"
#include "fx/Effects.h"
#include "fx/RotarySpeaker.h"
#include "sfz/SampleLibrary.h"

#include <array>
#include <memory>

namespace norg
{
    enum class SectionId : int { organ, piano, synth, sample, count };
    inline constexpr int numSections = static_cast<int> (SectionId::count);

    // Delay divisions in beats, matching the delay_division choices.
    float delayDivisionBeats (int division);

    // One complete instrument: every section of one panel, their mixing and effects.
    //
    // Each section renders into its own buffer at its volume, then Effect 1, Effect 2, Amp/EQ,
    // Rotary and Delay process whichever section their source switch picks. The sections are
    // summed, and the compressor and reverb work on the panel's mix.
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

        // The master clock's tempo, for tempo-synced effects.
        void setTempo (double bpm) { tempo = juce::jlimit (20.0, 400.0, bpm); }

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
        void setEffects (const ParamSnapshot&);
        void processEffects (int chunkLength);
        SectionId sourceSection (int source, const ParamSnapshot&) const;

        int panelIndex;
        double sampleRate = 44100.0;
        int maxBlockSize = 512;
        std::array<Slot, numSections> slots;
        juce::SmoothedValue<float> masterGain;
        double tempo = 120.0;

        // The per-section chain, in signal order, with the section each block is processing.
        fx::ModEffect1 effect1;
        fx::ModEffect2 effect2;
        fx::AmpEq ampEq;
        fx::RotarySpeaker rotary;
        fx::StereoDelay delay;
        std::array<SectionId, 5> chainSource {};

        // The panel mix.
        fx::Compressor compressor;
        fx::Reverb reverb;
        juce::AudioBuffer<float> mix;
    };
}
