#include "TonewheelOrgan.h"

namespace norg::organ
{
    namespace
    {
        // Driving/driven gear ratios for C, C#, D ... B. The motor turns the shaft at 20 rev/s.
        constexpr std::array<double, 12> gearRatios { 85.0 / 104.0, 71.0 / 82.0, 67.0 / 73.0, 105.0 / 108.0,
                                                      103.0 / 100.0, 84.0 / 77.0, 74.0 / 64.0, 98.0 / 80.0,
                                                      96.0 / 74.0, 88.0 / 64.0, 67.0 / 46.0, 108.0 / 70.0 };

        // Drawbar footages as semitones from the 8' pitch: 16, 5 1/3, 8, 4, 2 2/3, 2, 1 3/5, 1 1/3, 1.
        constexpr std::array<int, 9> footage { -12, 7, 0, 12, 19, 24, 28, 31, 36 };

        struct PedalPartial { int semitones; float gain; };
        constexpr std::array<PedalPartial, 3> pedal16 { { { -12, 1.0f }, { 0, 0.25f }, { 7, 0.12f } } };
        constexpr std::array<PedalPartial, 3> pedal8  { { { 0, 1.0f }, { 12, 0.3f }, { 19, 0.12f } } };

        int foldIntoRange (int wheel)
        {
            while (wheel < 1) wheel += 12;
            while (wheel > TonewheelOrgan::numWheels) wheel -= 12;
            return wheel;
        }
    }

    double TonewheelOrgan::wheelFrequency (int wheel)
    {
        wheel = juce::jlimit (1, numWheels, wheel);
        if (wheel > 84) // the top seven wheels run an octave above wheels 73-79
            return 2.0 * wheelFrequency (wheel - 12);

        const int octave = (wheel - 1) / 12; // 2, 4, 8 ... 128 teeth
        const int teeth = 2 << octave;
        return 20.0 * gearRatios[static_cast<size_t> ((wheel - 1) % 12)] * teeth;
    }

    int TonewheelOrgan::wheelFor (int midiNote, int drawbar)
    {
        // The 8' drawbar of MIDI note 36 (C2) is wheel 13.
        return foldIntoRange (midiNote - 23 + footage[static_cast<size_t> (juce::jlimit (0, 8, drawbar))]);
    }

    void TonewheelOrgan::prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;

        sine.fill ([] (float x) { return std::sin (x); });
        warm.fill ([] (float x) { return 0.96f * std::sin (x) + 0.045f * std::sin (2.0f * x) + 0.03f * std::sin (3.0f * x); });

        dsp::Noise phaseSeed { 0xabcdef1u };
        for (int w = 1; w <= numWheels; ++w)
        {
            increment[static_cast<size_t> (w)] = wheelFrequency (w) / sampleRate;
            phase[static_cast<size_t> (w)] = phaseSeed.uniform01(); // wheels never start in step
        }

        clickFilter.set (2600.0f, 0.9f, sampleRate);
        setTonewheelMode (0);
        reset();
    }

    void TonewheelOrgan::reset()
    {
        for (auto& bus : current) bus.fill (0.0f);
        for (auto& bus : target) bus.fill (0.0f);
        for (auto& k : keys) k = {};
        numActiveKeys = 0;
        percEnv = 0.0f;
        clickEnv = 0.0f;
        clickEvents = 0;
        clickFilter.reset();
    }

    void TonewheelOrgan::setDrawbars (int bus, const std::array<float, 9>& gains)
    {
        drawbars[static_cast<size_t> (bus)] = gains;
    }

    void TonewheelOrgan::setPercussion (bool on, bool third, bool fast, bool soft)
    {
        percOn = on;
        percThird = third;
        percFast = fast;
        percSoft = soft;
        if (! on)
            percEnv = 0.0f;
    }

    void TonewheelOrgan::setTonewheelMode (int mode)
    {
        leakage = mode == 0 ? 0.012f : mode == 1 ? 0.005f : 0.0f;
        const int warmUpTo = mode == 0 ? 36 : mode == 1 ? 24 : 0;
        for (int w = 1; w <= numWheels; ++w)
            wheelShape[static_cast<size_t> (w)] = w <= warmUpTo ? &warm : &sine;
    }

    bool TonewheelOrgan::anyUpperKeyDown() const
    {
        for (int i = 0; i < numActiveKeys; ++i)
        {
            const auto& k = keys[static_cast<size_t> (activeKeys[static_cast<size_t> (i)])];
            if (k.goal && k.bus == upperBus)
                return true;
        }
        return false;
    }

    void TonewheelOrgan::keyDown (int note, int bus)
    {
        if (note < 0 || note > 127)
            return;

        auto& k = keys[static_cast<size_t> (note)];
        if (k.goal)
            return;

        // Single-trigger percussion: only a key pressed when no other upper key is down restarts it.
        if (bus == upperBus && percOn && ! anyUpperKeyDown())
            percEnv = 1.0f;

        k.goal = true;
        k.bus = bus;

        // Contacts close over roughly 0-2 ms, each at its own moment.
        const float spread = static_cast<float> (0.002 * sampleRate);
        for (auto& d : k.delay)
            d = static_cast<int> (noise.uniform01() * spread);

        if (! k.listed)
        {
            k.listed = true;
            activeKeys[static_cast<size_t> (numActiveKeys++)] = note;
        }
    }

    void TonewheelOrgan::keyUp (int note)
    {
        if (note < 0 || note > 127)
            return;

        auto& k = keys[static_cast<size_t> (note)];
        if (! k.goal)
            return;

        k.goal = false;
        const float spread = static_cast<float> (0.001 * sampleRate);
        for (auto& d : k.delay)
            d = static_cast<int> (noise.uniform01() * spread);
    }

    void TonewheelOrgan::allKeysUp()
    {
        for (int i = 0; i < numActiveKeys; ++i)
            keyUp (activeKeys[static_cast<size_t> (i)]);
    }

    bool TonewheelOrgan::isActive() const
    {
        if (numActiveKeys > 0 || clickEnv > 1.0e-4f)
            return true;

        for (const auto& bus : current)
            for (float a : bus)
                if (a > 0.0f)
                    return true;
        return false;
    }

    void TonewheelOrgan::buildTargets()
    {
        for (auto& bus : target)
            bus.fill (0.0f);

        // The percussion takes over the 1' drawbar, and at normal volume the drawbars drop ~3 dB.
        auto upperBars = drawbars[upperBus];
        if (percOn)
        {
            upperBars[8] = 0.0f;
            if (! percSoft)
                for (auto& g : upperBars)
                    g *= 0.707f;
        }

        const int percFootage = percThird ? 4 : 3;
        const float percLevel = percEnv * (percSoft ? 0.6f : 1.25f);

        for (int i = 0; i < numActiveKeys; ++i)
        {
            const int note = activeKeys[static_cast<size_t> (i)];
            const auto& k = keys[static_cast<size_t> (note)];
            auto& t = target[static_cast<size_t> (k.bus)];

            if (k.bus == pedalBus)
            {
                const auto& bars = drawbars[pedalBus];
                const int base = note - 23;
                if (k.contact[0])
                    for (const auto& p : pedal16)
                        t[static_cast<size_t> (foldIntoRange (base + p.semitones))] += bars[0] * p.gain;
                if (k.contact[1])
                    for (const auto& p : pedal8)
                        t[static_cast<size_t> (foldIntoRange (base + p.semitones))] += bars[1] * p.gain;
                continue;
            }

            const auto& bars = k.bus == upperBus ? upperBars : drawbars[lowerBus];
            for (int d = 0; d < 9; ++d)
                if (k.contact[static_cast<size_t> (d)] && bars[static_cast<size_t> (d)] > 0.0f)
                    t[static_cast<size_t> (wheelFor (note, d))] += bars[static_cast<size_t> (d)];

            // Percussion is keyed through the 1' contact.
            if (k.bus == upperBus && percLevel > 1.0e-4f && k.contact[8])
                t[static_cast<size_t> (wheelFor (note, percFootage))] += percLevel;
        }

        for (auto& t : target)
        {
            // Leakage: a little of each sounding wheel bleeds into its neighbours in the generator.
            if (leakage > 0.0f)
            {
                std::array<float, numWheels + 1> leak {};
                for (int w = 1; w <= numWheels; ++w)
                {
                    const float a = t[static_cast<size_t> (w)];
                    if (a <= 0.0f)
                        continue;
                    for (int n : { w - 24, w + 24, w + 31 })
                        if (n >= 1 && n <= numWheels)
                            leak[static_cast<size_t> (n)] += a * leakage;
                }
                for (int w = 1; w <= numWheels; ++w)
                    t[static_cast<size_t> (w)] += leak[static_cast<size_t> (w)];
            }

            // Loudness robbing: the more that sounds at once, the quieter each part gets.
            float sum = 0.0f;
            for (float a : t)
                sum += a;
            if (sum > 0.0f)
            {
                const float robbing = 1.0f / std::sqrt (1.0f + sum / 12.0f);
                for (auto& a : t)
                    a *= robbing;
            }
        }
    }

    void TonewheelOrgan::renderBus (int bus, float* out, int n)
    {
        auto& cur = current[static_cast<size_t> (bus)];
        const auto& tgt = target[static_cast<size_t> (bus)];
        const float invN = 1.0f / static_cast<float> (n);

        for (int w = 1; w <= numWheels; ++w)
        {
            const auto wi = static_cast<size_t> (w);
            const float from = cur[wi];
            const float to = tgt[wi] < 1.0e-6f ? 0.0f : tgt[wi];
            if (from <= 0.0f && to <= 0.0f)
                continue;

            const float step = (to - from) * invN;
            const auto& shape = *wheelShape[wi];
            double ph = phase[wi];
            const double inc = increment[wi];
            float amp = from;

            for (int i = 0; i < n; ++i)
            {
                amp += step;
                out[i] += amp * shape.read (static_cast<float> (ph));
                ph += inc;
                if (ph >= 1.0)
                    ph -= 1.0;
            }

            cur[wi] = to;
        }
    }

    void TonewheelOrgan::render (float* upper, float* lower, float* pedal, int n)
    {
        jassert (n <= maxChunk);

        // 1. Contacts move towards their keys' state.
        for (int i = 0; i < numActiveKeys;)
        {
            auto& k = keys[static_cast<size_t> (activeKeys[static_cast<size_t> (i)])];
            bool busy = k.goal;

            for (size_t d = 0; d < 9; ++d)
            {
                if (k.delay[d] >= 0)
                {
                    k.delay[d] -= n;
                    if (k.delay[d] < 0)
                    {
                        if (k.contact[d] != k.goal && k.bus != pedalBus)
                            clickEvents += k.goal ? 2 : 1;
                        k.contact[d] = k.goal;
                    }
                }
                busy = busy || k.contact[d] || k.delay[d] >= 0;
            }

            if (busy)
            {
                ++i;
            }
            else
            {
                k.listed = false;
                activeKeys[static_cast<size_t> (i)] = activeKeys[static_cast<size_t> (--numActiveKeys)];
            }
        }

        // 2. Percussion envelope
        if (percEnv > 0.0f)
        {
            const double tau = percFast ? 0.12 : 0.32;
            percEnv *= static_cast<float> (std::exp (-n / (tau * sampleRate)));
            if (percEnv < 1.0e-4f)
                percEnv = 0.0f;
        }

        // 3. Wheel levels for this chunk, then the wheels themselves
        buildTargets();
        renderBus (upperBus, upper, n);
        renderBus (lowerBus, lower, n);
        renderBus (pedalBus, pedal, n);

        // 4. Key click: a short burst of filtered noise whenever contacts make or break.
        if (clickEvents > 0)
        {
            clickEnv = juce::jmin (3.0f, clickEnv + 0.22f * static_cast<float> (clickEvents));
            clickEvents = 0;
        }
        if (clickEnv > 1.0e-4f)
        {
            const float decay = static_cast<float> (std::exp (-1.0 / (0.0025 * sampleRate)));
            const float level = 0.35f * clickAmount * clickAmount;
            for (int i = 0; i < n; ++i)
            {
                clickFilter.process (noise.next());
                upper[i] += level * clickEnv * (clickFilter.band + 0.3f * clickFilter.high);
                clickEnv *= decay;
            }
        }

        // 5. Every wheel keeps turning, whether or not it's heard.
        for (int w = 1; w <= numWheels; ++w)
        {
            auto& ph = phase[static_cast<size_t> (w)];
            ph += increment[static_cast<size_t> (w)] * n;
            ph -= std::floor (ph);
        }
    }
}
