#include "ScannerVibrato.h"

namespace norg::organ
{
    void ScannerVibrato::prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        delay.prepare (static_cast<int> (0.01 * sampleRate));
        lineBox.setCutoff (6500.0f, sampleRate);
        lfoIncrement = static_cast<float> (6.87 / sampleRate);
        smoothing = static_cast<float> (1.0 - std::exp (-1.0 / (0.02 * sampleRate)));
        reset();
    }

    void ScannerVibrato::reset()
    {
        delay.reset();
        lineBox.reset();
        lfoPhase = 0.0f;
        depthSamples = targetDepth;
        wetMix = targetWet;
        dryMix = targetDry;
    }

    void ScannerVibrato::set (bool enabled, int mode)
    {
        mode = juce::jlimit (0, 5, mode);
        const int depthIndex = mode / 2;             // 1, 2, 3
        const bool chorus = (mode % 2) == 1;
        constexpr float depthsMs[] = { 0.3f, 0.55f, 0.9f };

        targetDepth = static_cast<float> (depthsMs[depthIndex] * 0.001 * sampleRate);
        if (! enabled)
        {
            targetWet = 0.0f;
            targetDry = 1.0f;
        }
        else if (chorus)
        {
            targetWet = 0.6f;
            targetDry = 0.6f;
        }
        else
        {
            targetWet = 1.0f;
            targetDry = 0.0f;
        }
    }

    void ScannerVibrato::process (float* buffer, int n)
    {
        const float base = static_cast<float> (0.0006 * sampleRate);

        for (int i = 0; i < n; ++i)
        {
            depthSamples += smoothing * (targetDepth - depthSamples);
            wetMix += smoothing * (targetWet - wetMix);
            dryMix += smoothing * (targetDry - dryMix);

            const float x = buffer[i];
            delay.push (x);

            // The scanner sweeps back and forth: a triangle-ish sweep, softened.
            const float tri = 2.0f * std::abs (2.0f * lfoPhase - 1.0f) - 1.0f;
            const float sweep = 0.5f + 0.5f * (1.5f * tri - 0.5f * tri * tri * tri);
            lfoPhase += lfoIncrement;
            if (lfoPhase >= 1.0f)
                lfoPhase -= 1.0f;

            const float wet = lineBox.process (delay.read (base + depthSamples * sweep));
            buffer[i] = dryMix * x + wetMix * wet;
        }
    }
}
