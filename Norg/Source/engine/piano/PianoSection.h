#pragma once

#include "Clavinet.h"
#include "ElectricPiano.h"
#include "ModeledPiano.h"
#include "SamplePlayer.h"
#include "StringResonance.h"
#include "engine/Section.h"

namespace norg::piano
{
    // The piano section: Grand and Upright (sampled when a library is installed, modelled
    // otherwise), Tine and Reed electric pianos and Clav, with string resonance, pedal noise,
    // soft release, stretch tuning and half-pedal support.
    class PianoSection final : public Section
    {
    public:
        enum class Type { grand, upright, tine, reed, clav };

        // Library slots for this panel in the shared LibraryManager.
        static int grandSlot (int panel)   { return panel * 3; }
        static int uprightSlot (int panel) { return panel * 3 + 1; }

        explicit PianoSection (sfz::LibraryManager* libraryManager = nullptr)
            : libraries (libraryManager),
              engines { &modeled, &grandPlayer, &uprightPlayer, &tine, &reed, &clav }
        {
        }

        void prepare (double sampleRate, int maxBlockSize) override;
        void reset() override;
        void setParameters (const ParamSnapshot&, int panel) override;
        void noteOn (int note, float velocity) override;
        void noteOff (int note, float releaseVelocity) override;
        void allNotesOff() override;
        void sustainPedal (float amount) override;
        void render (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) override;
        bool isActive() const override;

        Type type() const { return currentType; }
        bool usingSamples() const;

    private:
        PianoEngine* engineFor (Type);
        bool isAcoustic() const { return currentType == Type::grand || currentType == Type::upright; }
        void triggerPedalNoise (bool down);

        sfz::LibraryManager* libraries;
        double sampleRate = 44100.0;
        int maxBlock = 512;

        ModeledPiano modeled;
        SamplePlayer grandPlayer, uprightPlayer;
        ElectricPiano tine { ElectricPiano::Kind::tine }, reed { ElectricPiano::Kind::reed };
        Clavinet clav;
        StringResonance resonance;
        std::array<PianoEngine*, 6> engines {};
        std::array<PianoEngine*, 128> noteEngine {};

        Type currentType = Type::tine;
        bool useSamples = true, stringResonance = true, pedalNoise = true;
        std::array<bool, 128> held {}, struck {}; // struck: sounding since the pedal went down
        float pedal = 0.0f;
        bool pedalWasDown = false;

        float pedalNoiseEnv = 0.0f, pedalNoiseLevel = 0.0f;
        dsp::Svf pedalNoiseFilter;
        dsp::OnePole pedalThumpFilter;
        dsp::Noise noise { 0x5ed41u };

        std::vector<float> left, right;
        int tailSamples = 0;
    };
}
