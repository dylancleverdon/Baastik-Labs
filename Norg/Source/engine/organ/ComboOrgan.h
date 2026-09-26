#pragma once

#include "engine/dsp/DspUtils.h"

#include <array>

namespace norg::organ
{
    // Transistor combo organs (Vox, Farf) and a small pipe organ. Voice-based: each key sounds a
    // set of footages whose waveforms depend on the model; the nine drawbars pick the levels.
    class ComboOrgan
    {
    public:
        enum class Model { vox, farf, pipe };

        void prepare (double sampleRate);
        void reset();

        void setModel (Model);
        void setDrawbars (int bus, const std::array<float, 9>& gains); // bus: 0 upper, 1 lower

        void noteOn (int midiNote, int bus);
        void noteOff (int midiNote);
        void allNotesOff();

        // Adds numSamples into the upper and lower bus buffers.
        void render (float* upper, float* lower, int numSamples);

        bool isActive() const;

    private:
        enum class Wave { sine, flute, triangle, square, pulse30, pulse25, pulse12, saw, principal, stopped };

        struct Component
        {
            int semitones = 0;
            Wave wave = Wave::sine;
            float gain = 1.0f;
            float brightness = 0.0f; // one-pole low-pass at brightness x pitch (0 = open)
        };

        struct Stop
        {
            std::array<Component, 4> parts {};
            int numParts = 0;
        };

        static constexpr int maxParts = 20;

        struct Voice
        {
            int note = -1;
            int bus = 0;
            bool gate = false;
            float env = 0.0f;
            float chiff = 0.0f;
            dsp::Svf chiffFilter;
            juce::uint32 age = 0;
            std::array<float, maxParts> phase {}, increment {}, lpState {}, lpCoeff {}, level {};
            std::array<int, maxParts> stopIndex {};
            std::array<const Component*, maxParts> part {};
            int numParts = 0;
        };

        float oscillator (const Component&, float phase, float increment) const;
        void startVoice (Voice&, int note, int bus);
        const std::array<Stop, 9>& registration() const;

        double sampleRate = 44100.0;
        Model model = Model::vox;
        std::array<std::array<float, 9>, 2> drawbars {};
        std::array<Voice, 32> voices {};
        juce::uint32 noteCounter = 0;
        dsp::Noise noise { 0x5eed5u };
        dsp::Wavetable<2048> sineTable, fluteTable, triangleTable, principalTable, stoppedTable;
        std::array<Stop, 9> voxStops {}, farfStops {}, pipeStops {};
    };
}
