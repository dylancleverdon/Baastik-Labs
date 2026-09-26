#pragma once

#include "PianoCommon.h"
#include "engine/sfz/SampleLibrary.h"

#include <array>

namespace norg::piano
{
    // Plays an SFZ instrument: velocity layers, release triggers (with rt_decay), off_by groups,
    // loops, cubic interpolation, and damping that follows the keys and the sustain pedal
    // (including half-pedal). Used for sampled grand/upright pianos and the Sample section.
    class SamplePlayer final : public PianoEngine
    {
    public:
        // Swapped in from the audio thread; voices keep the instrument they started with alive.
        void setInstrument (std::shared_ptr<const sfz::Instrument> instrument);
        bool hasInstrument() const { return instrument != nullptr; }

        // Top-octave strings on a piano have no dampers; other instruments damp every note.
        void setPianoDamping (bool enabled) { pianoDamping = enabled; }
        void setGain (float g) { outputGain = g; }

        void prepare (double sampleRate, int maxBlockSize) override;
        void reset() override;
        void setSettings (const PianoSettings& s) override { settings = s; }
        void noteOn (int note, float velocity) override;
        void noteOff (int note) override;
        void setPedal (float amount) override;
        void allNotesOff() override;
        void render (float* left, float* right, int numSamples) override;
        bool isActive() const override;

    private:
        struct Voice
        {
            std::shared_ptr<const sfz::Instrument> instrument;
            const sfz::Region* region = nullptr;
            const sfz::SampleSource* source = nullptr;
            int note = -1;
            bool held = false;
            bool releaseFired = false;
            double position = 0.0, increment = 1.0;
            float gainL = 1.0f, gainR = 1.0f;
            float env = 1.0f, envRate = 1.0f;    // per-sample multiplier while damped
            float attack = 1.0f, attackStep = 1.0f;
            float fade = 1.0f, fadeStep = 0.0f;  // quick fade for off_by and voice stealing
            float velocity = 0.0f;
            juce::int64 startSample = 0;
            juce::uint32 age = 0;
        };

        void start (const sfz::Region&, int note, float velocity, float gainScale);
        bool playable (const sfz::Region&, float random) const;
        void updateDamping (Voice&);
        void fireReleaseSamples (Voice&);
        Voice& allocateVoice();

        std::shared_ptr<const sfz::Instrument> instrument;
        PianoSettings settings;
        double sampleRate = 44100.0;
        std::array<Voice, 64> voices;
        juce::uint32 counter = 0;
        juce::int64 clock = 0;
        float pedal = 0.0f;
        int activeSwitch = -1;
        dsp::Noise random { 0x51f2u };
        bool pianoDamping = true;
        float outputGain = 0.5f;
    };
}
