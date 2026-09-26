#pragma once

#include "engine/dsp/DspUtils.h"

#include <juce_dsp/juce_dsp.h>

namespace norg::fx
{
    // A rotating-speaker cabinet: tube preamp drive, then a crossover into a spinning treble horn
    // and bass drum, each with its own inertia, picked up by two microphones.
    class RotarySpeaker
    {
    public:
        void prepare (double sampleRate, int maxBlockSize);
        void reset();

        void setEnabled (bool on) { targetOn = on ? 1.0f : 0.0f; }
        // fast = the fast (tremolo) speed; with stopMode the slow setting brakes the rotors to a stop.
        void setSpeed (bool fast, bool stopMode);
        void setDrive (float amount) { drive = juce::jlimit (0.0f, 1.0f, amount); }

        // Stereo, in place. The cabinet is fed the mono sum; switched off, it cross-fades back to
        // the untouched stereo input and then stops processing.
        void process (float* left, float* right, int numSamples);
        bool isRunning() const { return targetOn > 0.0f || onMix > 0.0f; }

        float hornSpeedHz() const { return hornSpeed; }
        float drumSpeedHz() const { return drumSpeed; }

    private:
        void applyDrive (float* samples, int numSamples);

        double sampleRate = 44100.0;
        int maxBlock = 512;

        std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
        std::vector<float> driveBuffer;
        float drive = 0.25f;
        dsp::OnePole preampTone, postDriveTone;
        dsp::Biquad dcBlock;

        dsp::Biquad drumLow1, drumLow2, hornHigh1, hornHigh2;
        dsp::DelayLine hornDelay, drumDelay;
        dsp::OnePole hornToneL, hornToneR;

        float hornTarget = 0.8f, drumTarget = 0.7f;
        float hornSpeed = 0.8f, drumSpeed = 0.7f;
        float hornAngle = 0.0f, drumAngle = 1.3f;
        float targetOn = 1.0f, onMix = 1.0f;
    };
}
