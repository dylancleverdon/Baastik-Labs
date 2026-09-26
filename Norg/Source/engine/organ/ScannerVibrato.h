#pragma once

#include "engine/dsp/DspUtils.h"

namespace norg::organ
{
    // The tonewheel organ's scanner vibrato: a delay line swept at ~6.9 Hz. V1-V3 are pure
    // vibrato of increasing depth; C1-C3 mix it with the dry signal for the classic chorus.
    class ScannerVibrato
    {
    public:
        void prepare (double sampleRate);
        void reset();

        // mode: 0 V1, 1 C1, 2 V2, 3 C2, 4 V3, 5 C3
        void set (bool enabled, int mode);

        void process (float* buffer, int numSamples);

    private:
        double sampleRate = 44100.0;
        dsp::DelayLine delay;
        dsp::OnePole lineBox; // the scanner's delay line rolls off the top end
        float lfoPhase = 0.0f, lfoIncrement = 0.0f;
        float depthSamples = 0.0f, targetDepth = 0.0f;
        float wetMix = 0.0f, targetWet = 0.0f, dryMix = 1.0f, targetDry = 1.0f;
        float smoothing = 0.001f;
    };
}
