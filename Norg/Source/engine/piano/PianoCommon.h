#pragma once

#include "engine/dsp/DspUtils.h"

namespace norg::piano
{
    // Settings shared by the piano engines, read from the parameters once per block.
    struct PianoSettings
    {
        int tineModel = 0;   // Mk I, Mk II, Suitcase
        int reedModel = 0;   // 200A, 140B
        int timbre = 1;      // Soft, Mid, Bright, Dyno
        int clavPickup = 2;  // Neck, Bridge, Both, Phase
        int clavFilter = 2;  // Soft, Medium, Treble, Brilliant
        bool softRelease = false;
        bool stretchTuning = true;
    };

    // One family of piano sounds. Engines keep track of which keys are held; the section tells them
    // about the sustain pedal, and each engine works out its own damping (including half-pedal).
    class PianoEngine
    {
    public:
        virtual ~PianoEngine() = default;
        virtual void prepare (double sampleRate, int maxBlockSize) = 0;
        virtual void reset() = 0;
        virtual void setSettings (const PianoSettings&) {}
        virtual void noteOn (int note, float velocity) = 0;
        virtual void noteOff (int note) = 0;
        virtual void setPedal (float amount) = 0; // 0 up .. 1 fully down
        virtual void allNotesOff() = 0;
        virtual void render (float* left, float* right, int numSamples) = 0; // adds
        virtual bool isActive() const = 0;
    };

    // How hard the dampers press on a string: 0 = free, 1 = fully damped. With the pedal half down
    // the dampers only brush the strings, which is what half-pedalling is.
    inline float damperAmount (bool keyHeld, float pedal)
    {
        if (keyHeld)
            return 0.0f;
        const float up = juce::jlimit (0.0f, 1.0f, 1.0f - pedal);
        return up * up * (3.0f - 2.0f * up); // smoothstep
    }

    // Railsback-style stretch: bass slightly flat, treble sharp (in cents, relative to A4).
    inline float stretchCents (int note)
    {
        const float d = static_cast<float> (note - 69);
        return d < 0.0f ? -0.0045f * d * d : 0.012f * d * d;
    }

    inline float equalPowerLeft (float pan)  { return std::cos ((pan + 1.0f) * 0.25f * juce::MathConstants<float>::pi); }
    inline float equalPowerRight (float pan) { return std::sin ((pan + 1.0f) * 0.25f * juce::MathConstants<float>::pi); }

    // A decaying sinusoid computed by complex rotation: a couple of multiplies per sample.
    struct Partial
    {
        float re = 0.0f, im = 0.0f;
        float cosW = 1.0f, sinW = 0.0f;
        float cr = 1.0f, ci = 0.0f;
        float naturalRate = 0.0f; // 1 / natural decay time (per second)
        float dampedRate = 0.0f;  // extra decay rate when fully damped

        void start (float freqHz, float amplitude, float phase, float tauSeconds, float dampTauSeconds, float sampleRate)
        {
            const float w = dsp::twoPi * freqHz / sampleRate;
            cosW = std::cos (w);
            sinW = std::sin (w);
            re = amplitude * std::cos (phase);
            im = amplitude * std::sin (phase);
            naturalRate = 1.0f / juce::jmax (0.001f, tauSeconds);
            dampedRate = 1.0f / juce::jmax (0.001f, dampTauSeconds);
            setDamping (0.0f, sampleRate);
        }

        void setDamping (float damper, float sampleRate)
        {
            const float r = std::exp (-(naturalRate + damper * dampedRate) / sampleRate);
            cr = r * cosW;
            ci = r * sinW;
        }

        float tick()
        {
            const float nr = re * cr - im * ci;
            im = re * ci + im * cr;
            re = nr;
            return im;
        }

        float energy() const { return re * re + im * im; }
    };
}
