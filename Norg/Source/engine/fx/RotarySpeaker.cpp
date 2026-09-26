#include "RotarySpeaker.h"

namespace norg::fx
{
    namespace
    {
        // Rotor speeds in revolutions per second.
        constexpr float hornSlow = 0.8f, hornFast = 6.7f;
        constexpr float drumSlow = 0.67f, drumFast = 5.8f;

        // Microphones either side of the cabinet.
        constexpr float micAngleL = 1.05f, micAngleR = -1.05f;
    }

    void RotarySpeaker::prepare (double newSampleRate, int maxBlockSize)
    {
        sampleRate = newSampleRate;
        maxBlock = juce::jmax (1, maxBlockSize);

        oversampling = std::make_unique<juce::dsp::Oversampling<float>> (
            1, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
        oversampling->initProcessing (static_cast<size_t> (maxBlock));
        driveBuffer.assign (static_cast<size_t> (maxBlock), 0.0f);

        preampTone.setCutoff (9000.0f, sampleRate);
        postDriveTone.setCutoff (7000.0f, sampleRate * 2.0);
        dcBlock.setHighPass (25.0f, 0.707f, sampleRate);

        drumLow1.setLowPass (800.0f, 0.707f, sampleRate);
        drumLow2.setLowPass (800.0f, 0.707f, sampleRate);
        hornHigh1.setHighPass (800.0f, 0.707f, sampleRate);
        hornHigh2.setHighPass (800.0f, 0.707f, sampleRate);

        hornDelay.prepare (static_cast<int> (0.01 * sampleRate));
        drumDelay.prepare (static_cast<int> (0.01 * sampleRate));
        reset();
    }

    void RotarySpeaker::reset()
    {
        if (oversampling)
            oversampling->reset();
        for (auto* b : { &dcBlock, &drumLow1, &drumLow2, &hornHigh1, &hornHigh2 })
            b->reset();
        preampTone.reset();
        postDriveTone.reset();
        hornToneL.reset();
        hornToneR.reset();
        hornDelay.reset();
        drumDelay.reset();
        hornSpeed = hornTarget;
        drumSpeed = drumTarget;
        onMix = targetOn;
    }

    void RotarySpeaker::setSpeed (bool fast, bool stopMode)
    {
        hornTarget = fast ? hornFast : (stopMode ? 0.0f : hornSlow);
        drumTarget = fast ? drumFast : (stopMode ? 0.0f : drumSlow);
    }

    void RotarySpeaker::applyDrive (float* samples, int n)
    {
        // Tube-style preamp: asymmetric saturation, run at 2x to keep the harmonics clean.
        const float gain = 1.0f + 14.0f * drive * drive * drive + 2.0f * drive;
        const float bias = 0.12f + 0.2f * drive;
        const float offset = std::tanh (bias);
        const float makeup = 1.0f / std::sqrt (gain);

        float* channels[] = { samples };
        juce::dsp::AudioBlock<float> block (channels, 1, static_cast<size_t> (n));
        auto up = oversampling->processSamplesUp (block);
        auto* data = up.getChannelPointer (0);
        for (size_t i = 0; i < up.getNumSamples(); ++i)
        {
            const float y = std::tanh (gain * data[i] + bias) - offset;
            data[i] = postDriveTone.process (y) * makeup;
        }
        oversampling->processSamplesDown (block);
    }

    void RotarySpeaker::process (const float* in, float* outL, float* outR, int n)
    {
        jassert (n <= maxBlock);
        auto* driven = driveBuffer.data();
        for (int i = 0; i < n; ++i)
            driven[i] = preampTone.process (in[i]);
        applyDrive (driven, n);

        const float sr = static_cast<float> (sampleRate);
        const float fadeStep = 1.0f / (0.03f * sr);

        // Heavier drum spins up and down more slowly than the horn.
        const auto inertia = [sr] (float seconds) { return 1.0f - std::exp (-1.0f / (seconds * sr)); };
        const float hornCoeff = inertia (hornTarget > hornSpeed ? 0.35f : 0.75f);
        const float drumCoeff = inertia (drumTarget > drumSpeed ? 1.9f : 2.4f);

        const float hornDepth = 0.00032f * sr; // horn mouth radius ~11 cm
        const float drumDepth = 0.00012f * sr;
        const float hornBase = 0.0012f * sr;
        const float drumBase = 0.0010f * sr;
        const float reflectionOffset = 0.0013f * sr;

        for (int i = 0; i < n; ++i)
        {
            const float x = dcBlock.process (driven[i]);

            hornSpeed += hornCoeff * (hornTarget - hornSpeed);
            drumSpeed += drumCoeff * (drumTarget - drumSpeed);
            hornAngle += dsp::twoPi * hornSpeed / sr;
            drumAngle -= dsp::twoPi * drumSpeed / sr; // the drum turns the other way
            if (hornAngle > dsp::twoPi) hornAngle -= dsp::twoPi;
            if (drumAngle < 0.0f) drumAngle += dsp::twoPi;

            const float low = drumLow2.process (drumLow1.process (x));
            const float high = hornHigh2.process (hornHigh1.process (x));
            hornDelay.push (high);
            drumDelay.push (low);

            // Horn: Doppler from the moving mouth, louder and brighter when facing a mic,
            // plus a reflection off the cabinet's back wall.
            const auto hornMic = [&] (float micAngle, dsp::OnePole& tone)
            {
                const float rel = hornAngle - micAngle;
                const float facing = 0.5f + 0.5f * std::cos (rel);
                const float direct = hornDelay.read (hornBase + hornDepth * std::sin (rel));
                const float reflected = hornDelay.read (hornBase + reflectionOffset - hornDepth * std::sin (rel));
                tone.a = 0.25f + 0.6f * facing;
                return tone.process ((0.35f + 0.65f * facing) * direct + 0.3f * (1.0f - facing) * reflected);
            };

            const auto drumMic = [&] (float micAngle)
            {
                const float rel = drumAngle - micAngle;
                const float facing = 0.5f + 0.5f * std::cos (rel);
                return (0.7f + 0.3f * facing) * drumDelay.read (drumBase + drumDepth * std::sin (rel));
            };

            const float wetL = 0.95f * hornMic (micAngleL, hornToneL) + drumMic (micAngleL);
            const float wetR = 0.95f * hornMic (micAngleR, hornToneR) + drumMic (micAngleR);

            onMix += (targetOn > onMix ? 1.0f : -1.0f) * juce::jmin (fadeStep, std::abs (targetOn - onMix));
            const float dry = in[i];
            outL[i] = onMix * wetL + (1.0f - onMix) * dry;
            outR[i] = onMix * wetR + (1.0f - onMix) * dry;
        }
    }
}
