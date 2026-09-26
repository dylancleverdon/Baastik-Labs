#include "ComboOrgan.h"

namespace norg::organ
{
    void ComboOrgan::prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;

        sineTable.fill ([] (float x) { return std::sin (x); });
        fluteTable.fill ([] (float x) { return std::sin (x) + 0.12f * std::sin (2 * x) + 0.04f * std::sin (3 * x); });
        triangleTable.fill ([] (float x)
        {
            float s = 0.0f;
            for (int k = 1; k <= 15; k += 2)
                s += ((k / 2) % 2 == 0 ? 1.0f : -1.0f) * std::sin (static_cast<float> (k) * x) / static_cast<float> (k * k);
            return s * 0.81f;
        });
        principalTable.fill ([] (float x)
        {
            return 0.62f * (std::sin (x) + 0.5f * std::sin (2 * x) + 0.32f * std::sin (3 * x)
                            + 0.16f * std::sin (4 * x) + 0.08f * std::sin (5 * x) + 0.04f * std::sin (6 * x));
        });
        stoppedTable.fill ([] (float x) { return 0.85f * (std::sin (x) + 0.22f * std::sin (3 * x) + 0.06f * std::sin (5 * x)); });

        const auto stop = [] (std::initializer_list<Component> parts)
        {
            Stop s;
            for (const auto& p : parts)
                s.parts[static_cast<size_t> (s.numParts++)] = p;
            return s;
        };

        // Vox: drawbars 1-4 are the flute voices (16', 8', 4', IV), 5-8 the reed voices, 9 brightens the reeds.
        voxStops = { stop ({ { -12, Wave::triangle, 1.0f, 0 } }),
                     stop ({ { 0, Wave::triangle, 1.0f, 0 } }),
                     stop ({ { 12, Wave::triangle, 1.0f, 0 } }),
                     stop ({ { 19, Wave::triangle, 0.5f, 0 }, { 24, Wave::triangle, 0.5f, 0 },
                             { 28, Wave::triangle, 0.4f, 0 }, { 31, Wave::triangle, 0.4f, 0 } }),
                     stop ({ { -12, Wave::pulse30, 0.8f, 5.0f } }),
                     stop ({ { 0, Wave::pulse30, 0.8f, 5.0f } }),
                     stop ({ { 12, Wave::pulse30, 0.8f, 5.0f } }),
                     stop ({ { 19, Wave::pulse30, 0.4f, 5.0f }, { 24, Wave::pulse30, 0.4f, 5.0f },
                             { 28, Wave::pulse30, 0.3f, 5.0f }, { 31, Wave::pulse30, 0.3f, 5.0f } }),
                     stop ({}) };

        // Farf tabs: Bass 16, Strings 16, Flute 8, Oboe 8, Trumpet 8, Strings 8, Flute 4, Strings 4, 2 2/3.
        farfStops = { stop ({ { -12, Wave::square, 1.0f, 2.5f } }),
                      stop ({ { -12, Wave::pulse12, 0.8f, 0 } }),
                      stop ({ { 0, Wave::flute, 1.0f, 0 } }),
                      stop ({ { 0, Wave::pulse25, 0.8f, 4.0f } }),
                      stop ({ { 0, Wave::saw, 0.75f, 7.0f } }),
                      stop ({ { 0, Wave::pulse12, 0.75f, 0 } }),
                      stop ({ { 12, Wave::flute, 0.9f, 0 } }),
                      stop ({ { 12, Wave::pulse12, 0.7f, 0 } }),
                      stop ({ { 19, Wave::flute, 0.7f, 0 } }) };

        // Pipe: Bourdon 16, Principal 8, Flute 8, Octave 4, Flute 4, Nazard 2 2/3, Superoctave 2, Tierce 1 3/5, Mixture.
        pipeStops = { stop ({ { -12, Wave::stopped, 1.0f, 0 } }),
                      stop ({ { 0, Wave::principal, 1.0f, 0 } }),
                      stop ({ { 0, Wave::flute, 0.9f, 0 } }),
                      stop ({ { 12, Wave::principal, 0.8f, 0 } }),
                      stop ({ { 12, Wave::flute, 0.7f, 0 } }),
                      stop ({ { 19, Wave::flute, 0.55f, 0 } }),
                      stop ({ { 24, Wave::principal, 0.55f, 0 } }),
                      stop ({ { 28, Wave::flute, 0.45f, 0 } }),
                      stop ({ { 19, Wave::principal, 0.35f, 0 }, { 24, Wave::principal, 0.3f, 0 },
                              { 31, Wave::principal, 0.25f, 0 } }) };

        reset();
    }

    void ComboOrgan::reset()
    {
        for (auto& v : voices)
            v = {};
    }

    const std::array<ComboOrgan::Stop, 9>& ComboOrgan::registration() const
    {
        return model == Model::vox ? voxStops : model == Model::farf ? farfStops : pipeStops;
    }

    void ComboOrgan::setModel (Model m)
    {
        if (m == model)
            return;
        model = m;
        allNotesOff();
    }

    void ComboOrgan::setDrawbars (int bus, const std::array<float, 9>& gains)
    {
        drawbars[static_cast<size_t> (juce::jlimit (0, 1, bus))] = gains;
    }

    void ComboOrgan::startVoice (Voice& v, int note, int bus)
    {
        v = {};
        v.note = note;
        v.bus = bus;
        v.gate = true;
        v.age = ++noteCounter;
        v.chiff = model == Model::pipe ? 1.0f : 0.0f;
        v.chiffFilter.set (dsp::noteHz (static_cast<float> (note)) * 3.0f, 1.2f, sampleRate);

        const auto& stops = registration();
        const float freq = dsp::noteHz (static_cast<float> (note));
        const float nyquist = 0.45f * static_cast<float> (sampleRate);

        for (int s = 0; s < 9; ++s)
        {
            const auto& stop = stops[static_cast<size_t> (s)];
            for (int p = 0; p < stop.numParts && v.numParts < maxParts; ++p)
            {
                const auto& part = stop.parts[static_cast<size_t> (p)];
                float f = freq * std::pow (2.0f, static_cast<float> (part.semitones) / 12.0f);
                while (f > nyquist)
                    f *= 0.5f; // fold back an octave rather than alias

                const auto i = static_cast<size_t> (v.numParts++);
                v.part[i] = &part;
                v.stopIndex[i] = s;
                v.increment[i] = f / static_cast<float> (sampleRate);
                // Divide-down organs start every footage of a key in phase.
                v.phase[i] = model == Model::pipe ? noise.uniform01() : 0.0f;
                v.lpCoeff[i] = part.brightness > 0.0f
                                   ? 1.0f - std::exp (-dsp::twoPi * juce::jmin (nyquist, f * part.brightness)
                                                      / static_cast<float> (sampleRate))
                                   : 1.0f;
            }
        }
    }

    void ComboOrgan::noteOn (int note, int bus)
    {
        Voice* chosen = nullptr;
        for (auto& v : voices)
            if (v.note == note && v.gate)
                return;

        for (auto& v : voices)
            if (v.note < 0) { chosen = &v; break; }

        if (chosen == nullptr) // steal the oldest
        {
            chosen = &voices[0];
            for (auto& v : voices)
                if (v.age < chosen->age)
                    chosen = &v;
        }

        startVoice (*chosen, note, bus);
    }

    void ComboOrgan::noteOff (int note)
    {
        for (auto& v : voices)
            if (v.note == note)
                v.gate = false;
    }

    void ComboOrgan::allNotesOff()
    {
        for (auto& v : voices)
            v.gate = false;
    }

    bool ComboOrgan::isActive() const
    {
        for (const auto& v : voices)
            if (v.note >= 0)
                return true;
        return false;
    }

    float ComboOrgan::oscillator (const Component& c, float ph, float inc) const
    {
        switch (c.wave)
        {
            case Wave::sine:      return sineTable.read (ph);
            case Wave::flute:     return fluteTable.read (ph);
            case Wave::triangle:  return triangleTable.read (ph);
            case Wave::principal: return principalTable.read (ph);
            case Wave::stopped:   return stoppedTable.read (ph);
            case Wave::saw:       return 0.7f * (2.0f * ph - 1.0f - dsp::polyBlep (ph, inc));
            case Wave::square:
            case Wave::pulse30:
            case Wave::pulse25:
            case Wave::pulse12:
            {
                const float width = c.wave == Wave::square ? 0.5f : c.wave == Wave::pulse30 ? 0.3f
                                  : c.wave == Wave::pulse25 ? 0.25f : 0.12f;
                float y = ph < width ? 1.0f : -1.0f;
                y += dsp::polyBlep (ph, inc);
                float t2 = ph - width;
                if (t2 < 0.0f) t2 += 1.0f;
                y -= dsp::polyBlep (t2, inc);
                return 0.6f * (y - (2.0f * width - 1.0f)); // remove the DC of narrow pulses
            }
        }
        return 0.0f;
    }

    void ComboOrgan::render (float* upper, float* lower, int n)
    {
        const bool pipe = model == Model::pipe;
        const float sr = static_cast<float> (sampleRate);
        const float attack = 1.0f / ((pipe ? 0.045f : 0.003f) * sr);
        const float release = 1.0f / ((pipe ? 0.14f : 0.018f) * sr);
        const float chiffDecay = std::exp (-1.0f / (0.035f * sr));

        // Vox drawbar 9 brightens the reeds; on the other models it is an ordinary stop.
        const auto& upperBars = drawbars[0];
        const float voxBright = model == Model::vox ? 1.0f + 3.0f * upperBars[8] : 1.0f;

        for (auto& v : voices)
        {
            if (v.note < 0)
                continue;

            auto* out = v.bus == 1 ? lower : upper;
            const auto& bars = drawbars[static_cast<size_t> (v.bus)];

            for (int p = 0; p < v.numParts; ++p)
                v.level[static_cast<size_t> (p)] = bars[static_cast<size_t> (v.stopIndex[static_cast<size_t> (p)])]
                                                   * v.part[static_cast<size_t> (p)]->gain;

            for (int i = 0; i < n; ++i)
            {
                if (v.gate) v.env = juce::jmin (1.0f, v.env + attack);
                else        v.env = juce::jmax (0.0f, v.env - release);

                float s = 0.0f;
                for (int p = 0; p < v.numParts; ++p)
                {
                    const auto pi = static_cast<size_t> (p);
                    if (v.level[pi] <= 0.0f)
                    {
                        v.phase[pi] += v.increment[pi];
                        v.phase[pi] -= std::floor (v.phase[pi]);
                        continue;
                    }

                    float x = oscillator (*v.part[pi], v.phase[pi], v.increment[pi]);
                    if (v.lpCoeff[pi] < 1.0f)
                    {
                        const float coeff = juce::jmin (1.0f, v.lpCoeff[pi] * voxBright);
                        v.lpState[pi] += coeff * (x - v.lpState[pi]);
                        x = v.lpState[pi];
                    }
                    s += v.level[pi] * x;

                    v.phase[pi] += v.increment[pi];
                    if (v.phase[pi] >= 1.0f)
                        v.phase[pi] -= 1.0f;
                }

                if (pipe)
                {
                    // Chiff at the start of the note, plus a whisper of wind while it sounds.
                    v.chiffFilter.process (noise.next());
                    s += (0.35f * v.chiff + 0.015f) * v.chiffFilter.band;
                    v.chiff *= chiffDecay;
                }

                out[i] += 0.2f * v.env * s;
            }

            if (! v.gate && v.env <= 0.0f)
                v.note = -1;
        }
    }
}
