#include "StringResonance.h"

namespace norg::piano
{
    void StringResonance::prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        const auto sr = static_cast<float> (sampleRate);
        freeR = std::exp (-1.0f / (1.5f * sr));   // an undamped string rings for seconds
        dampedR = std::exp (-1.0f / (0.02f * sr)); // a damped one barely at all

        for (int i = 0; i < numStrings; ++i)
        {
            auto& s = strings[static_cast<size_t> (i)];
            const int note = lowestNote + i;
            const float w = dsp::twoPi * dsp::noteHz (static_cast<float> (note)) / sr;
            s.cosW = std::cos (w);
            s.sinW = std::sin (w);
            s.pan = juce::jlimit (-1.0f, 1.0f, (static_cast<float> (note) - 64.0f) / 48.0f) * 0.6f;
        }

        std::array<bool, 128> none {};
        setDamping (none, 0.0f);
        reset();
    }

    void StringResonance::reset()
    {
        for (auto& s : strings)
            s.y1 = s.y2 = 0.0f;
        energy = 0.0f;
        in1 = in2 = 0.0f;
    }

    void StringResonance::setDamping (const std::array<bool, 128>& sounding, float pedal)
    {
        for (int i = 0; i < numStrings; ++i)
        {
            auto& s = strings[static_cast<size_t> (i)];
            const float d = sounding[static_cast<size_t> (lowestNote + i)] ? 1.0f : damperAmount (false, pedal);
            const float r = freeR + d * (dampedR - freeR);
            s.coeff1 = 2.0f * r * s.cosW;
            s.coeff2 = -r * r;
            s.inputGain = 1.5f * (1.0f - r * r); // band-pass form: coupling x (1 - r^2)/2 (1 - z^-2)
            s.free = d < 0.5f;
        }
    }

    void StringResonance::process (float* left, float* right, int n)
    {
        bool anyFree = false;
        for (const auto& s : strings)
            anyFree = anyFree || s.free;

        // Nothing to do when every string is damped and the last ring has died away.
        if (! anyFree && energy < 1.0e-9f)
            return;

        constexpr float excite = 0.5f;
        constexpr float mix = 0.2f;
        float blockEnergy = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            const float in = excite * (left[i] + right[i]);
            const float drive = in - in2; // zero phase at each resonance
            in2 = in1;
            in1 = in;
            float l = 0.0f, r = 0.0f;
            for (auto& s : strings)
            {
                const float y = s.inputGain * drive + s.coeff1 * s.y1 + s.coeff2 * s.y2;
                s.y2 = s.y1;
                s.y1 = y;
                l += y * (0.5f - 0.5f * s.pan);
                r += y * (0.5f + 0.5f * s.pan);
            }
            left[i] += mix * l;
            right[i] += mix * r;
            blockEnergy += l * l + r * r;
        }

        energy = blockEnergy / static_cast<float> (juce::jmax (1, n));
    }
}
