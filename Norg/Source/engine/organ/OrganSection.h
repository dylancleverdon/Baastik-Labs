#pragma once

#include "ComboOrgan.h"
#include "ScannerVibrato.h"
#include "TonewheelOrgan.h"
#include "engine/Section.h"
#include "engine/fx/RotarySpeaker.h"

namespace norg::organ
{
    // The organ section: B3 tonewheels or Vox/Farf/Pipe, drawbar presets I/II, organ split with
    // lower manual and bass pedals, percussion, scanner vibrato, swell and the rotary speaker.
    class OrganSection final : public Section
    {
    public:
        enum class Model { b3, vox, farf, pipe };

        // Drawbar positions (0-8) to gain: each step is about 3 dB, like the real thing.
        static float drawbarGain (int position);

        void prepare (double sampleRate, int maxBlockSize) override;
        void reset() override;
        void setParameters (const ParamSnapshot&, int panel) override;
        void noteOn (int note, float velocity) override;
        void noteOff (int note, float releaseVelocity) override;
        void allNotesOff() override;
        void sustainPedal (float amount) override;
        void expression (float amount) override;
        void render (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) override;
        bool isActive() const override;

        int busForNote (int note) const;

    private:
        void release (int note);

        double sampleRate = 44100.0;
        int maxBlock = 512;

        TonewheelOrgan tonewheels;
        ComboOrgan combo;
        ScannerVibrato upperVibrato, lowerVibrato;
        fx::RotarySpeaker rotary;

        std::vector<float> mono, left, right;

        Model model = Model::b3;
        bool split = false, pedals = true, sustainToOrgan = false;
        int splitPoint = 60, pedalPoint = 48;
        bool vibOn = false, vibLower = false;
        int vibMode = 5;

        std::array<bool, 128> held {}, sustained {};
        bool pedalDown = false;

        float swellTarget = 1.0f, swell = 1.0f, swellCoeff = 0.001f;
        int tailSamples = 0;
    };
}
