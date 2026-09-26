#pragma once

#include "Section.h"

#include <array>

namespace norg
{
    // A tiny placeholder instrument (three sine partials + envelope) used until the real section
    // engines land. `colour` scales the upper partial so each section sounds a little different.
    class TestToneSection final : public Section
    {
    public:
        explicit TestToneSection (float colour = 1.5f) : partialColour (colour) {}

        void prepare (double sampleRate, int maxBlockSize) override;
        void reset() override;
        void setParameters (const ParamSnapshot&, int) override {}
        void noteOn (int note, float velocity) override;
        void noteOff (int note, float releaseVelocity) override;
        void allNotesOff() override;
        void sustainPedal (float amount) override;
        void render (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) override;
        bool isActive() const override;

    private:
        struct Voice
        {
            int note = -1;
            bool keyDown = false;
            bool sustained = false;
            double phase = 0.0, increment = 0.0;
            float level = 0.0f, target = 0.0f, velocity = 0.0f;
            juce::uint32 age = 0;
        };

        void release (Voice&);

        std::array<Voice, 16> voices;
        double sampleRate = 44100.0;
        float attackStep = 0.0f, releaseStep = 0.0f;
        float partialColour;
        bool pedalDown = false;
        juce::uint32 noteCounter = 0;
    };
}
