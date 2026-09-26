#include "ElectricPiano.h"

namespace norg::piano
{
    namespace
    {
        // Natural decay time of the fundamental: bass tines ring for ages, treble ones die quickly.
        float tineDecay (int note) { return juce::jlimit (0.6f, 9.0f, 7.0f * std::pow (2.0f, -(note - 40) / 20.0f)); }
        float reedDecay (int note) { return juce::jlimit (0.4f, 6.0f, 3.4f * std::pow (2.0f, -(note - 48) / 22.0f)); }
    }

    void ElectricPiano::prepare (double newSampleRate, int maxBlockSize)
    {
        sampleRate = newSampleRate;
        scratch.assign (static_cast<size_t> (juce::jmax (1, maxBlockSize)), 0.0f);
        configureTone();
        reset();
    }

    void ElectricPiano::reset()
    {
        for (auto& v : voices)
            v = {};
        for (auto& b : tone)
            b.reset();
        pedal = 0.0f;
    }

    void ElectricPiano::setSettings (const PianoSettings& s)
    {
        const bool changed = s.tineModel != settings.tineModel || s.reedModel != settings.reedModel || s.timbre != settings.timbre;
        settings = s;
        if (changed)
            configureTone();
    }

    void ElectricPiano::configureTone()
    {
        const auto sr = sampleRate;
        const int timbre = settings.timbre;

        if (kind == Kind::tine)
        {
            // Mk I: warm with plenty of bark. Mk II: cleaner and brighter. Suitcase: Mk I through
            // the suitcase amp's fuller low end.
            const int model = settings.tineModel;
            pickupOffset = model == 1 ? 0.18f : 0.34f;
            timbreDrive = timbre == 0 ? 0.55f : timbre == 1 ? 1.0f : 1.6f;

            tone[0].setLowShelf (180.0f, model == 2 ? 4.0f : 1.5f, sr);
            tone[1].setLowPass (model == 1 ? 8000.0f : model == 2 ? 4600.0f : 5600.0f, 0.707f, sr);
            if (timbre == 3)      tone[2].setPeak (2800.0f, 0.9f, 7.0f, sr);  // Dyno: that glassy mid-top
            else if (timbre == 2) tone[2].setHighShelf (3000.0f, 3.0f, sr);
            else if (timbre == 0) tone[2].setLowPass (3200.0f, 0.707f, sr);
            else                  tone[2].setPeak (1000.0f, 0.7f, 0.0f, sr);
            tone[3].setHighPass (40.0f, 0.707f, sr);
        }
        else
        {
            // 200A: nasal mid honk. 140B: a touch brighter and thinner.
            const bool b140 = settings.reedModel == 1;
            timbreDrive = timbre == 0 ? 0.5f : timbre == 1 ? 1.0f : 1.5f;
            tone[0].setHighPass (b140 ? 160.0f : 120.0f, 0.707f, sr);
            tone[1].setPeak (b140 ? 1500.0f : 1100.0f, 1.0f, 5.0f, sr);
            tone[2].setLowPass (b140 ? 7000.0f : 5500.0f, 0.707f, sr);
            if (timbre == 3) tone[3].setPeak (2600.0f, 0.9f, 5.0f, sr);
            else             tone[3].setPeak (3000.0f, 0.7f, 0.0f, sr);
        }
    }

    float ElectricPiano::pickup (float x, float drive) const
    {
        if (kind == Kind::tine)
        {
            // Electromagnetic pickup: the tine sits a little off-centre, so the response saturates
            // unevenly and adds even harmonics as the tine swings further.
            const float o = pickupOffset;
            const float u = x * drive + o;
            const float y = u / (1.0f + std::abs (u)) - o / (1.0f + o);
            return y * (1.0f + o) * (1.0f + o);
        }

        // Electrostatic pickup: capacitance goes as 1 / (gap - displacement). The reed can't touch
        // the plate, so very large swings are softly limited first.
        const float k = juce::jlimit (0.0f, 0.55f, 0.28f * drive);
        const float swing = x / (1.0f + 0.35f * std::abs (x));
        return swing / (1.0f - k * swing);
    }

    void ElectricPiano::updateDamping (Voice& v)
    {
        v.damper = damperAmount (v.held, pedal);
        const auto sr = static_cast<float> (sampleRate);
        for (auto& p : v.partials)
            p.setDamping (v.damper, sr);
    }

    void ElectricPiano::noteOn (int note, float velocity)
    {
        Voice* chosen = nullptr;
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
        v = {};
        v.note = note;
        v.held = true;
        v.age = ++counter;
        v.life = 1.0f;

        const auto sr = static_cast<float> (sampleRate);
        const float f = dsp::noteHz (static_cast<float> (note));
        const float vel = juce::jlimit (0.02f, 1.0f, velocity);
        const float strike = std::pow (vel, 1.25f);
        const float nyquist = 0.45f * sr;
        const float release = settings.softRelease ? 0.28f : 0.07f;

        // Partials that would land above Nyquist are silenced rather than aliased.
        const auto start = [&] (Partial& p, float freq, float amp, float phase, float tau)
        {
            p.start (juce::jmin (freq, nyquist), freq < nyquist ? amp : 0.0f, phase, tau, release, sr);
        };

        if (kind == Kind::tine)
        {
            const float T = tineDecay (note);
            start (v.partials[0], f, strike, 0.0f, T);
            start (v.partials[1], f * 1.0011f, 0.22f * strike, 0.4f, T * 1.6f);          // tonebar
            start (v.partials[2], f * 6.27f, 0.3f * strike * vel, 0.0f, 0.09f);          // the tine's 2nd mode
            start (v.partials[3], f * 17.55f, 0.1f * strike * vel, 0.0f, 0.025f);
            v.drive = timbreDrive * (0.9f + 0.9f * vel);
            v.gain = 0.2f;
        }
        else
        {
            const float T = reedDecay (note);
            start (v.partials[0], f, strike, 0.0f, T);
            start (v.partials[1], f * 2.0f, 0.06f * strike, 0.3f, T * 0.5f);
            start (v.partials[2], f * 6.27f, 0.22f * strike * vel, 0.0f, 0.06f);
            start (v.partials[3], f * 3.0f, 0.04f * strike, 0.0f, T * 0.3f);
            v.drive = timbreDrive * (0.5f + 1.6f * vel);
            v.gain = 0.2f;
        }

        v.thump = 0.12f * vel;
        v.thumpFilter.setCutoff (1800.0f, sampleRate);
        updateDamping (v);
    }

    void ElectricPiano::noteOff (int note)
    {
        for (auto& v : voices)
            if (v.note == note && v.held)
            {
                v.held = false;
                updateDamping (v);
                v.thump = juce::jmax (v.thump, v.damper > 0.5f ? 0.015f : 0.0f); // damper felt touching down
            }
    }

    void ElectricPiano::setPedal (float amount)
    {
        if (std::abs (amount - pedal) < 1.0e-3f)
            return;
        pedal = amount;
        for (auto& v : voices)
            if (v.note >= 0 && ! v.held)
                updateDamping (v);
    }

    void ElectricPiano::allNotesOff()
    {
        for (auto& v : voices)
            if (v.note >= 0)
            {
                v.held = false;
                updateDamping (v);
            }
    }

    bool ElectricPiano::isActive() const
    {
        for (const auto& v : voices)
            if (v.note >= 0)
                return true;
        return false;
    }

    void ElectricPiano::render (float* left, float* right, int n)
    {
        auto* mono = scratch.data();
        std::fill (mono, mono + n, 0.0f);
        const float thumpDecay = std::exp (-1.0f / (0.004f * static_cast<float> (sampleRate)));
        bool any = false;

        for (auto& v : voices)
        {
            if (v.note < 0)
                continue;
            any = true;

            for (int i = 0; i < n; ++i)
            {
                float x = 0.0f;
                for (auto& p : v.partials)
                    x += p.tick();

                float out = pickup (x, v.drive);
                if (v.thump > 1.0e-5f)
                {
                    out += v.thumpFilter.process (noise.next()) * v.thump;
                    v.thump *= thumpDecay;
                }
                mono[i] += v.gain * out;
            }

            // Retire the voice once it has decayed into silence.
            if (v.partials[0].energy() + v.partials[1].energy() < 1.0e-9f && v.thump < 1.0e-5f)
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
