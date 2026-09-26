#include "Effects.h"

namespace norg::fx
{
    namespace
    {
        float expRange (float lo, float hi, float t) { return lo * std::pow (hi / lo, juce::jlimit (0.0f, 1.0f, t)); }

        float wrap (float phase) { return phase >= 1.0f ? phase - 1.0f : phase; }

        float allpassCoeff (float hz, double sampleRate)
        {
            const float t = std::tan (juce::MathConstants<float>::pi
                                      * juce::jlimit (10.0f, 0.45f * static_cast<float> (sampleRate), hz)
                                      / static_cast<float> (sampleRate));
            return (t - 1.0f) / (t + 1.0f);
        }
    }

    //==============================================================================
    void FxBlock::prepareBlock (double newSampleRate, int maxBlockSize)
    {
        sampleRate = newSampleRate;
        const auto size = static_cast<size_t> (juce::jmax (1, maxBlockSize));
        dryL.assign (size, 0.0f);
        dryR.assign (size, 0.0f);
        ramp.assign (size, 0.0f);
        fadeStep = static_cast<float> (1.0 / (0.02 * sampleRate));
        fade = enabled ? 1.0f : 0.0f;
        prepare (sampleRate, maxBlockSize);
    }

    void FxBlock::process (float* left, float* right, int n)
    {
        if (! isRunning())
        {
            idle (left, right, n);
            return;
        }

        jassert (n <= static_cast<int> (ramp.size()));
        const float target = enabled ? 1.0f : 0.0f;

        if (isSend())
        {
            for (int i = 0; i < n; ++i)
            {
                fade += juce::jlimit (-fadeStep, fadeStep, target - fade);
                ramp[static_cast<size_t> (i)] = fade;
            }
            processSend (left, right, ramp.data(), n);
            return;
        }

        const bool steady = std::abs (fade - target) <= 0.0f;
        if (steady && enabled)
        {
            processWet (left, right, n);
            return;
        }

        std::copy (left, left + n, dryL.begin());
        std::copy (right, right + n, dryR.begin());
        processWet (left, right, n);

        for (int i = 0; i < n; ++i)
        {
            fade += juce::jlimit (-fadeStep, fadeStep, target - fade);
            const auto k = static_cast<size_t> (i);
            left[i] = dryL[k] + fade * (left[i] - dryL[k]);
            right[i] = dryR[k] + fade * (right[i] - dryR[k]);
        }

        if (fade <= 0.0f)
            reset();
    }

    //==============================================================================
    void ModEffect1::reset()
    {
        phase = carrierPhase = envelope = 0.0f;
        wahL.reset();
        wahR.reset();
        coeffCounter = 0;
    }

    void ModEffect1::processWet (float* left, float* right, int n)
    {
        const float sr = static_cast<float> (sampleRate);
        const float amount = juce::jlimit (0.0f, 1.0f, amountValue);

        switch (type)
        {
            case Type::tremolo:
            case Type::pan:
            {
                const float inc = expRange (0.4f, 12.0f, rateValue) / sr;
                for (int i = 0; i < n; ++i)
                {
                    phase = wrap (phase + inc);
                    const float lfo = std::sin (dsp::twoPi * phase);

                    if (type == Type::tremolo)
                    {
                        // A rounded, slightly peaky shape like an optical tremolo.
                        const float g = 1.0f - amount * (0.5f - 0.5f * lfo) * (0.85f + 0.15f * lfo);
                        left[i] *= g;
                        right[i] *= g;
                    }
                    else
                    {
                        const float angle = juce::MathConstants<float>::pi * 0.25f * (1.0f + amount * lfo);
                        left[i] *= juce::MathConstants<float>::sqrt2 * std::cos (angle);
                        right[i] *= juce::MathConstants<float>::sqrt2 * std::sin (angle);
                    }
                }
                break;
            }

            case Type::ring:
            {
                const float inc = expRange (30.0f, 2400.0f, rateValue) / sr;
                for (int i = 0; i < n; ++i)
                {
                    carrierPhase = wrap (carrierPhase + inc);
                    const float c = std::sin (dsp::twoPi * carrierPhase);
                    const float g = 1.0f - amount + amount * c;
                    left[i] *= g;
                    right[i] *= g;
                }
                break;
            }

            case Type::wah:
            case Type::autoWah:
            {
                const float attack = 1.0f - std::exp (-1.0f / (0.003f * sr));
                const float release = 1.0f - std::exp (-1.0f / (0.15f * sr));
                const float q = 3.5f;
                const float boost = 3.0f / q; // band output * k, lifted ~9.5 dB at the peak

                for (int i = 0; i < n; ++i)
                {
                    if (type == Type::autoWah)
                    {
                        const float level = 0.5f * (std::abs (left[i]) + std::abs (right[i]));
                        envelope += (level > envelope ? attack : release) * (level - envelope);
                    }

                    if (coeffCounter-- <= 0)
                    {
                        coeffCounter = 16;
                        // Wah: rate is the pedal position. Auto-wah: rate is the sensitivity.
                        const float position = type == Type::wah
                                                 ? rateValue
                                                 : 1.0f - std::exp (-envelope * (4.0f + 40.0f * rateValue));
                        const float hz = expRange (320.0f, 2400.0f, position);
                        wahL.set (hz, q, sampleRate);
                        wahR.set (hz, q, sampleRate);
                    }

                    wahL.process (left[i]);
                    wahR.process (right[i]);
                    left[i] += amount * (boost * wahL.band - left[i]);
                    right[i] += amount * (boost * wahR.band - right[i]);
                }
                break;
            }
        }
    }

    //==============================================================================
    void ModEffect2::prepare (double sr, int)
    {
        delayL.prepare (static_cast<int> (0.03 * sr));
        delayR.prepare (static_cast<int> (0.03 * sr));
        delayL.reset();
        delayR.reset();
        reset();
    }

    void ModEffect2::idle (const float* left, const float* right, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            delayL.push (left[i]);
            delayR.push (right[i]);
        }
    }

    void ModEffect2::reset()
    {
        phase = 0.0f;
        chainL = {};
        chainR = {};
        flangeFbL = flangeFbR = 0.0f;
    }

    void ModEffect2::processWet (float* left, float* right, int n)
    {
        const float sr = static_cast<float> (sampleRate);
        const float amount = juce::jlimit (0.0f, 1.0f, amountValue);

        switch (type)
        {
            case Type::phaser:
            {
                // Four first-order all-passes swept together, with feedback; summed with the dry
                // signal they carve two moving notches.
                const float inc = expRange (0.05f, 6.0f, rateValue) / sr;
                const float depth = 0.35f + 0.65f * amount;
                const float fb = 0.15f + 0.5f * amount;
                for (int i = 0; i < n; ++i)
                {
                    phase = wrap (phase + inc);
                    const auto run = [&] (float x, AllpassChain& chain, float lfo)
                    {
                        const float a = allpassCoeff (expRange (160.0f, 2600.0f, 0.5f + 0.5f * depth * lfo), sampleRate);
                        float y = x + fb * chain.feedback;
                        for (int s = 0; s < 4; ++s)
                            y = allpassStage (y, a, chain.state[static_cast<size_t> (s)]);
                        chain.feedback = y;
                        return 0.5f * (x + y);
                    };
                    left[i] = run (left[i], chainL, std::sin (dsp::twoPi * phase));
                    right[i] = run (right[i], chainR, std::sin (dsp::twoPi * wrap (phase + 0.25f)));
                }
                break;
            }

            case Type::flanger:
            case Type::chorus:
            {
                const bool flanger = type == Type::flanger;
                const float inc = (flanger ? expRange (0.04f, 4.0f, rateValue) : expRange (0.12f, 5.0f, rateValue)) / sr;
                const float base = (flanger ? 0.0032f : 0.0085f) * sr;
                const float depth = (flanger ? 0.0028f : 0.0035f) * sr * (flanger ? (0.4f + 0.6f * amount) : 1.0f);
                const float fb = flanger ? 0.35f + 0.45f * amount : 0.0f;
                const float wet = flanger ? 0.7f : 0.9f * amount;
                const float norm = flanger ? 1.0f / (1.0f + wet * (1.0f + fb)) * 1.6f : 1.0f / (1.0f + 0.35f * wet);

                for (int i = 0; i < n; ++i)
                {
                    phase = wrap (phase + inc);
                    // L and R LFOs a quarter-cycle apart widen the image.
                    const float lfoL = std::sin (dsp::twoPi * phase);
                    const float lfoR = std::sin (dsp::twoPi * wrap (phase + 0.25f));

                    delayL.push (left[i] + fb * flangeFbL);
                    delayR.push (right[i] + fb * flangeFbR);
                    const float dL = delayL.read (base + depth * lfoL);
                    const float dR = delayR.read (base + depth * lfoR);
                    flangeFbL = dL;
                    flangeFbR = dR;

                    left[i] = norm * (left[i] + wet * dL);
                    right[i] = norm * (right[i] + wet * dR);
                }
                break;
            }

            case Type::vibe:
            {
                // Four all-passes with staggered corners, all swept by one lamp-driven photocell:
                // the lamp's slow heat-up makes the LFO lopsided and gives the throb.
                const float inc = expRange (0.4f, 9.0f, rateValue) / sr;
                static constexpr float corners[4] = { 90.0f, 380.0f, 1300.0f, 4200.0f };
                const float depth = 0.3f + 0.7f * amount;
                for (int i = 0; i < n; ++i)
                {
                    phase = wrap (phase + inc);
                    const float s = 0.5f + 0.5f * std::sin (dsp::twoPi * phase);
                    const float lamp = s * s * (3.0f - 2.0f * s);
                    const float sweep = std::pow (5.0f, depth * (lamp - 0.5f));
                    const float mono = 0.5f * (left[i] + right[i]);

                    float y = mono + 0.2f * chainL.feedback;
                    for (int st = 0; st < 4; ++st)
                        y = allpassStage (y, allpassCoeff (corners[st] * sweep, sampleRate), chainL.state[static_cast<size_t> (st)]);
                    chainL.feedback = y;

                    const float throb = 1.0f - 0.12f * amount * lamp;
                    left[i] = throb * 0.5f * (left[i] + y);
                    right[i] = throb * 0.5f * (right[i] + y);
                }
                break;
            }
        }
    }

    //==============================================================================
    void AmpEq::set (Type t, float drive, float bassDb, float midDb, float midHz, float trebleDb)
    {
        const auto differs = [] (float a, float b) { return std::abs (a - b) > 1.0e-6f; };
        if (t != type || differs (bassDb, bass) || differs (midDb, mid) || differs (midHz, midFreq) || differs (trebleDb, treble))
            dirty = true;
        type = t;
        driveValue = juce::jlimit (0.0f, 1.0f, drive);
        bass = bassDb;
        mid = midDb;
        midFreq = midHz;
        treble = trebleDb;
    }

    void AmpEq::prepare (double, int maxBlockSize)
    {
        maxBlock = juce::jmax (1, maxBlockSize);
        oversampling = std::make_unique<juce::dsp::Oversampling<float>> (
            2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
        oversampling->initProcessing (static_cast<size_t> (maxBlock));
        dirty = true;
        reset();
    }

    void AmpEq::reset()
    {
        for (auto* bank : { &eqLow, &eqMid, &eqHigh, &cabHigh, &cabLow, &voicing })
            for (auto& b : *bank)
                b.reset();
        if (oversampling)
            oversampling->reset();
    }

    void AmpEq::design()
    {
        dirty = false;
        for (size_t ch = 0; ch < 2; ++ch)
        {
            eqLow[ch].setLowShelf (110.0f, bass, sampleRate);
            eqMid[ch].setPeak (midFreq, 0.9f, mid, sampleRate);
            eqHigh[ch].setHighShelf (4000.0f, treble, sampleRate);

            switch (type)
            {
                case Type::eqOnly:
                    break;
                case Type::twin: // big clean combo: scooped mids, open top, two 12" speakers
                    voicing[ch].setPeak (480.0f, 0.7f, -5.0f, sampleRate);
                    cabHigh[ch].setHighPass (70.0f, 0.8f, sampleRate);
                    cabLow[ch].setLowPass (5600.0f, 0.9f, sampleRate);
                    break;
                case Type::small: // small tube combo: pushed mids, boxy single 8"
                    voicing[ch].setPeak (1100.0f, 0.8f, 5.0f, sampleRate);
                    cabHigh[ch].setHighPass (150.0f, 1.1f, sampleRate);
                    cabLow[ch].setLowPass (3700.0f, 1.2f, sampleRate);
                    break;
                case Type::jc: // solid-state jazz chorus: flat, wide and clean
                    voicing[ch].setPeak (2500.0f, 0.7f, 1.5f, sampleRate);
                    cabHigh[ch].setHighPass (60.0f, 0.7f, sampleRate);
                    cabLow[ch].setLowPass (7500.0f, 0.7f, sampleRate);
                    break;
            }
        }
    }

    void AmpEq::processWet (float* left, float* right, int n)
    {
        if (dirty)
            design();

        if (type != Type::eqOnly)
        {
            for (int i = 0; i < n; ++i)
            {
                left[i] = voicing[0].process (left[i]);
                right[i] = voicing[1].process (right[i]);
            }

            // Drive stage at 4x: tubes (Twin, Small) clip softly and asymmetrically; the JC stays
            // clean until pushed hard, then clips more abruptly.
            const float d = driveValue;
            const bool tube = type != Type::jc;
            const float gain = tube ? 1.0f + (type == Type::small ? 30.0f : 16.0f) * d * d : 1.0f + 10.0f * d * d * d;
            const float bias = tube ? 0.1f + 0.15f * d : 0.0f;
            const float offset = std::tanh (bias);
            // Tubes compress as they're pushed, so they need less make-up than the clean JC.
            const float makeup = 1.0f / std::pow (gain, tube ? 0.6f : 0.85f);

            float* channels[] = { left, right };
            juce::dsp::AudioBlock<float> block (channels, 2, static_cast<size_t> (n));
            auto up = oversampling->processSamplesUp (block);
            for (size_t ch = 0; ch < 2; ++ch)
            {
                auto* data = up.getChannelPointer (ch);
                for (size_t i = 0; i < up.getNumSamples(); ++i)
                {
                    const float x = gain * data[i];
                    const float y = tube ? std::tanh (x + bias) - offset
                                         : x / std::pow (1.0f + std::pow (std::abs (x), 5.0f), 0.2f);
                    data[i] = y * makeup;
                }
            }
            oversampling->processSamplesDown (block);

            for (int i = 0; i < n; ++i)
            {
                left[i] = cabLow[0].process (cabHigh[0].process (left[i]));
                right[i] = cabLow[1].process (cabHigh[1].process (right[i]));
            }
        }

        for (int i = 0; i < n; ++i)
        {
            left[i] = eqHigh[0].process (eqMid[0].process (eqLow[0].process (left[i])));
            right[i] = eqHigh[1].process (eqMid[1].process (eqLow[1].process (right[i])));
        }
    }

    //==============================================================================
    void StereoDelay::prepare (double sr, int)
    {
        const int maxSamples = static_cast<int> (maxSeconds * sr) + 8;
        lineL.prepare (maxSamples);
        lineR.prepare (maxSamples);
        lowCutL.setHighPass (90.0f, 0.6f, sr);
        lowCutR.setHighPass (90.0f, 0.6f, sr);
        targetDelay = juce::jlimit (1.0f, static_cast<float> (maxSeconds * sr), seconds * static_cast<float> (sr));
        updateTone();
        reset();
    }

    void StereoDelay::reset()
    {
        lineL.reset();
        lineR.reset();
        toneL.reset();
        toneR.reset();
        lowCutL.reset();
        lowCutR.reset();
        currentDelay = targetDelay;
        quietSamples = 1 << 30;
    }

    void StereoDelay::updateTone()
    {
        // "Analog" tone darkens every repeat: 0 = clean digital, 1 = dark tape.
        const float cutoff = expRange (16000.0f, 1400.0f, toneValue);
        toneL.setCutoff (cutoff, sampleRate);
        toneR.setCutoff (cutoff, sampleRate);
    }

    void StereoDelay::set (float newSeconds, float feedback, float mix, bool pingPongMode, float tone)
    {
        seconds = newSeconds;
        targetDelay = juce::jlimit (1.0f, static_cast<float> (maxSeconds * sampleRate), seconds * static_cast<float> (sampleRate));
        if (! hasTail())
            currentDelay = targetDelay; // only glide while repeats are actually sounding
        feedbackValue = juce::jlimit (0.0f, 0.95f, feedback);
        mixValue = juce::jlimit (0.0f, 1.0f, mix);
        pingPong = pingPongMode;

        if (std::abs (tone - toneValue) > 1.0e-6f)
        {
            toneValue = tone;
            updateTone();
        }
    }

    void StereoDelay::processSend (float* left, float* right, const float* inputGain, int n)
    {
        // Time changes glide like a tape delay rather than jumping (and clicking).
        const float glide = 1.0f - std::exp (-1.0f / (0.12f * static_cast<float> (sampleRate)));
        const float wet = mixValue;

        for (int i = 0; i < n; ++i)
        {
            currentDelay += glide * (targetDelay - currentDelay);
            const float readPos = currentDelay - 1.0f;

            const float echoL = lineL.read (readPos);
            const float echoR = lineR.read (readPos);
            const float fbL = dsp::softClip (lowCutL.process (toneL.process (echoL)) * feedbackValue);
            const float fbR = dsp::softClip (lowCutR.process (toneR.process (echoR)) * feedbackValue);

            const float inL = left[i] * inputGain[i];
            const float inR = right[i] * inputGain[i];

            float writeL, writeR;
            if (pingPong)
            {
                // The mono input starts on the left; each repeat crosses to the other side.
                writeL = 0.5f * (inL + inR) + fbR;
                writeR = fbL;
            }
            else
            {
                writeL = inL + fbL;
                writeR = inR + fbR;
            }
            lineL.push (writeL);
            lineR.push (writeR);

            left[i] += wet * echoL;
            right[i] += wet * echoR;

            if (std::abs (writeL) + std::abs (writeR) > 1.0e-5f)
                quietSamples = 0;
            else if (quietSamples < (1 << 30))
                ++quietSamples;
        }
    }

    //==============================================================================
    void Compressor::processWet (float* left, float* right, int n)
    {
        const float sr = static_cast<float> (sampleRate);
        const float amount = juce::jlimit (0.0f, 1.0f, amountValue);
        const float threshold = -8.0f - 24.0f * amount;
        const float ratio = 1.5f + 6.5f * amount;
        const float knee = 8.0f;
        const float makeup = -threshold * (1.0f - 1.0f / ratio) * 0.45f;
        const float attack = 1.0f - std::exp (-1.0f / ((fastMode ? 0.0008f : 0.008f) * sr));
        const float release = 1.0f - std::exp (-1.0f / ((fastMode ? 0.07f : 0.3f) * sr));

        for (int i = 0; i < n; ++i)
        {
            const float level = juce::jmax (std::abs (left[i]), std::abs (right[i]));
            const float levelDb = 20.0f * std::log10 (level + 1.0e-9f);
            const float over = levelDb - threshold;

            float reduction = 0.0f; // dB
            if (over > knee * 0.5f)
                reduction = over * (1.0f - 1.0f / ratio);
            else if (over > -knee * 0.5f)
            {
                const float k = over + knee * 0.5f;
                reduction = (1.0f - 1.0f / ratio) * k * k / (2.0f * knee);
            }

            envelope += (reduction > envelope ? attack : release) * (reduction - envelope);
            const float g = dsp::dbToGain (makeup - envelope);
            left[i] *= g;
            right[i] *= g;
        }
    }

    //==============================================================================
    namespace
    {
        // Line lengths in ms at size 1, mutually prime-ish so the echoes never line up.
        constexpr float lineMs[8] = { 29.7f, 37.1f, 41.1f, 43.7f, 53.3f, 59.9f, 67.7f, 73.9f };
        constexpr float diffuserMs[4] = { 4.77f, 3.59f, 12.73f, 9.3f };
        constexpr float maxSize = 1.4f;
        constexpr float maxPreDelayMs = 40.0f;
    }

    void Reverb::prepare (double sr, int)
    {
        preDelay.prepare (static_cast<int> (maxPreDelayMs * 0.001 * sr) + 4);
        for (size_t i = 0; i < diffusers.size(); ++i)
            diffusers[i].prepare (static_cast<int> (diffuserMs[i] * 0.001 * sr) + 4);
        for (size_t i = 0; i < lines.size(); ++i)
            lines[i].prepare (static_cast<int> ((lineMs[i] * maxSize + 2.0f) * 0.001f * static_cast<float> (sr)) + 4);
        dirty = true;
        reset();
    }

    void Reverb::reset()
    {
        preDelay.reset();
        for (auto& d : diffusers) d.reset();
        for (auto& l : lines) l.reset();
        for (auto& d : damping) d.reset();
        feedback.fill (0.0f);
        quietSamples = 1 << 30;
    }

    void Reverb::set (Type t, float amount, bool bright)
    {
        if (t != type || bright != brightValue)
            dirty = true;
        type = t;
        brightValue = bright;
        amountValue = juce::jlimit (0.0f, 1.0f, amount);
    }

    void Reverb::configure()
    {
        dirty = false;
        const float sr = static_cast<float> (sampleRate);

        float size = 0.8f, rt60 = 1.9f, pre = 12.0f;
        switch (type)
        {
            case Type::room:  size = 0.42f; rt60 = 0.75f; pre = 4.0f;  break;
            case Type::stage: size = 0.8f;  rt60 = 1.9f;  pre = 14.0f; break;
            case Type::hall:  size = 1.35f; rt60 = 3.4f;  pre = 28.0f; break;
        }

        preDelaySamples = pre * 0.001f * sr;
        for (size_t i = 0; i < diffuserLength.size(); ++i)
            diffuserLength[i] = diffuserMs[i] * 0.001f * sr;

        for (size_t i = 0; i < lines.size(); ++i)
        {
            lineLength[i] = lineMs[i] * size * 0.001f * sr;
            lineGain[i] = std::pow (10.0f, -3.0f * lineLength[i] / (rt60 * sr));
            damping[i].setCutoff (brightValue ? 9500.0f : 4200.0f, sampleRate);
        }

        // Bigger spaces have more echo density, so their output needs less gain.
        wetGain = 0.55f / std::sqrt (size);
        holdSamples = static_cast<int> (0.08f * sr);
    }

    void Reverb::processSend (float* left, float* right, const float* inputGain, int n)
    {
        if (dirty)
            configure();

        const float sr = static_cast<float> (sampleRate);
        const float modInc = 0.63f / sr;
        const float modDepth = 0.00035f * sr;
        const float g = 0.62f; // diffuser all-pass gain
        const float wet = wetGain * amountValue;

        for (int i = 0; i < n; ++i)
        {
            preDelay.push (0.5f * (left[i] + right[i]) * inputGain[i]);
            float x = preDelay.read (preDelaySamples);

            // Input diffusion: a chain of Schroeder all-passes smears the attack into a wash.
            for (size_t d = 0; d < diffusers.size(); ++d)
            {
                const float delayed = diffusers[d].read (diffuserLength[d] - 1.0f);
                const float v = x + g * delayed;
                diffusers[d].push (v);
                x = delayed - g * v;
            }

            modPhase = wrap (modPhase + modInc);
            const float mod = std::sin (dsp::twoPi * modPhase);

            // Read every line (two of them slowly modulated, which breaks up metallic ringing).
            std::array<float, numLines> y {};
            for (size_t l = 0; l < lines.size(); ++l)
            {
                float len = lineLength[l] - 1.0f;
                if (l == 2) len += modDepth * mod;
                if (l == 5) len -= modDepth * mod;
                y[l] = damping[l].process (lines[l].read (len)) * lineGain[l];
            }

            // 8x8 Hadamard mix (fast Walsh-Hadamard), energy-preserving.
            auto h = y;
            for (size_t span = 1; span < numLines; span <<= 1)
                for (size_t a = 0; a < numLines; a += span << 1)
                    for (size_t b = a; b < a + span; ++b)
                    {
                        const float p = h[b], q = h[b + span];
                        h[b] = p + q;
                        h[b + span] = p - q;
                    }

            const float norm = 1.0f / std::sqrt (static_cast<float> (numLines));
            float energy = 0.0f;
            for (size_t l = 0; l < lines.size(); ++l)
            {
                const float in = h[l] * norm + ((l & 1) ? -x : x) * 0.5f;
                lines[l].push (in);
                energy += std::abs (in);
            }

            const float outL = y[0] - y[2] + y[4] - y[6];
            const float outR = y[1] - y[3] + y[5] - y[7];
            left[i] += wet * outL;
            right[i] += wet * outR;

            if (energy > 1.0e-5f)
                quietSamples = 0;
            else if (quietSamples < (1 << 30))
                ++quietSamples;
        }
    }

    //==============================================================================
    void Limiter::prepare (double sr)
    {
        release = 1.0f - std::exp (-1.0f / (0.15f * static_cast<float> (sr)));
        gain = 1.0f;
    }

    void Limiter::process (float* left, float* right, int n)
    {
        constexpr float ceiling = 0.95f;
        for (int i = 0; i < n; ++i)
        {
            const float peak = juce::jmax (std::abs (left[i]), right != nullptr ? std::abs (right[i]) : 0.0f);
            const float target = peak > ceiling ? ceiling / peak : 1.0f;
            gain = target < gain ? target : gain + release * (target - gain);

            left[i] = juce::jlimit (-1.0f, 1.0f, left[i] * gain);
            if (right != nullptr)
                right[i] = juce::jlimit (-1.0f, 1.0f, right[i] * gain);
        }
    }
}
