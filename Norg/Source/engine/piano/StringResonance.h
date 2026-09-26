#pragma once

#include "PianoCommon.h"

#include <array>

namespace norg::piano
{
    // Sympathetic string resonance: with the sustain pedal down (or keys held), undamped strings
    // pick up and ring along with whatever is played. A bank of tuned two-pole resonators, each
    // damped or free exactly like the string it stands for.
    class StringResonance
    {
    public:
        static constexpr int lowestNote = 36;
        static constexpr int numStrings = 48;

        void prepare (double sampleRate);
        void reset();

        // Called when keys or the pedal change. `sounding` marks strings that were struck (they are
        // the source, not sympathetic strings); the others ring along when the pedal frees them.
        void setDamping (const std::array<bool, 128>& sounding, float pedal);

        // Reads the dry stereo signal and adds the resonance back into it.
        void process (float* left, float* right, int numSamples);

    private:
        struct Resonator
        {
            float coeff1 = 0.0f, coeff2 = 0.0f; // 2 r cos(w), -r^2
            float cosW = 1.0f, sinW = 0.0f;
            float inputGain = 0.0f;             // unity, zero-phase gain at resonance
            float y1 = 0.0f, y2 = 0.0f;
            float pan = 0.0f;
            bool free = false;
        };

        double sampleRate = 44100.0;
        std::array<Resonator, numStrings> strings;
        float freeR = 0.9999f, dampedR = 0.99f;
        float energy = 0.0f;
        float in1 = 0.0f, in2 = 0.0f;
    };
}
