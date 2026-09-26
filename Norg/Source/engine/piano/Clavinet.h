#pragma once

#include "PianoCommon.h"

#include <array>
#include <vector>

namespace norg::piano
{
    // A clavinet: each key's tangent strikes and holds the string (a plucked-string waveguide),
    // two pickups under the strings (neck, bridge, both, or both out of phase), four tone switches,
    // and the yarn damper that chokes the string when the key comes up.
    class Clavinet final : public PianoEngine
    {
    public:
        void prepare (double sampleRate, int maxBlockSize) override;
        void reset() override;
        void setSettings (const PianoSettings&) override;
        void noteOn (int note, float velocity) override;
        void noteOff (int note) override;
        void setPedal (float amount) override;
        void allNotesOff() override;
        void render (float* left, float* right, int numSamples) override;
        bool isActive() const override;

    private:
        struct Voice
        {
            int note = -1;
            bool held = false;
            float period = 100.0f;       // delay length in samples
            float naturalGain = 0.999f;  // loop gain per period, undamped
            float dampedGain = 0.5f;     // loop gain per period, fully damped
            float loopGain = 0.999f;
            float brightness = 0.5f;     // loop low-pass coefficient
            float lpState = 0.0f;
            float thump = 0.0f;
            float quiet = 0.0f;          // running level, to retire silent voices
            juce::uint32 age = 0;
            int write = 0;
            std::vector<float> line;
        };

        float readLine (const Voice&, float delay) const;
        void updateDamping (Voice&);
        void configureFilters();

        PianoSettings settings;
        double sampleRate = 44100.0;
        int lineSize = 2048;
        std::array<Voice, 16> voices;
        juce::uint32 counter = 0;
        float pedal = 0.0f;
        dsp::Noise noise { 0xc1a7u };
        std::array<dsp::Biquad, 3> tone;
        dsp::OnePole thumpFilter;
        std::vector<float> scratch;
    };
}
