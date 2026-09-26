#pragma once

#include "PianoCommon.h"

#include <array>

namespace norg::piano
{
    // Acoustic grand and upright pianos built from physics rather than samples: each note has one
    // to three slightly detuned strings, each a set of stretched (inharmonic) partials whose
    // brightness depends on how hard the hammer hits. Used when no sample library is loaded.
    class ModeledPiano final : public PianoEngine
    {
    public:
        enum class Variant { grand, upright };

        void setVariant (Variant v) { variant = v; }

        void prepare (double sampleRate, int maxBlockSize) override;
        void reset() override;
        void setSettings (const PianoSettings& s) override { settings = s; }
        void noteOn (int note, float velocity) override;
        void noteOff (int note) override;
        void setPedal (float amount) override;
        void allNotesOff() override;
        void render (float* left, float* right, int numSamples) override;
        bool isActive() const override;

        // The top of the keyboard has no dampers on a real piano.
        static bool isUndamped (int note) { return note >= 89; }

    private:
        static constexpr int maxPartials = 42;

        struct Voice
        {
            int note = -1;
            bool held = false;
            Variant variant = Variant::grand;
            float panL = 0.7f, panR = 0.7f;
            float thump = 0.0f;
            juce::uint32 age = 0;
            int numPartials = 0;
            std::array<Partial, maxPartials> partials;
            dsp::OnePole thumpFilter;
        };

        void updateDamping (Voice&);
        void configureBody();

        Variant variant = Variant::grand;
        Variant bodyVariant = Variant::grand;
        PianoSettings settings;
        double sampleRate = 44100.0;
        std::array<Voice, 32> voices;
        juce::uint32 counter = 0;
        float pedal = 0.0f;
        dsp::Noise noise { 0x9a0du };
        std::array<dsp::Biquad, 3> bodyL, bodyR;
        std::vector<float> scratchL, scratchR;
    };
}
