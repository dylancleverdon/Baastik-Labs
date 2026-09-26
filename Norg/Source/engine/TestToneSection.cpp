#include "TestToneSection.h"

namespace norg
{
    void TestToneSection::prepare (double newSampleRate, int)
    {
        sampleRate = newSampleRate;
        attackStep = static_cast<float> (1.0 / (0.004 * sampleRate));
        releaseStep = static_cast<float> (1.0 / (0.08 * sampleRate));
        reset();
    }

    void TestToneSection::reset()
    {
        for (auto& v : voices)
            v = {};
        pedalDown = false;
    }

    void TestToneSection::noteOn (int note, float velocity)
    {
        auto* chosen = &voices[0];
        for (auto& v : voices)
        {
            if (v.level <= 0.0f && v.target <= 0.0f) { chosen = &v; break; }
            if (v.age < chosen->age) chosen = &v;
        }

        chosen->note = note;
        chosen->keyDown = true;
        chosen->sustained = false;
        chosen->increment = juce::MathConstants<double>::twoPi
                          * juce::MidiMessage::getMidiNoteInHertz (note) / sampleRate;
        chosen->velocity = 0.3f + 0.7f * velocity;
        chosen->target = 1.0f;
        chosen->age = ++noteCounter;
    }

    void TestToneSection::release (Voice& v)
    {
        v.keyDown = false;
        if (pedalDown)
            v.sustained = true;
        else
            v.target = 0.0f;
    }

    void TestToneSection::noteOff (int note, float)
    {
        for (auto& v : voices)
            if (v.note == note && v.keyDown)
                release (v);
    }

    void TestToneSection::allNotesOff()
    {
        for (auto& v : voices)
        {
            v.keyDown = false;
            v.sustained = false;
            v.target = 0.0f;
        }
    }

    void TestToneSection::sustainPedal (float amount)
    {
        pedalDown = amount >= 0.5f;
        if (! pedalDown)
            for (auto& v : voices)
                if (v.sustained)
                {
                    v.sustained = false;
                    v.target = 0.0f;
                }
    }

    bool TestToneSection::isActive() const
    {
        for (const auto& v : voices)
            if (v.level > 0.0f || v.target > 0.0f)
                return true;
        return false;
    }

    void TestToneSection::render (juce::AudioBuffer<float>& buffer, int start, int num)
    {
        auto* left = buffer.getWritePointer (0);
        auto* right = buffer.getWritePointer (1);

        for (auto& v : voices)
        {
            if (v.level <= 0.0f && v.target <= 0.0f)
                continue;

            for (int n = start; n < start + num; ++n)
            {
                if (v.level < v.target)      v.level = juce::jmin (v.target, v.level + attackStep);
                else if (v.level > v.target) v.level = juce::jmax (v.target, v.level - releaseStep);

                const auto p = v.phase;
                const float s = static_cast<float> (std::sin (p) + 0.5 * std::sin (0.5 * p)
                                                    + 0.25 * partialColour * std::sin (2.0 * p));
                const float out = 0.12f * s * v.level * v.velocity;
                left[n] += out;
                right[n] += out;

                v.phase += v.increment;
                if (v.phase > 4.0 * juce::MathConstants<double>::twoPi)
                    v.phase -= 4.0 * juce::MathConstants<double>::twoPi;
            }

            if (v.level <= 0.0f && v.target <= 0.0f)
                v.note = -1;
        }
    }
}
