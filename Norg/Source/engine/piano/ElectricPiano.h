#pragma once

#include "PianoCommon.h"

#include <array>

namespace norg::piano
{
    // Electric pianos, modelled rather than sampled:
    //  - Tine: a struck tine (plus its tonebar) in front of an electromagnetic pickup. The pickup's
    //    asymmetric response is what makes the "bark" when you dig in.
    //  - Reed: a struck steel reed read by an electrostatic pickup, which growls at high velocity.
    class ElectricPiano final : public PianoEngine
    {
    public:
        enum class Kind { tine, reed };

        explicit ElectricPiano (Kind k) : kind (k) {}

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
        static constexpr int numPartials = 4;

        struct Voice
        {
            int note = -1;
            bool held = false;
            float damper = 0.0f;
            float drive = 1.0f;
            float gain = 0.0f;
            float thump = 0.0f;
            float life = 0.0f; // counts down once the voice is quiet
            juce::uint32 age = 0;
            std::array<Partial, numPartials> partials;
            dsp::OnePole thumpFilter;
        };

        void updateDamping (Voice&);
        void configureTone();
        float pickup (float x, float drive) const;

        Kind kind;
        PianoSettings settings;
        double sampleRate = 44100.0;
        std::array<Voice, 32> voices;
        juce::uint32 counter = 0;
        float pedal = 0.0f;
        float pickupOffset = 0.3f, timbreDrive = 1.0f;
        dsp::Noise noise { 0x7e1a5u };
        std::array<dsp::Biquad, 4> tone;
        std::vector<float> scratch;
    };
}
