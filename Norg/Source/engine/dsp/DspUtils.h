#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <cmath>
#include <cstdint>

// Small real-time-safe DSP helpers shared by the section engines.
namespace norg::dsp
{
    inline constexpr float twoPi = juce::MathConstants<float>::twoPi;

    inline float dbToGain (float db) { return std::pow (10.0f, db * 0.05f); }

    inline float noteHz (float midiNote) { return 440.0f * std::pow (2.0f, (midiNote - 69.0f) / 12.0f); }

    // A fast, deterministic noise source (xorshift32), for clicks, breath and chiff.
    class Noise
    {
    public:
        explicit Noise (uint32_t seed = 0x9e3779b9u) : state (seed ? seed : 1u) {}

        float next() // uniform in [-1, 1)
        {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return static_cast<float> (static_cast<int32_t> (state)) * (1.0f / 2147483648.0f);
        }

        float uniform01() { return 0.5f * (next() + 1.0f); }

    private:
        uint32_t state;
    };

    // One-pole low-pass: y += a (x - y).
    struct OnePole
    {
        float a = 1.0f, y = 0.0f;

        void setCutoff (float hz, double sampleRate)
        {
            const float x = std::exp (-twoPi * juce::jlimit (1.0f, 0.49f * static_cast<float> (sampleRate), hz)
                                      / static_cast<float> (sampleRate));
            a = 1.0f - x;
        }

        float process (float x) { y += a * (x - y); return y; }
        void reset() { y = 0.0f; }
    };

    // Topology-preserving state-variable filter (Cytomic/Simper style): stable under modulation.
    struct Svf
    {
        float g = 0.0f, k = 1.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
        float ic1 = 0.0f, ic2 = 0.0f;
        float low = 0.0f, band = 0.0f, high = 0.0f;

        void set (float cutoffHz, float q, double sampleRate)
        {
            const float nyquistSafe = 0.48f * static_cast<float> (sampleRate);
            g = std::tan (juce::MathConstants<float>::pi * juce::jlimit (5.0f, nyquistSafe, cutoffHz)
                          / static_cast<float> (sampleRate));
            k = 1.0f / juce::jmax (0.05f, q);
            a1 = 1.0f / (1.0f + g * (g + k));
            a2 = g * a1;
            a3 = g * a2;
        }

        void process (float v0)
        {
            const float v3 = v0 - ic2;
            const float v1 = a1 * ic1 + a2 * v3;
            const float v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0f * v1 - ic1;
            ic2 = 2.0f * v2 - ic2;
            low = v2;
            band = v1;
            high = v0 - k * v1 - v2;
        }

        void reset() { ic1 = ic2 = low = band = high = 0.0f; }
    };

    // RBJ biquad (transposed direct form II), for fixed EQ and crossover shapes.
    struct Biquad
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        float z1 = 0, z2 = 0;

        float process (float x)
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }

        void reset() { z1 = z2 = 0.0f; }

        void setLowPass (float hz, float q, double sr)  { design (0, hz, q, 0.0f, sr); }
        void setHighPass (float hz, float q, double sr) { design (1, hz, q, 0.0f, sr); }
        void setPeak (float hz, float q, float gainDb, double sr) { design (2, hz, q, gainDb, sr); }
        void setLowShelf (float hz, float gainDb, double sr)  { design (3, hz, 0.707f, gainDb, sr); }
        void setHighShelf (float hz, float gainDb, double sr) { design (4, hz, 0.707f, gainDb, sr); }

    private:
        void design (int type, float hz, float q, float gainDb, double sr)
        {
            const float w0 = twoPi * juce::jlimit (5.0f, 0.49f * static_cast<float> (sr), hz) / static_cast<float> (sr);
            const float cw = std::cos (w0), sw = std::sin (w0);
            const float alpha = sw / (2.0f * juce::jmax (0.05f, q));
            const float A = std::pow (10.0f, gainDb / 40.0f);
            float nb0 = 1, nb1 = 0, nb2 = 0, na0 = 1, na1 = 0, na2 = 0;

            switch (type)
            {
                case 0: nb0 = (1 - cw) / 2; nb1 = 1 - cw; nb2 = (1 - cw) / 2; na0 = 1 + alpha; na1 = -2 * cw; na2 = 1 - alpha; break;
                case 1: nb0 = (1 + cw) / 2; nb1 = -(1 + cw); nb2 = (1 + cw) / 2; na0 = 1 + alpha; na1 = -2 * cw; na2 = 1 - alpha; break;
                case 2: nb0 = 1 + alpha * A; nb1 = -2 * cw; nb2 = 1 - alpha * A; na0 = 1 + alpha / A; na1 = -2 * cw; na2 = 1 - alpha / A; break;
                case 3:
                {
                    const float s = 2 * std::sqrt (A) * alpha;
                    nb0 = A * ((A + 1) - (A - 1) * cw + s); nb1 = 2 * A * ((A - 1) - (A + 1) * cw); nb2 = A * ((A + 1) - (A - 1) * cw - s);
                    na0 = (A + 1) + (A - 1) * cw + s; na1 = -2 * ((A - 1) + (A + 1) * cw); na2 = (A + 1) + (A - 1) * cw - s;
                    break;
                }
                default:
                {
                    const float s = 2 * std::sqrt (A) * alpha;
                    nb0 = A * ((A + 1) + (A - 1) * cw + s); nb1 = -2 * A * ((A - 1) + (A + 1) * cw); nb2 = A * ((A + 1) + (A - 1) * cw - s);
                    na0 = (A + 1) - (A - 1) * cw + s; na1 = 2 * ((A - 1) - (A + 1) * cw); na2 = (A + 1) - (A - 1) * cw - s;
                    break;
                }
            }

            b0 = nb0 / na0; b1 = nb1 / na0; b2 = nb2 / na0; a1 = na1 / na0; a2 = na2 / na0;
        }
    };

    // A lookup table for one cycle of a periodic waveform, read with linear interpolation.
    template <int Size = 2048>
    struct Wavetable
    {
        std::array<float, static_cast<size_t> (Size) + 1> table {};

        template <typename Fn>
        void fill (Fn&& fn)
        {
            for (int i = 0; i <= Size; ++i)
                table[static_cast<size_t> (i)] = fn (twoPi * static_cast<float> (i) / Size);
        }

        // phase in [0, 1)
        float read (float phase) const
        {
            const float pos = phase * Size;
            const int i = static_cast<int> (pos);
            const float frac = pos - static_cast<float> (i);
            const float a = table[static_cast<size_t> (i)];
            return a + frac * (table[static_cast<size_t> (i + 1)] - a);
        }
    };

    // PolyBLEP correction for band-limited saw/pulse oscillators.
    inline float polyBlep (float t, float dt)
    {
        if (t < dt)
        {
            t /= dt;
            return t + t - t * t - 1.0f;
        }
        if (t > 1.0f - dt)
        {
            t = (t - 1.0f) / dt;
            return t * t + t + t + 1.0f;
        }
        return 0.0f;
    }

    // Soft saturation with an adjustable knee; unity gain for small signals.
    inline float softClip (float x)
    {
        if (x > 1.5f) return 1.0f;
        if (x < -1.5f) return -1.0f;
        return x - (4.0f / 27.0f) * x * x * x;
    }

    // A fractional delay line (linear interpolation), sized once in prepare().
    class DelayLine
    {
    public:
        void prepare (int maxSamples)
        {
            buffer.assign (static_cast<size_t> (juce::nextPowerOfTwo (maxSamples + 4)), 0.0f);
            mask = static_cast<int> (buffer.size()) - 1;
            write = 0;
        }

        void reset() { std::fill (buffer.begin(), buffer.end(), 0.0f); }

        void push (float x)
        {
            write = (write + 1) & mask;
            buffer[static_cast<size_t> (write)] = x;
        }

        // Read `delay` samples behind the last pushed sample.
        float read (float delay) const
        {
            const int whole = static_cast<int> (delay);
            const float frac = delay - static_cast<float> (whole);
            const float a = buffer[static_cast<size_t> ((write - whole) & mask)];
            const float b = buffer[static_cast<size_t> ((write - whole - 1) & mask)];
            return a + frac * (b - a);
        }

    private:
        std::vector<float> buffer { 0.0f };
        int mask = 0, write = 0;
    };
}
