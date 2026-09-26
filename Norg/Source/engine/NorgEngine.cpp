#include "NorgEngine.h"
#include "TestToneSection.h"
#include "organ/OrganSection.h"
#include "piano/PianoSection.h"

namespace norg
{
    namespace
    {
        // Volume knobs feel natural with a squared taper (roughly -12 dB at half travel).
        float taper (float v) { return v * v; }

        enum ChainBlock { effect1Block, effect2Block, ampBlock, rotaryBlock, delayBlock };
    }

    float delayDivisionBeats (int division)
    {
        // 1/4, 1/8 dotted, 1/8, 1/4 triplet, 1/8 triplet, 1/16, 1/2
        static constexpr float beats[] = { 1.0f, 0.75f, 0.5f, 2.0f / 3.0f, 1.0f / 3.0f, 0.25f, 2.0f };
        return beats[juce::jlimit (0, 6, division)];
    }

    NorgEngine::NorgEngine (int panel, sfz::LibraryManager* libraries) : panelIndex (panel)
    {
        const auto install = [this] (SectionId id, std::unique_ptr<Section> section, P on, P volume)
        {
            auto& slot = slots[static_cast<size_t> (id)];
            slot.section = std::move (section);
            slot.onParam = on;
            slot.volumeParam = volume;
        };

        install (SectionId::organ,  std::make_unique<organ::OrganSection>(), P::organOn,  P::organVolume);
        install (SectionId::piano,  std::make_unique<piano::PianoSection> (libraries), P::pianoOn, P::pianoVolume);
        install (SectionId::synth,  std::make_unique<TestToneSection> (0.5f), P::synthOn,  P::synthVolume);
        install (SectionId::sample, std::make_unique<TestToneSection> (2.0f), P::sampleOn, P::sampleVolume);
    }

    NorgEngine::~NorgEngine() = default;

    void NorgEngine::prepare (double newSampleRate, int newMaxBlockSize)
    {
        sampleRate = newSampleRate;
        maxBlockSize = juce::jmax (1, newMaxBlockSize);

        for (auto& slot : slots)
        {
            slot.section->prepare (sampleRate, maxBlockSize);
            slot.buffer.setSize (2, maxBlockSize, false, true, false);
            slot.gain.reset (sampleRate, 0.02);
            slot.gain.setCurrentAndTargetValue (0.0f);
        }

        masterGain.reset (sampleRate, 0.02);

        for (auto* block : std::initializer_list<fx::FxBlock*> { &effect1, &effect2, &ampEq, &delay, &compressor, &reverb })
            block->prepareBlock (sampleRate, maxBlockSize);
        rotary.prepare (sampleRate, maxBlockSize);
        mix.setSize (2, maxBlockSize, false, true, false);
    }

    void NorgEngine::reset()
    {
        for (auto& slot : slots)
            slot.section->reset();
        for (auto* block : std::initializer_list<fx::FxBlock*> { &effect1, &effect2, &ampEq, &delay, &compressor, &reverb })
            block->reset();
        rotary.reset();
    }

    bool NorgEngine::isActive() const
    {
        for (const auto& slot : slots)
            if (slot.section->isActive() || (slot.enabled && slot.gain.getCurrentValue() > 0.0f))
                return true;
        return delay.hasTail() || reverb.hasTail();
    }

    SectionId NorgEngine::sourceSection (int source, const ParamSnapshot& params) const
    {
        // The third source is whichever section the mode has in that place: Synth or Sample.
        if (source <= 0) return SectionId::organ;
        if (source == 1) return SectionId::piano;
        return params.getInt (P::mode) == 0 ? SectionId::synth : SectionId::sample;
    }

    void NorgEngine::setEffects (const ParamSnapshot& p)
    {
        const int panel = panelIndex;

        effect1.setEnabled (p.getBool (P::fx1On, panel));
        effect1.set (static_cast<fx::ModEffect1::Type> (p.getInt (P::fx1Type, panel)), p.get (P::fx1Rate, panel), p.get (P::fx1Amount, panel));
        chainSource[effect1Block] = sourceSection (p.getInt (P::fx1Source, panel), p);

        effect2.setEnabled (p.getBool (P::fx2On, panel));
        effect2.set (static_cast<fx::ModEffect2::Type> (p.getInt (P::fx2Type, panel)), p.get (P::fx2Rate, panel), p.get (P::fx2Amount, panel));
        chainSource[effect2Block] = sourceSection (p.getInt (P::fx2Source, panel), p);

        ampEq.setEnabled (p.getBool (P::ampOn, panel));
        ampEq.set (static_cast<fx::AmpEq::Type> (p.getInt (P::ampType, panel)), p.get (P::ampDrive, panel),
                   p.get (P::eqBass, panel), p.get (P::eqMid, panel), p.get (P::eqMidFreq, panel), p.get (P::eqTreble, panel));
        chainSource[ampBlock] = sourceSection (p.getInt (P::ampSource, panel), p);

        rotary.setEnabled (p.getBool (P::rotaryOn, panel));
        rotary.setSpeed (p.getBool (P::rotaryFast, panel), p.getBool (P::rotaryStop, panel));
        rotary.setDrive (p.get (P::rotaryDrive, panel));
        chainSource[rotaryBlock] = sourceSection (p.getInt (P::rotarySource, panel), p);

        const float seconds = p.getBool (P::delaySync, panel)
                                ? delayDivisionBeats (p.getInt (P::delayDivision, panel)) * static_cast<float> (60.0 / tempo)
                                : p.get (P::delayTime, panel);
        delay.setEnabled (p.getBool (P::delayOn, panel));
        delay.set (seconds, p.get (P::delayFeedback, panel), p.get (P::delayMix, panel),
                   p.getBool (P::delayPingPong, panel), p.get (P::delayTone, panel));
        chainSource[delayBlock] = sourceSection (p.getInt (P::delaySource, panel), p);

        compressor.setEnabled (p.getBool (P::compOn, panel));
        compressor.set (p.get (P::compAmount, panel), p.getBool (P::compFast, panel));

        reverb.setEnabled (p.getBool (P::reverbOn, panel));
        reverb.set (static_cast<fx::Reverb::Type> (p.getInt (P::reverbType, panel)), p.get (P::reverbAmount, panel),
                    p.getBool (P::reverbBright, panel));
    }

    void NorgEngine::processEffects (int n)
    {
        const auto channelsOf = [this] (SectionId id)
        {
            auto& buffer = slots[static_cast<size_t> (id)].buffer;
            return std::pair { buffer.getWritePointer (0), buffer.getWritePointer (1) };
        };

        auto [l1, r1] = channelsOf (chainSource[effect1Block]);
        effect1.process (l1, r1, n);
        auto [l2, r2] = channelsOf (chainSource[effect2Block]);
        effect2.process (l2, r2, n);
        auto [l3, r3] = channelsOf (chainSource[ampBlock]);
        ampEq.process (l3, r3, n);
        auto [l4, r4] = channelsOf (chainSource[rotaryBlock]);
        rotary.process (l4, r4, n);
        auto [l5, r5] = channelsOf (chainSource[delayBlock]);
        delay.process (l5, r5, n);
    }

    bool NorgEngine::sectionAvailable (SectionId id, const ParamSnapshot& params) const
    {
        // Stage has a Synth section, Electro a Sample section; Panel B only exists in Stage mode.
        const bool stage = params.getInt (P::mode) == 0;
        if (! stage && panelIndex == 1)
            return false;
        if (id == SectionId::synth)
            return stage;
        if (id == SectionId::sample)
            return ! stage;
        return true;
    }

    void NorgEngine::handleMidi (const juce::MidiMessage& m)
    {
        if (m.isNoteOn())
        {
            for (auto& slot : slots)
                if (slot.enabled)
                    slot.section->noteOn (m.getNoteNumber(), m.getFloatVelocity());
        }
        else if (m.isNoteOff())
        {
            for (auto& slot : slots)
                slot.section->noteOff (m.getNoteNumber(), m.getFloatVelocity());
        }
        else if (m.isSustainPedalOn() || m.isSustainPedalOff() || m.isControllerOfType (64))
        {
            const float amount = static_cast<float> (m.getControllerValue()) / 127.0f;
            for (auto& slot : slots)
                slot.section->sustainPedal (amount);
        }
        else if (m.isControllerOfType (11))
        {
            const float amount = static_cast<float> (m.getControllerValue()) / 127.0f;
            for (auto& slot : slots)
                slot.section->expression (amount);
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff())
        {
            for (auto& slot : slots)
                slot.section->allNotesOff();
        }
    }

    void NorgEngine::renderRange (int start, int num)
    {
        for (auto& slot : slots)
            if (slot.enabled || slot.section->isActive())
                slot.section->render (slot.buffer, start, num);
    }

    void NorgEngine::process (juce::AudioBuffer<float>& output, const juce::MidiBuffer& midi, const ParamSnapshot& params)
    {
        const int numSamples = output.getNumSamples();
        const int channels = juce::jmin (2, output.getNumChannels());
        output.clear();

        for (size_t i = 0; i < slots.size(); ++i)
        {
            auto& slot = slots[i];
            const bool nowEnabled = sectionAvailable (static_cast<SectionId> (i), params)
                                 && params.getBool (slot.onParam, panelIndex);

            if (slot.enabled && ! nowEnabled)
                slot.section->allNotesOff();

            slot.enabled = nowEnabled;
            slot.gain.setTargetValue (nowEnabled ? taper (params.get (slot.volumeParam, panelIndex)) : 0.0f);
            slot.section->setParameters (params, panelIndex);
        }

        masterGain.setTargetValue (taper (params.get (P::masterVolume)));
        setEffects (params);

        // Hosts may send blocks bigger than promised, so render in chunks that fit our buffers.
        auto event = midi.begin();
        for (int chunkStart = 0; chunkStart < numSamples; chunkStart += maxBlockSize)
        {
            const int chunkLength = juce::jmin (maxBlockSize, numSamples - chunkStart);

            for (auto& slot : slots)
                slot.buffer.clear (0, chunkLength);

            int position = 0;
            for (; event != midi.end() && (*event).samplePosition < chunkStart + chunkLength; ++event)
            {
                const int eventTime = juce::jlimit (0, chunkLength, (*event).samplePosition - chunkStart);
                if (eventTime > position)
                {
                    renderRange (position, eventTime - position);
                    position = eventTime;
                }
                handleMidi ((*event).getMessage());
            }

            if (position < chunkLength)
                renderRange (position, chunkLength - position);

            // Section volume first (so it drives the amp and rotary like a real level), then
            // each section's effects, then the panel mix.
            for (auto& slot : slots)
            {
                if (! slot.gain.isSmoothing() && slot.gain.getTargetValue() <= 0.0f)
                {
                    slot.buffer.clear (0, chunkLength);
                    continue;
                }

                for (int ch = 0; ch < 2; ++ch)
                {
                    auto gain = slot.gain;
                    auto* data = slot.buffer.getWritePointer (ch);
                    for (int n = 0; n < chunkLength; ++n)
                        data[n] *= gain.getNextValue();
                }
                slot.gain.skip (chunkLength);
            }

            processEffects (chunkLength);

            mix.clear (0, chunkLength);
            for (auto& slot : slots)
                for (int ch = 0; ch < 2; ++ch)
                    mix.addFrom (ch, 0, slot.buffer, ch, 0, chunkLength);

            compressor.process (mix.getWritePointer (0), mix.getWritePointer (1), chunkLength);
            reverb.process (mix.getWritePointer (0), mix.getWritePointer (1), chunkLength);

            for (int ch = 0; ch < channels; ++ch)
                output.copyFrom (ch, chunkStart, mix, channels == 1 ? 0 : ch, 0, chunkLength);
            if (channels == 1)
                output.addFrom (0, chunkStart, mix, 1, 0, chunkLength);
        }

        for (; event != midi.end(); ++event)
            handleMidi ((*event).getMessage());

        for (int ch = 0; ch < channels; ++ch)
        {
            auto gain = masterGain;
            auto* dst = output.getWritePointer (ch);
            for (int n = 0; n < numSamples; ++n)
                dst[n] *= gain.getNextValue();
        }
        masterGain.skip (numSamples);
    }
}
