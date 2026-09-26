#include "PianoSection.h"

namespace norg::piano
{
    void PianoSection::prepare (double newSampleRate, int maxBlockSize)
    {
        sampleRate = newSampleRate;
        maxBlock = juce::jmax (1, maxBlockSize);

        for (auto* e : engines)
            e->prepare (sampleRate, maxBlock);

        // Sample libraries are mastered well below full scale; bring them up to the modelled pianos.
        grandPlayer.setGain (1.3f);
        uprightPlayer.setGain (1.3f);
        resonance.prepare (sampleRate);
        pedalNoiseFilter.set (900.0f, 0.6f, sampleRate);
        pedalThumpFilter.setCutoff (220.0f, sampleRate);

        left.assign (static_cast<size_t> (maxBlock), 0.0f);
        right.assign (static_cast<size_t> (maxBlock), 0.0f);
        reset();
    }

    void PianoSection::reset()
    {
        for (auto* e : engines)
            e->reset();
        resonance.reset();
        noteEngine.fill (nullptr);
        held.fill (false);
        struck.fill (false);
        pedal = 0.0f;
        pedalWasDown = false;
        pedalNoiseEnv = 0.0f;
        tailSamples = 0;
    }

    void PianoSection::setParameters (const ParamSnapshot& p, int panel)
    {
        currentType = static_cast<Type> (juce::jlimit (0, 4, p.getInt (P::pianoType, panel)));
        useSamples = p.getBool (P::pianoSampled, panel);
        stringResonance = p.getBool (P::pianoStringRes, panel);
        pedalNoise = p.getBool (P::pianoPedalNoise, panel);

        PianoSettings s;
        s.tineModel = p.getInt (P::pianoTineModel, panel);
        s.reedModel = p.getInt (P::pianoReedModel, panel);
        s.timbre = p.getInt (P::pianoTimbre, panel);
        s.clavPickup = p.getInt (P::pianoClavPickup, panel);
        s.clavFilter = p.getInt (P::pianoClavFilter, panel);
        s.softRelease = p.getBool (P::pianoSoftRelease, panel);
        s.stretchTuning = p.getBool (P::pianoStretch, panel);
        for (auto* e : engines)
            e->setSettings (s);

        if (libraries != nullptr)
        {
            std::shared_ptr<const sfz::Instrument> instrument;
            if (libraries->tryGet (grandSlot (panel), instrument))
                grandPlayer.setInstrument (std::move (instrument));
            if (libraries->tryGet (uprightSlot (panel), instrument))
                uprightPlayer.setInstrument (std::move (instrument));
        }
    }

    bool PianoSection::usingSamples() const
    {
        return useSamples && ((currentType == Type::grand && grandPlayer.hasInstrument())
                              || (currentType == Type::upright && uprightPlayer.hasInstrument()));
    }

    PianoEngine* PianoSection::engineFor (Type t)
    {
        switch (t)
        {
            case Type::grand:
                if (useSamples && grandPlayer.hasInstrument())
                    return &grandPlayer;
                modeled.setVariant (ModeledPiano::Variant::grand);
                return &modeled;
            case Type::upright:
                if (useSamples && uprightPlayer.hasInstrument())
                    return &uprightPlayer;
                modeled.setVariant (ModeledPiano::Variant::upright);
                return &modeled;
            case Type::tine: return &tine;
            case Type::reed: return &reed;
            case Type::clav: return &clav;
        }
        return &tine;
    }

    void PianoSection::noteOn (int note, float velocity)
    {
        if (note < 0 || note > 127)
            return;

        auto* engine = engineFor (currentType);
        noteEngine[static_cast<size_t> (note)] = engine;
        held[static_cast<size_t> (note)] = true;
        struck[static_cast<size_t> (note)] = true;
        engine->noteOn (note, velocity);
        resonance.setDamping (struck, pedal);
    }

    void PianoSection::noteOff (int note, float)
    {
        if (note < 0 || note > 127)
            return;

        held[static_cast<size_t> (note)] = false;
        if (pedal < 0.5f)
            struck[static_cast<size_t> (note)] = false;
        if (auto* engine = noteEngine[static_cast<size_t> (note)])
            engine->noteOff (note);
        resonance.setDamping (struck, pedal);
    }

    void PianoSection::allNotesOff()
    {
        for (auto* e : engines)
            e->allNotesOff();
        held.fill (false);
        struck.fill (false);
        resonance.setDamping (struck, pedal);
    }

    void PianoSection::triggerPedalNoise (bool down)
    {
        if (! pedalNoise || ! (isAcoustic() || currentType == Type::tine))
            return;
        pedalNoiseEnv = 1.0f;
        pedalNoiseLevel = down ? 0.035f : 0.022f;
    }

    void PianoSection::sustainPedal (float amount)
    {
        pedal = juce::jlimit (0.0f, 1.0f, amount);
        for (auto* e : engines)
            e->setPedal (pedal);
        if (pedal < 0.5f)
            struck = held; // released strings are damped again
        resonance.setDamping (struck, pedal);

        // The pedal mechanism: a thump and a felt swish as the dampers lift or fall.
        const bool down = pedal >= 0.5f;
        if (down != pedalWasDown)
            triggerPedalNoise (down);
        pedalWasDown = down;
    }

    bool PianoSection::isActive() const
    {
        for (const auto* e : engines)
            if (e->isActive())
                return true;
        return tailSamples > 0 || pedalNoiseEnv > 1.0e-4f;
    }

    void PianoSection::render (juce::AudioBuffer<float>& buffer, int start, int num)
    {
        bool sounding = false;

        for (int offset = 0; offset < num; offset += maxBlock)
        {
            const int n = juce::jmin (maxBlock, num - offset);
            auto* l = left.data();
            auto* r = right.data();
            std::fill (l, l + n, 0.0f);
            std::fill (r, r + n, 0.0f);

            for (auto* e : engines)
                if (e->isActive())
                {
                    e->render (l, r, n);
                    sounding = true;
                }

            if (stringResonance && isAcoustic())
                resonance.process (l, r, n);

            if (pedalNoiseEnv > 1.0e-4f)
            {
                const float decay = std::exp (-1.0f / (0.07f * static_cast<float> (sampleRate)));
                for (int i = 0; i < n; ++i)
                {
                    const float x = noise.next();
                    pedalNoiseFilter.process (x);
                    const float s = pedalNoiseLevel * pedalNoiseEnv
                                  * (2.5f * pedalThumpFilter.process (x) + 0.4f * pedalNoiseFilter.band);
                    l[i] += s;
                    r[i] += s;
                    pedalNoiseEnv *= decay;
                }
            }

            buffer.addFrom (0, start + offset, l, n);
            buffer.addFrom (1, start + offset, r, n);
        }

        // Keep rendering briefly after the last voice, so resonance can ring out.
        if (sounding)
            tailSamples = static_cast<int> (0.5 * sampleRate);
        else
            tailSamples = juce::jmax (0, tailSamples - num);
    }
}
