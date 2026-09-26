#pragma once

#include "engine/dsp/DspUtils.h"

#include <juce_dsp/juce_dsp.h>

#include <array>
#include <vector>

namespace norg::fx
{
    // Base for an effect block that can be switched on and off without clicks.
    //
    // Insert effects (modulation, amp, compressor) cross-fade between the dry and processed signal
    // over ~20 ms and stop processing once fully off. Send effects (delay, reverb) fade their input
    // instead, so switching them off lets the repeats and the reverb tail ring out naturally.
    class FxBlock
    {
    public:
        virtual ~FxBlock() = default;

        void prepareBlock (double sampleRate, int maxBlockSize);
        void setEnabled (bool on) { enabled = on; }
        bool isEnabled() const { return enabled; }

        // Processes a stereo buffer in place.
        void process (float* left, float* right, int numSamples);

        // True while the block does anything to the signal (on, fading, or still ringing).
        bool isRunning() const { return enabled || fade > 0.0f || hasTail(); }

        // True while the block is still producing a tail (delay repeats, reverb decay).
        virtual bool hasTail() const { return false; }
        virtual void reset() {}

    protected:
        virtual void prepare (double sampleRate, int maxBlockSize) = 0;

        // Insert effects: replace the buffer with the processed signal.
        virtual void processWet (float*, float*, int) {}

        // Called with the signal while the block is off, so delay-based effects can keep their
        // lines filled and come in smoothly when switched on.
        virtual void idle (const float*, const float*, int) {}

        // Send effects: add the effect's output to the buffer, feeding it input * inputGain[i].
        virtual bool isSend() const { return false; }
        virtual void processSend (float*, float*, const float* /*inputGain*/, int) {}

        double sampleRate = 44100.0;

    private:
        bool enabled = false;
        float fade = 0.0f, fadeStep = 0.001f;
        std::vector<float> dryL, dryR, ramp;
    };

    // Effect 1: tremolo, auto-pan, ring modulator, wah (rate = pedal position), auto-wah.
    class ModEffect1 final : public FxBlock
    {
    public:
        enum class Type { tremolo, pan, ring, wah, autoWah };
        void set (Type t, float rate, float amount) { type = t; rateValue = rate; amountValue = amount; }
        void reset() override;

    protected:
        void prepare (double, int) override { reset(); }
        void processWet (float* left, float* right, int numSamples) override;

    private:
        Type type = Type::tremolo;
        float rateValue = 0.5f, amountValue = 0.5f;
        float phase = 0.0f, carrierPhase = 0.0f, envelope = 0.0f;
        dsp::Svf wahL, wahR;
        int coeffCounter = 0;
    };

    // Effect 2: phaser, flanger, chorus and a Uni-Vibe-style throb.
    class ModEffect2 final : public FxBlock
    {
    public:
        enum class Type { phaser, flanger, chorus, vibe };
        void set (Type t, float rate, float amount) { type = t; rateValue = rate; amountValue = amount; }
        void reset() override;

    protected:
        void prepare (double sampleRate, int maxBlockSize) override;
        void processWet (float* left, float* right, int numSamples) override;
        void idle (const float* left, const float* right, int numSamples) override;

    private:
        static constexpr int maxStages = 6;
        struct AllpassChain
        {
            std::array<float, maxStages> state {};
            float feedback = 0.0f;
        };

        float allpassStage (float x, float coeff, float& state) const
        {
            const float y = coeff * x + state;
            state = x - coeff * y;
            return y;
        }

        Type type = Type::phaser;
        float rateValue = 0.35f, amountValue = 0.5f;
        float phase = 0.0f;
        AllpassChain chainL, chainR;
        dsp::DelayLine delayL, delayR;
        float flangeFbL = 0.0f, flangeFbR = 0.0f;
    };

    // Amp simulator (Twin, Small, JC; 4x oversampled drive) and a 3-band EQ with sweepable mid.
    class AmpEq final : public FxBlock
    {
    public:
        enum class Type { eqOnly, twin, small, jc };
        void set (Type t, float drive, float bassDb, float midDb, float midHz, float trebleDb);
        void reset() override;

    protected:
        void prepare (double sampleRate, int maxBlockSize) override;
        void processWet (float* left, float* right, int numSamples) override;

    private:
        void design();

        Type type = Type::eqOnly;
        float driveValue = 0.2f, bass = 0.0f, mid = 0.0f, midFreq = 1000.0f, treble = 0.0f;
        bool dirty = true;
        std::array<dsp::Biquad, 2> eqLow, eqMid, eqHigh, cabHigh, cabLow, voicing;
        std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
        int maxBlock = 512;
    };

    // Stereo delay: tempo-synced or free time, ping-pong, and an analog-style darkening feedback.
    class StereoDelay final : public FxBlock
    {
    public:
        static constexpr double maxSeconds = 3.2; // a synced 1/2 note at 40 bpm is 3 s
        void set (float seconds, float feedback, float mix, bool pingPong, float tone);
        bool hasTail() const override { return quietSamples < static_cast<int> (2.0f * currentDelay) + 64; }
        void reset() override;

    protected:
        void prepare (double sampleRate, int maxBlockSize) override;
        bool isSend() const override { return true; }
        void processSend (float* left, float* right, const float* inputGain, int numSamples) override;

    private:
        void updateTone();

        dsp::DelayLine lineL, lineR;
        float seconds = 0.375f, toneValue = 0.4f;
        float targetDelay = 1000.0f, currentDelay = 1000.0f;
        float feedbackValue = 0.35f, mixValue = 0.3f;
        bool pingPong = true;
        dsp::OnePole toneL, toneR;
        dsp::Biquad lowCutL, lowCutR;
        int quietSamples = 1 << 30;
    };

    // Stereo-linked compressor; more "amount" = lower threshold, higher ratio, more make-up gain.
    class Compressor final : public FxBlock
    {
    public:
        void set (float amount, bool fast) { amountValue = amount; fastMode = fast; }
        void reset() override { envelope = 0.0f; }

    protected:
        void prepare (double, int) override { reset(); }
        void processWet (float* left, float* right, int numSamples) override;

    private:
        float amountValue = 0.4f;
        bool fastMode = false;
        float envelope = 0.0f;
    };

    // Algorithmic reverb: pre-delay, input diffusion, and an 8-line feedback delay network with
    // damping and gentle modulation. Room, Stage and Hall sizes, dark or bright.
    class Reverb final : public FxBlock
    {
    public:
        enum class Type { room, stage, hall };
        void set (Type t, float amount, bool bright);
        bool hasTail() const override { return quietSamples < holdSamples; }
        void reset() override;

    protected:
        void prepare (double sampleRate, int maxBlockSize) override;
        bool isSend() const override { return true; }
        void processSend (float* left, float* right, const float* inputGain, int numSamples) override;

    private:
        static constexpr int numLines = 8;
        void configure();

        Type type = Type::stage;
        float amountValue = 0.25f;
        bool brightValue = false, dirty = true;

        dsp::DelayLine preDelay;
        std::array<dsp::DelayLine, 4> diffusers;
        std::array<float, 4> diffuserLength {};
        std::array<dsp::DelayLine, numLines> lines;
        std::array<float, numLines> lineLength {}, lineGain {};
        std::array<dsp::OnePole, numLines> damping;
        std::array<float, numLines> feedback {};
        float preDelaySamples = 0.0f, modPhase = 0.0f, wetGain = 0.5f;
        int quietSamples = 1 << 30, holdSamples = 4096;
    };

    // Master brickwall-ish limiter: fast attack, smooth release, then a soft ceiling.
    class Limiter
    {
    public:
        void prepare (double sampleRate);
        void process (float* left, float* right, int numSamples);
        float gainReduction() const { return 1.0f - gain; }

    private:
        float gain = 1.0f, attack = 0.5f, release = 0.001f;
    };
}
