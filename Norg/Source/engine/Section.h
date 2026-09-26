#pragma once

#include "params/Parameters.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace norg
{
    // One instrument section (organ, piano, synth or sample). A section renders stereo audio into
    // its own buffer; the engine handles on/off, level, mixing and effects around it.
    //
    // Real-time rules: nothing here may allocate, lock or do I/O except prepare().
    class Section
    {
    public:
        virtual ~Section() = default;

        virtual void prepare (double sampleRate, int maxBlockSize) = 0;
        virtual void reset() = 0;

        // Called once at the start of every block with that block's parameter values.
        virtual void setParameters (const ParamSnapshot&, int panel) = 0;

        virtual void noteOn (int note, float velocity) = 0;
        virtual void noteOff (int note, float releaseVelocity) = 0;
        virtual void allNotesOff() = 0;

        // 0 = pedal up, 1 = fully down (values in between are half-pedal).
        virtual void sustainPedal (float amount) { juce::ignoreUnused (amount); }

        // Adds `numSamples` of stereo output into `buffer`, starting at `startSample`.
        virtual void render (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) = 0;

        // True while the section is making (or about to make) sound.
        virtual bool isActive() const = 0;
    };
}
