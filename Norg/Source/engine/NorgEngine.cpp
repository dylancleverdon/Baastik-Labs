#include "NorgEngine.h"
#include "TestToneSection.h"

namespace norg
{
    namespace
    {
        // Volume knobs feel natural with a squared taper (roughly -12 dB at half travel).
        float taper (float v) { return v * v; }
    }

    NorgEngine::NorgEngine (int panel) : panelIndex (panel)
    {
        const auto install = [this] (SectionId id, std::unique_ptr<Section> section, P on, P volume)
        {
            auto& slot = slots[static_cast<size_t> (id)];
            slot.section = std::move (section);
            slot.onParam = on;
            slot.volumeParam = volume;
        };

        install (SectionId::organ,  std::make_unique<TestToneSection>(),      P::organOn,  P::organVolume);
        install (SectionId::piano,  std::make_unique<TestToneSection> (1.0f), P::pianoOn,  P::pianoVolume);
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
    }

    void NorgEngine::reset()
    {
        for (auto& slot : slots)
            slot.section->reset();
    }

    bool NorgEngine::isActive() const
    {
        for (const auto& slot : slots)
            if (slot.section->isActive() || (slot.enabled && slot.gain.getCurrentValue() > 0.0f))
                return true;
        return false;
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

            for (auto& slot : slots)
            {
                if (! slot.gain.isSmoothing() && slot.gain.getTargetValue() <= 0.0f)
                    continue;

                for (int ch = 0; ch < channels; ++ch)
                {
                    auto gain = slot.gain;
                    const auto* src = slot.buffer.getReadPointer (ch);
                    auto* dst = output.getWritePointer (ch, chunkStart);
                    for (int n = 0; n < chunkLength; ++n)
                        dst[n] += src[n] * gain.getNextValue();
                }
                slot.gain.skip (chunkLength);
            }
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
