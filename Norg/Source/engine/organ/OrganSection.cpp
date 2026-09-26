#include "OrganSection.h"

namespace norg::organ
{
    namespace
    {
        P offsetParam (P first, int offset)
        {
            return static_cast<P> (static_cast<int> (first) + offset);
        }

        static_assert (static_cast<int> (P::organDbI9) - static_cast<int> (P::organDbI1) == 8);
        static_assert (static_cast<int> (P::organDbII9) - static_cast<int> (P::organDbII1) == 8);
        static_assert (static_cast<int> (P::organLower9) - static_cast<int> (P::organLower1) == 8);

        constexpr float outputGain = 0.16f;
    }

    float OrganSection::drawbarGain (int position)
    {
        if (position <= 0)
            return 0.0f;
        return dsp::dbToGain (-3.0f * static_cast<float> (8 - juce::jmin (8, position)));
    }

    void OrganSection::prepare (double newSampleRate, int maxBlockSize)
    {
        sampleRate = newSampleRate;
        maxBlock = juce::jmax (1, maxBlockSize);

        tonewheels.prepare (sampleRate);
        combo.prepare (sampleRate);
        upperVibrato.prepare (sampleRate);
        lowerVibrato.prepare (sampleRate);

        swellCoeff = static_cast<float> (1.0 - std::exp (-1.0 / (0.01 * sampleRate)));
        reset();
    }

    void OrganSection::reset()
    {
        tonewheels.reset();
        combo.reset();
        upperVibrato.reset();
        lowerVibrato.reset();
        held.fill (false);
        sustained.fill (false);
        pedalDown = false;
        swell = swellTarget;
        tailSamples = 0;
    }

    void OrganSection::setParameters (const ParamSnapshot& p, int panel)
    {
        model = static_cast<Model> (juce::jlimit (0, 3, p.getInt (P::organModel, panel)));

        const P upperFirst = p.getInt (P::organPreset, panel) == 0 ? P::organDbI1 : P::organDbII1;
        std::array<float, 9> upper {}, lower {}, pedal {};
        for (int d = 0; d < 9; ++d)
        {
            upper[static_cast<size_t> (d)] = drawbarGain (p.getInt (offsetParam (upperFirst, d), panel));
            lower[static_cast<size_t> (d)] = drawbarGain (p.getInt (offsetParam (P::organLower1, d), panel));
        }
        pedal[0] = drawbarGain (p.getInt (P::organPedal16, panel));
        pedal[1] = drawbarGain (p.getInt (P::organPedal8, panel));

        tonewheels.setDrawbars (upperBus, upper);
        tonewheels.setDrawbars (lowerBus, lower);
        tonewheels.setDrawbars (pedalBus, pedal);
        combo.setDrawbars (0, upper);
        combo.setDrawbars (1, lower);

        if (model != Model::b3)
            combo.setModel (model == Model::vox ? ComboOrgan::Model::vox
                            : model == Model::farf ? ComboOrgan::Model::farf : ComboOrgan::Model::pipe);

        tonewheels.setPercussion (model == Model::b3 && p.getBool (P::organPercOn, panel),
                                  p.getBool (P::organPercThird, panel),
                                  p.getBool (P::organPercFast, panel),
                                  p.getBool (P::organPercSoft, panel));
        tonewheels.setClick (p.get (P::organClick, panel));
        tonewheels.setTonewheelMode (p.getInt (P::organTonewheel, panel));

        split = p.getBool (P::organSplit, panel);
        splitPoint = p.getInt (P::organSplitPoint, panel);
        pedals = p.getBool (P::organPedals, panel);
        pedalPoint = p.getInt (P::organPedalPoint, panel);
        sustainToOrgan = p.getBool (P::organSustain, panel);

        vibOn = p.getBool (P::organVibOn, panel);
        vibMode = p.getInt (P::organVibMode, panel);
        vibLower = p.getBool (P::organVibLower, panel);
        upperVibrato.set (vibOn, vibMode);
        lowerVibrato.set (vibOn && vibLower, vibMode);
    }

    int OrganSection::busForNote (int note) const
    {
        if (! split)
            return upperBus;
        if (pedals && note < pedalPoint)
            return pedalBus;
        return note < splitPoint ? lowerBus : upperBus;
    }

    void OrganSection::noteOn (int note, float)
    {
        if (note < 0 || note > 127)
            return;

        held[static_cast<size_t> (note)] = true;
        sustained[static_cast<size_t> (note)] = false;

        const int bus = busForNote (note);
        if (bus == pedalBus || model == Model::b3)
            tonewheels.keyDown (note, bus);
        else
            combo.noteOn (note, bus);
    }

    void OrganSection::release (int note)
    {
        // Send the release to both engines: the note may have started before a model change.
        tonewheels.keyUp (note);
        combo.noteOff (note);
    }

    void OrganSection::noteOff (int note, float)
    {
        if (note < 0 || note > 127)
            return;

        held[static_cast<size_t> (note)] = false;
        if (sustainToOrgan && pedalDown)
            sustained[static_cast<size_t> (note)] = true;
        else
            release (note);
    }

    void OrganSection::allNotesOff()
    {
        held.fill (false);
        sustained.fill (false);
        tonewheels.allKeysUp();
        combo.allNotesOff();
    }

    void OrganSection::sustainPedal (float amount)
    {
        const bool down = amount >= 0.5f;
        if (pedalDown && ! down)
            for (int n = 0; n < 128; ++n)
                if (sustained[static_cast<size_t> (n)])
                {
                    sustained[static_cast<size_t> (n)] = false;
                    if (! held[static_cast<size_t> (n)])
                        release (n);
                }
        pedalDown = down;
    }

    void OrganSection::expression (float amount)
    {
        // Swell pedal: heel down is about -24 dB, not silence.
        const float a = juce::jlimit (0.0f, 1.0f, amount);
        swellTarget = 0.06f + 0.94f * a * std::sqrt (a);
    }

    bool OrganSection::isActive() const
    {
        return tonewheels.isActive() || combo.isActive() || tailSamples > 0;
    }

    void OrganSection::render (juce::AudioBuffer<float>& buffer, int start, int num)
    {
        auto* outL = buffer.getWritePointer (0, start);
        auto* outR = buffer.getWritePointer (1, start);

        for (int chunk = 0; chunk < num; chunk += TonewheelOrgan::maxChunk)
        {
            const int n = juce::jmin (TonewheelOrgan::maxChunk, num - chunk);
            float upper[TonewheelOrgan::maxChunk] {}, lower[TonewheelOrgan::maxChunk] {}, pedal[TonewheelOrgan::maxChunk] {};

            tonewheels.render (upper, lower, pedal, n);
            combo.render (upper, lower, n);
            upperVibrato.process (upper, n);
            lowerVibrato.process (lower, n);

            for (int i = 0; i < n; ++i)
            {
                swell += swellCoeff * (swellTarget - swell);
                const float y = outputGain * swell * (upper[i] + lower[i] + pedal[i]);
                outL[chunk + i] += y;
                outR[chunk + i] += y;
            }
        }

        // Keep running briefly after the last key, so the vibrato scanner's delay line empties.
        if (tonewheels.isActive() || combo.isActive())
            tailSamples = static_cast<int> (0.1 * sampleRate);
        else
            tailSamples = juce::jmax (0, tailSamples - num);
    }
}
