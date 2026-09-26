#pragma once

#include "PluginProcessor.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>

namespace norg::test
{
    struct RenderResult
    {
        float peak = 0.0f;
        double rms = 0.0;
        bool hasNonFinite = false;
        juce::AudioBuffer<float> audio;
    };

    inline void prepare (juce::AudioProcessor& p, double rate = 48000.0, int block = 256)
    {
        p.setPlayConfigDetails (0, 2, rate, block);
        p.prepareToPlay (rate, block);
    }

    // Renders `numSamples`, sending `midi` (sample positions relative to the start) in the first block.
    inline RenderResult render (juce::AudioProcessor& p, const juce::MidiBuffer& midi, int numSamples, int block = 256)
    {
        RenderResult r;
        r.audio.setSize (2, numSamples);
        juce::AudioBuffer<float> buffer;
        double sumSquares = 0.0;

        for (int start = 0; start < numSamples; start += block)
        {
            const int n = juce::jmin (block, numSamples - start);
            buffer.setSize (2, n, false, false, true);
            buffer.clear();

            juce::MidiBuffer chunk;
            for (const auto m : midi)
                if (m.samplePosition >= start && m.samplePosition < start + n)
                    chunk.addEvent (m.getMessage(), m.samplePosition - start);

            p.processBlock (buffer, chunk);

            for (int ch = 0; ch < 2; ++ch)
            {
                r.audio.copyFrom (ch, start, buffer, ch, 0, n);
                for (int i = 0; i < n; ++i)
                {
                    const float s = buffer.getSample (ch, i);
                    if (! std::isfinite (s))
                        r.hasNonFinite = true;
                    r.peak = juce::jmax (r.peak, std::abs (s));
                    sumSquares += static_cast<double> (s) * s;
                }
            }
        }

        r.rms = std::sqrt (sumSquares / juce::jmax (1, 2 * numSamples));
        return r;
    }

    inline void setParam (NorgProcessor& p, const juce::String& id, float plainValue)
    {
        auto* param = p.state().getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (plainValue));
    }

    inline float getParam (NorgProcessor& p, const juce::String& id)
    {
        auto* param = p.state().getParameter (id);
        REQUIRE (param != nullptr);
        return param->convertFrom0to1 (param->getValue());
    }
}
