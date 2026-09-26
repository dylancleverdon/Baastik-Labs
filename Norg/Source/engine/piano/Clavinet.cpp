#include "Clavinet.h"

namespace norg::piano
{
    namespace
    {
        constexpr float neckPosition = 0.23f;   // fraction of the string length
        constexpr float bridgePosition = 0.07f;

        float clavDecay (int note) { return juce::jlimit (0.35f, 4.0f, 2.4f * std::pow (2.0f, -(note - 40) / 24.0f)); }
    }

    void Clavinet::prepare (double newSampleRate, int maxBlockSize)
    {
        sampleRate = newSampleRate;
        lineSize = juce::nextPowerOfTwo (static_cast<int> (sampleRate / 25.0) + 8);
        for (auto& v : voices)
            v.line.assign (static_cast<size_t> (lineSize), 0.0f);
        scratch.assign (static_cast<size_t> (juce::jmax (1, maxBlockSize)), 0.0f);
        thumpFilter.setCutoff (700.0f, sampleRate);
        configureFilters();
        reset();
    }

    void Clavinet::reset()
    {
        for (auto& v : voices)
        {
            std::fill (v.line.begin(), v.line.end(), 0.0f);
            v.note = -1;
            v.held = false;
            v.thump = 0.0f;
            v.lpState = 0.0f;
        }
        for (auto& b : tone)
            b.reset();
        pedal = 0.0f;
    }

    void Clavinet::setSettings (const PianoSettings& s)
    {
        const bool changed = s.clavFilter != settings.clavFilter || s.timbre != settings.timbre;
        settings = s;
        if (changed)
            configureFilters();
    }

    void Clavinet::configureFilters()
    {
        const auto sr = sampleRate;
        switch (settings.clavFilter)
        {
            case 0: // Soft
                tone[0].setHighPass (60.0f, 0.707f, sr);
                tone[1].setLowPass (1300.0f, 0.8f, sr);
                tone[2].setPeak (400.0f, 0.8f, 2.0f, sr);
                break;
            case 1: // Medium
                tone[0].setHighPass (180.0f, 0.707f, sr);
                tone[1].setLowPass (3200.0f, 0.9f, sr);
                tone[2].setPeak (1200.0f, 1.0f, 3.0f, sr);
                break;
            case 2: // Treble
                tone[0].setHighPass (380.0f, 0.707f, sr);
                tone[1].setLowPass (7500.0f, 0.707f, sr);
                tone[2].setPeak (2500.0f, 1.0f, 2.0f, sr);
                break;
            default: // Brilliant
                tone[0].setHighPass (850.0f, 0.707f, sr);
                tone[1].setLowPass (10000.0f, 0.707f, sr);
                tone[2].setHighShelf (3000.0f, 2.0f, sr);
                break;
        }
    }

    float Clavinet::readLine (const Voice& v, float delay) const
    {
        // `delay` samples behind the newest sample, linearly interpolated.
        const float pos = static_cast<float> (v.write) - delay;
        const float whole = std::floor (pos);
        const float frac = pos - whole;
        const int i0 = static_cast<int> (whole) & (lineSize - 1);
        const int i1 = (i0 + 1) & (lineSize - 1);
        const float a = v.line[static_cast<size_t> (i0)];
        return a + frac * (v.line[static_cast<size_t> (i1)] - a);
    }

    void Clavinet::updateDamping (Voice& v)
    {
        const float d = damperAmount (v.held, pedal);
        v.loopGain = v.naturalGain + d * (v.dampedGain - v.naturalGain);
    }

    void Clavinet::noteOn (int note, float velocity)
    {
        Voice* chosen = nullptr;
        for (auto& v : voices)
            if (v.note == note) { chosen = &v; break; } // restrike the same string
        if (chosen == nullptr)
            for (auto& v : voices)
                if (v.note < 0) { chosen = &v; break; }
        if (chosen == nullptr)
        {
            chosen = &voices[0];
            for (auto& v : voices)
                if (v.age < chosen->age)
                    chosen = &v;
        }

        auto& v = *chosen;
        const float vel = juce::jlimit (0.02f, 1.0f, velocity);
        const float f = dsp::noteHz (static_cast<float> (note));
        const auto sr = static_cast<float> (sampleRate);

        v.note = note;
        v.held = true;
        v.age = ++counter;
        v.period = juce::jlimit (4.0f, static_cast<float> (lineSize - 4), sr / f);
        v.naturalGain = std::exp (-1.0f / (clavDecay (note) * f));
        v.dampedGain = std::exp (-1.0f / (0.02f * f));
        v.brightness = juce::jlimit (0.15f, 0.95f, 0.45f + 0.45f * vel + (note - 60) * 0.004f);
        v.lpState = 0.0f;
        v.quiet = 1.0f;
        updateDamping (v);

        // The tangent's strike: a short, bright burst whose brightness follows the velocity.
        dsp::OnePole shaper;
        shaper.setCutoff (1200.0f + 9000.0f * vel * vel, sampleRate);
        const int len = static_cast<int> (v.period);
        float mean = 0.0f;
        std::fill (v.line.begin(), v.line.end(), 0.0f);
        for (int i = 0; i < len; ++i)
        {
            const float burst = shaper.process (noise.next());
            v.line[static_cast<size_t> (i)] = burst;
            mean += burst;
        }
        mean /= static_cast<float> (juce::jmax (1, len));
        for (int i = 0; i < len; ++i)
            v.line[static_cast<size_t> (i)] = (v.line[static_cast<size_t> (i)] - mean) * (0.6f * vel + 0.08f);
        v.write = len - 1;
        v.thump = 0.0f;
    }

    void Clavinet::noteOff (int note)
    {
        for (auto& v : voices)
            if (v.note == note && v.held)
            {
                v.held = false;
                updateDamping (v);
                if (pedal < 0.5f)
                    v.thump = 0.05f; // the yarn damper's "chunk"
            }
    }

    void Clavinet::setPedal (float amount)
    {
        pedal = amount;
        for (auto& v : voices)
            if (v.note >= 0)
                updateDamping (v);
    }

    void Clavinet::allNotesOff()
    {
        for (auto& v : voices)
            if (v.note >= 0)
            {
                v.held = false;
                updateDamping (v);
            }
    }

    bool Clavinet::isActive() const
    {
        for (const auto& v : voices)
            if (v.note >= 0)
                return true;
        return false;
    }

    void Clavinet::render (float* left, float* right, int n)
    {
        auto* mono = scratch.data();
        std::fill (mono, mono + n, 0.0f);
        const int pickupMode = settings.clavPickup;
        const float thumpDecay = std::exp (-1.0f / (0.008f * static_cast<float> (sampleRate)));
        bool any = false;

        for (auto& v : voices)
        {
            if (v.note < 0)
                continue;
            any = true;

            // Each sample passes the loss filter once per trip around the loop. The read point is
            // pulled in by one sample (the write step) and by the low-pass's own delay, to stay in tune.
            const float gain = v.loopGain;
            const float loopDelay = juce::jmax (1.0f, v.period - 1.0f - (1.0f - v.brightness) / v.brightness);
            const float neckDelay = neckPosition * v.period;
            const float bridgeDelay = bridgePosition * v.period;
            float level = 0.0f;

            for (int i = 0; i < n; ++i)
            {
                // Waveguide loop: delayed sample, low-passed (string losses), back into the line.
                const float delayed = readLine (v, loopDelay);
                v.lpState += v.brightness * (delayed - v.lpState);
                const float y = v.lpState * gain;
                v.write = (v.write + 1) & (lineSize - 1);
                v.line[static_cast<size_t> (v.write)] = y;

                // Pickups see the string at their position: a comb on the travelling wave.
                const float neck = y - readLine (v, neckDelay);
                const float bridge = y - readLine (v, bridgeDelay);
                float out = pickupMode == 0 ? neck
                          : pickupMode == 1 ? 0.95f * bridge
                          : pickupMode == 2 ? 0.6f * (neck + bridge)
                                            : 0.8f * (neck - bridge);

                if (v.thump > 1.0e-5f)
                {
                    out += thumpFilter.process (noise.next()) * v.thump;
                    v.thump *= thumpDecay;
                }

                mono[i] += 0.9f * out;
                level = juce::jmax (level, std::abs (y));
            }

            v.quiet = level;
            if (! v.held && level < 1.0e-5f && v.thump < 1.0e-5f)
                v.note = -1;
        }

        if (! any)
            return;

        for (int i = 0; i < n; ++i)
        {
            float s = mono[i];
            for (auto& b : tone)
                s = b.process (s);
            left[i] += s;
            right[i] += s;
        }
    }
}
