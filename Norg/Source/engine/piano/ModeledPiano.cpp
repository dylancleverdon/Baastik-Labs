#include "ModeledPiano.h"

namespace norg::piano
{
    void ModeledPiano::prepare (double newSampleRate, int maxBlockSize)
    {
        sampleRate = newSampleRate;
        scratchL.assign (static_cast<size_t> (juce::jmax (1, maxBlockSize)), 0.0f);
        scratchR.assign (static_cast<size_t> (juce::jmax (1, maxBlockSize)), 0.0f);
        configureBody();
        reset();
    }

    void ModeledPiano::reset()
    {
        for (auto& v : voices)
            v.note = -1;
        for (auto& b : bodyL) b.reset();
        for (auto& b : bodyR) b.reset();
        pedal = 0.0f;
    }

    void ModeledPiano::configureBody()
    {
        // The soundboard and case: a gentle resonance shape, fuller and boxier for the upright.
        const auto sr = sampleRate;
        for (auto* body : { &bodyL, &bodyR })
        {
            if (bodyVariant == Variant::grand)
            {
                (*body)[0].setLowShelf (120.0f, 2.0f, sr);
                (*body)[1].setPeak (520.0f, 0.8f, 1.5f, sr);
                (*body)[2].setHighShelf (6500.0f, -2.5f, sr);
            }
            else
            {
                (*body)[0].setPeak (240.0f, 1.0f, 3.5f, sr);
                (*body)[1].setPeak (1250.0f, 1.2f, 3.0f, sr);
                (*body)[2].setHighShelf (5500.0f, -1.5f, sr);
            }
        }
    }

    void ModeledPiano::updateDamping (Voice& v)
    {
        const float d = isUndamped (v.note) ? 0.0f : damperAmount (v.held, pedal);
        const auto sr = static_cast<float> (sampleRate);
        for (int i = 0; i < v.numPartials; ++i)
            v.partials[static_cast<size_t> (i)].setDamping (d, sr);
    }

    void ModeledPiano::noteOn (int note, float velocity)
    {
        if (variant != bodyVariant)
        {
            bodyVariant = variant;
            configureBody();
        }

        // Re-striking a note: let at most one older voice of it keep ringing.
        Voice* sameNote = nullptr;
        int sameCount = 0;
        for (auto& v : voices)
            if (v.note == note)
            {
                ++sameCount;
                if (sameNote == nullptr || v.age < sameNote->age)
                    sameNote = &v;
            }

        Voice* chosen = sameCount >= 2 ? sameNote : nullptr;
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
        const bool upright = variant == Variant::upright;
        const auto sr = static_cast<float> (sampleRate);
        const float nyquist = 0.45f * sr;
        const float vel = juce::jlimit (0.02f, 1.0f, velocity);

        v.note = note;
        v.held = true;
        v.variant = variant;
        v.age = ++counter;

        const float cents = settings.stretchTuning ? stretchCents (note) : 0.0f;
        const float f0 = dsp::noteHz (static_cast<float> (note) + cents / 100.0f);

        // String stiffness makes partials run sharp: f_k = k f0 sqrt(1 + B k^2).
        const float B = (upright ? 1.8f : 1.0f) * 0.00015f * std::pow (2.0f, (note - 21) / 24.0f);
        const float T = (upright ? 0.7f : 1.0f) * juce::jlimit (0.25f, 16.0f, 14.0f * std::pow (2.0f, -(note - 21) / 17.0f));
        const float damperTau = settings.softRelease ? 0.35f : 0.09f;

        // Hammer about 1/8 of the way along the string; harder blows reach higher partials.
        const float hammerPos = upright ? 0.12f : 0.135f;
        const float rolloff = (upright ? 1.7f : 1.9f) - 0.95f * vel;
        const float loudness = (upright ? 0.22f : 0.26f) * (0.12f + std::pow (vel, 1.4f));

        const int strings = note >= 40 ? 3 : note >= 28 ? 2 : 1;
        const float detuneCents[3] = { 0.0f, upright ? 1.5f : 0.7f, upright ? -1.2f : -0.5f };
        const float stringTau[3] = { 1.0f, 2.3f, 0.8f };   // unequal decays give the two-stage decay
        const float stringAmp[3] = { 1.0f, 0.55f, 0.8f };

        const int perString = juce::jlimit (4, maxPartials / strings, static_cast<int> (nyquist / f0));
        v.numPartials = 0;

        for (int s = 0; s < strings; ++s)
        {
            const float fs = f0 * std::pow (2.0f, detuneCents[s] / 1200.0f);
            for (int k = 1; k <= perString && v.numPartials < maxPartials; ++k)
            {
                const float fk = static_cast<float> (k) * fs * std::sqrt (1.0f + B * static_cast<float> (k * k));
                if (fk >= nyquist)
                    break;

                const float shape = std::abs (std::sin (juce::MathConstants<float>::pi * static_cast<float> (k) * hammerPos));
                const float amp = loudness * stringAmp[s] * shape / std::pow (static_cast<float> (k), rolloff);
                const float tau = T * stringTau[s] / (1.0f + 0.012f * static_cast<float> (k * k));
                v.partials[static_cast<size_t> (v.numPartials++)].start (fk, amp, noise.uniform01() * dsp::twoPi, tau, damperTau, sr);
            }
        }

        // Spread across the stereo field like sitting at the keyboard.
        const float pan = juce::jlimit (-1.0f, 1.0f, (static_cast<float> (note) - 64.0f) / 48.0f) * 0.7f;
        v.panL = equalPowerLeft (pan);
        v.panR = equalPowerRight (pan);

        v.thump = 0.05f * vel;
        v.thumpFilter.setCutoff (upright ? 700.0f : 450.0f, sampleRate);
        updateDamping (v);
    }

    void ModeledPiano::noteOff (int note)
    {
        for (auto& v : voices)
            if (v.note == note && v.held)
            {
                v.held = false;
                updateDamping (v);
            }
    }

    void ModeledPiano::setPedal (float amount)
    {
        if (std::abs (amount - pedal) < 1.0e-3f)
            return;
        pedal = amount;
        for (auto& v : voices)
            if (v.note >= 0 && ! v.held)
                updateDamping (v);
    }

    void ModeledPiano::allNotesOff()
    {
        for (auto& v : voices)
            if (v.note >= 0)
            {
                v.held = false;
                updateDamping (v);
            }
    }

    bool ModeledPiano::isActive() const
    {
        for (const auto& v : voices)
            if (v.note >= 0)
                return true;
        return false;
    }

    void ModeledPiano::render (float* left, float* right, int n)
    {
        auto* l = scratchL.data();
        auto* r = scratchR.data();
        std::fill (l, l + n, 0.0f);
        std::fill (r, r + n, 0.0f);
        const float thumpDecay = std::exp (-1.0f / (0.012f * static_cast<float> (sampleRate)));
        bool any = false;

        for (auto& v : voices)
        {
            if (v.note < 0)
                continue;
            any = true;

            float energy = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                float s = 0.0f;
                for (int k = 0; k < v.numPartials; ++k)
                    s += v.partials[static_cast<size_t> (k)].tick();

                if (v.thump > 1.0e-5f)
                {
                    s += v.thumpFilter.process (noise.next()) * v.thump;
                    v.thump *= thumpDecay;
                }

                l[i] += s * v.panL;
                r[i] += s * v.panR;
            }

            for (int k = 0; k < v.numPartials; ++k)
                energy += v.partials[static_cast<size_t> (k)].energy();
            if (energy < 1.0e-10f && v.thump < 1.0e-5f)
                v.note = -1;
        }

        if (! any)
            return;

        for (int i = 0; i < n; ++i)
        {
            float a = l[i], b = r[i];
            for (auto& f : bodyL) a = f.process (a);
            for (auto& f : bodyR) b = f.process (b);
            left[i] += a;
            right[i] += b;
        }
    }
}
