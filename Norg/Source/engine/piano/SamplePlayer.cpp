#include "SamplePlayer.h"
#include "ModeledPiano.h"

namespace norg::piano
{
    namespace
    {
        // 4-point, 3rd-order Hermite interpolation.
        float hermite (float xm1, float x0, float x1, float x2, float t)
        {
            const float c = (x1 - xm1) * 0.5f;
            const float v = x0 - x1;
            const float w = c + v;
            const float a = w + v + (x2 - x0) * 0.5f;
            const float b = w + a;
            return (((a * t) - b) * t + c) * t + x0;
        }
    }

    void SamplePlayer::setInstrument (std::shared_ptr<const sfz::Instrument> newInstrument)
    {
        if (newInstrument != instrument)
        {
            instrument = std::move (newInstrument); // the library manager still owns it: nothing is freed here
            activeSwitch = instrument != nullptr ? instrument->swDefault : -1;
        }
    }

    void SamplePlayer::prepare (double newSampleRate, int)
    {
        sampleRate = newSampleRate;
        reset();
    }

    void SamplePlayer::reset()
    {
        for (auto& v : voices)
        {
            v.note = -1;
            v.region = nullptr;
            v.source = nullptr;
            v.instrument.reset();
        }
        pedal = 0.0f;
    }

    SamplePlayer::Voice& SamplePlayer::allocateVoice()
    {
        for (auto& v : voices)
            if (v.note < 0)
                return v;

        auto* oldest = &voices[0];
        for (auto& v : voices)
            if (v.age < oldest->age)
                oldest = &v;
        return *oldest;
    }

    void SamplePlayer::start (const sfz::Region& region, int note, float velocity, float gainScale)
    {
        const auto* source = instrument->source (region);
        if (source == nullptr || source->numFrames() < 4)
            return;

        // Starting a region silences anything its group turns off (e.g. open/closed hi-hats).
        if (region.group != 0)
            for (auto& v : voices)
                if (v.note >= 0 && v.region != nullptr && v.region->offBy == region.group)
                    v.fadeStep = 1.0f / (v.region->offTime * static_cast<float> (sampleRate));

        // note_polyphony: a re-struck note chokes its previous sound over off_time.
        if (region.notePolyphony > 0)
        {
            int count = 0;
            for (auto& v : voices)
                if (v.note == note && v.region != nullptr && v.fadeStep <= 0.0f
                    && (v.region->trigger == sfz::Trigger::release) == (region.trigger == sfz::Trigger::release))
                    ++count;

            for (int excess = count - region.notePolyphony + 1; excess > 0; --excess)
            {
                Voice* oldest = nullptr;
                for (auto& v : voices)
                    if (v.note == note && v.region != nullptr && v.fadeStep <= 0.0f
                        && (v.region->trigger == sfz::Trigger::release) == (region.trigger == sfz::Trigger::release)
                        && (oldest == nullptr || v.age < oldest->age))
                        oldest = &v;
                if (oldest == nullptr)
                    break;
                oldest->fadeStep = 1.0f / (region.offTime * static_cast<float> (sampleRate));
            }
        }

        auto& v = allocateVoice();
        v.instrument = instrument;
        v.region = &region;
        v.source = source;
        v.note = note;
        v.held = region.trigger != sfz::Trigger::release;
        v.releaseFired = region.trigger == sfz::Trigger::release;
        v.velocity = velocity;
        v.age = ++counter;
        v.startSample = clock;
        v.position = static_cast<double> (juce::jlimit<juce::int64> (0, source->numFrames() - 2, region.offset));

        const float semis = static_cast<float> (note - region.keyCenter) + region.transpose + region.tuneCents / 100.0f;
        v.increment = std::pow (2.0, semis / 12.0) * source->sampleRate() / sampleRate;

        // amp_veltrack: 100% means level follows velocity squared; 0% ignores velocity.
        const float track = juce::jlimit (-1.0f, 1.0f, region.ampVeltrack / 100.0f);
        const float velGain = track >= 0.0f ? (1.0f - track) + track * velocity * velocity
                                            : 1.0f + track * velocity * velocity;
        const float gain = gainScale * velGain * dsp::dbToGain (region.volumeDb);
        const float pan = juce::jlimit (-1.0f, 1.0f, region.pan / 100.0f);
        v.gainL = gain * (pan > 0.0f ? 1.0f - pan : 1.0f);
        v.gainR = gain * (pan < 0.0f ? 1.0f + pan : 1.0f);

        v.env = 1.0f;
        v.fade = 1.0f;
        v.fadeStep = 0.0f;
        v.attack = region.ampegAttack > 0.0005f ? 0.0f : 1.0f;
        v.attackStep = region.ampegAttack > 0.0005f ? 1.0f / (region.ampegAttack * static_cast<float> (sampleRate)) : 1.0f;
        updateDamping (v);
    }

    void SamplePlayer::updateDamping (Voice& v)
    {
        if (v.region == nullptr)
            return;

        const bool undamped = pianoDamping && ModeledPiano::isUndamped (v.note);
        float d = undamped ? 0.0f : damperAmount (v.held, pedal);

        // Release samples play out naturally; one-shots ignore the dampers entirely.
        if (v.region->trigger == sfz::Trigger::release || v.region->loopMode == sfz::LoopMode::oneShot)
            d = 0.0f;

        const float release = juce::jmax (0.03f, v.region->ampegRelease * (settings.softRelease ? 1.0f : 0.25f));
        v.envRate = std::exp (-d / (release * static_cast<float> (sampleRate)));
    }

    bool SamplePlayer::playable (const sfz::Region& r, float rnd) const
    {
        return (r.swLast < 0 || r.swLast == activeSwitch) && rnd >= r.loRand && rnd < r.hiRand;
    }

    void SamplePlayer::fireReleaseSamples (Voice& v)
    {
        if (v.releaseFired || instrument == nullptr || v.region == nullptr)
            return;
        v.releaseFired = true;

        // The damper settling on the string: quieter the longer the note has been ringing.
        const float heldSeconds = static_cast<float> (clock - v.startSample) / static_cast<float> (sampleRate);
        const int vel = juce::jlimit (1, 127, juce::roundToInt (v.velocity * 127.0f));
        const float rnd = random.uniform01();
        const int note = v.note;
        const float velocity = v.velocity;
        for (const auto& r : instrument->regions)
            if (r.trigger == sfz::Trigger::release && r.matches (note, vel) && playable (r, rnd))
                start (r, note, velocity, dsp::dbToGain (-r.rtDecay * heldSeconds));
    }

    void SamplePlayer::noteOn (int note, float velocity)
    {
        if (instrument == nullptr)
            return;

        // Keyswitches select a set of regions instead of playing.
        if (instrument->swLoKey >= 0 && note >= instrument->swLoKey && note <= instrument->swHiKey)
        {
            activeSwitch = note;
            return;
        }

        const int vel = juce::jlimit (1, 127, juce::roundToInt (velocity * 127.0f));
        const float rnd = random.uniform01();
        for (const auto& r : instrument->regions)
            if ((r.trigger == sfz::Trigger::attack || r.trigger == sfz::Trigger::first) && r.matches (note, vel) && playable (r, rnd))
                start (r, note, velocity, 1.0f);
    }

    void SamplePlayer::noteOff (int note)
    {
        for (auto& v : voices)
            if (v.note == note && v.held)
            {
                v.held = false;
                updateDamping (v);
                if (damperAmount (false, pedal) > 0.5f)
                    fireReleaseSamples (v);
            }
    }

    void SamplePlayer::setPedal (float amount)
    {
        const bool lifting = damperAmount (false, amount) > 0.5f && damperAmount (false, pedal) <= 0.5f;
        pedal = amount;
        for (auto& v : voices)
            if (v.note >= 0 && ! v.held)
            {
                updateDamping (v);
                if (lifting)
                    fireReleaseSamples (v);
            }
    }

    void SamplePlayer::allNotesOff()
    {
        for (auto& v : voices)
            if (v.note >= 0)
            {
                v.held = false;
                updateDamping (v);
            }
    }

    bool SamplePlayer::isActive() const
    {
        for (const auto& v : voices)
            if (v.note >= 0)
                return true;
        return false;
    }

    void SamplePlayer::render (float* left, float* right, int n)
    {
        clock += n;

        for (auto& v : voices)
        {
            if (v.note < 0)
                continue;

            const auto& src = *v.source;
            const auto frames = src.numFrames();
            const bool stereo = src.numChannels() > 1;
            const bool looping = v.region->loopMode == sfz::LoopMode::continuous
                              || (v.region->loopMode == sfz::LoopMode::sustain && v.held);
            const auto loopEnd = v.region->loopEnd > v.region->loopStart ? juce::jmin (v.region->loopEnd, frames - 2) : frames - 2;
            const auto loopStart = juce::jlimit<juce::int64> (0, loopEnd - 1, v.region->loopStart);
            bool finished = false;

            for (int i = 0; i < n; ++i)
            {
                auto idx = static_cast<juce::int64> (v.position);
                if (looping && idx >= loopEnd)
                {
                    v.position -= static_cast<double> (loopEnd - loopStart);
                    idx = static_cast<juce::int64> (v.position);
                }
                if (idx >= frames - 2)
                {
                    finished = true;
                    break;
                }

                const float t = static_cast<float> (v.position - static_cast<double> (idx));
                const auto im1 = juce::jmax<juce::int64> (0, idx - 1);
                const float l = hermite (src.get (0, im1), src.get (0, idx), src.get (0, idx + 1), src.get (0, juce::jmin (idx + 2, frames - 1)), t);
                const float r = stereo ? hermite (src.get (1, im1), src.get (1, idx), src.get (1, idx + 1), src.get (1, juce::jmin (idx + 2, frames - 1)), t)
                                       : l;

                v.attack = juce::jmin (1.0f, v.attack + v.attackStep);
                const float g = v.env * v.attack * v.fade * outputGain;
                left[i] += l * v.gainL * g;
                right[i] += r * v.gainR * g;

                v.env *= v.envRate;
                if (v.fadeStep > 0.0f)
                    v.fade = juce::jmax (0.0f, v.fade - v.fadeStep);
                v.position += v.increment;
            }

            if (finished || v.env < 1.0e-4f || v.fade <= 0.0f)
            {
                v.note = -1;
                v.region = nullptr;
                v.source = nullptr;
                v.instrument.reset(); // the library manager still holds it
            }
        }
    }
}
