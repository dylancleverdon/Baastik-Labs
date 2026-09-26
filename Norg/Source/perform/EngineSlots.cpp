#include "EngineSlots.h"

namespace norg::perform
{
    EngineSlots::EngineSlots (int panel, sfz::LibraryManager* libraries)
    {
        for (auto& e : engines)
            e = std::make_unique<NorgEngine> (panel, libraries);
        for (auto& s : settingsFor)
            s.resetToDefaults();
        noteOwner.fill (-1);
    }

    void EngineSlots::prepare (double sampleRate, int maxBlockSize)
    {
        for (auto& e : engines)
            e->prepare (sampleRate, maxBlockSize);
        for (auto& m : midiFor)
            m.ensureSize (8192);
        scratch.setSize (2, chunkSize, false, true, false);
        reset();
    }

    void EngineSlots::reset()
    {
        for (auto& e : engines)
            e->reset();
        noteOwner.fill (-1);
        fadingActive = false;
        switchPending = false;
        sustainValue = 0;
        expressionValue = -1;
    }

    bool EngineSlots::isActive() const
    {
        return engines[0]->isActive() || engines[1]->isActive();
    }

    void EngineSlots::doSwitch()
    {
        switchPending = false;
        const int fresh = 1 - current;

        // If the older sound is somehow still ringing from the change before, it gives way.
        engines[static_cast<size_t> (fresh)]->reset();
        for (auto& owner : noteOwner)
            if (owner == fresh)
                owner = -1;

        current = fresh;
        fadingActive = true;
    }

    void EngineSlots::routeEvents (const juce::MidiBuffer& midi, int start, int length)
    {
        for (auto& m : midiFor)
            m.clear();

        for (const auto metadata : midi)
        {
            if (metadata.samplePosition < start || metadata.samplePosition >= start + length)
                continue;

            const auto message = metadata.getMessage();
            const int time = metadata.samplePosition - start;

            if (message.isNoteOn())
            {
                const auto note = static_cast<size_t> (message.getNoteNumber());
                // A retriggered note that the old sound still holds: release it there first.
                if (noteOwner[note] >= 0 && noteOwner[note] != current)
                    midiFor[static_cast<size_t> (noteOwner[note])].addEvent (juce::MidiMessage::noteOff (message.getChannel(), message.getNoteNumber()), time);
                noteOwner[note] = current;
                midiFor[static_cast<size_t> (current)].addEvent (message, time);
            }
            else if (message.isNoteOff())
            {
                const auto note = static_cast<size_t> (message.getNoteNumber());
                const int owner = noteOwner[note] >= 0 ? noteOwner[note] : current;
                noteOwner[note] = -1;
                midiFor[static_cast<size_t> (owner)].addEvent (message, time);
            }
            else
            {
                if (message.isControllerOfType (64))
                    sustainValue = message.getControllerValue();
                else if (message.isControllerOfType (11))
                    expressionValue = message.getControllerValue();

                // Pedals, controllers and panic go to both sounds.
                for (auto& m : midiFor)
                    m.addEvent (message, time);
            }
        }
    }

    void EngineSlots::process (juce::AudioBuffer<float>& output, const juce::MidiBuffer& midi, const ParamSnapshot& settings, double tempo)
    {
        const int total = output.getNumSamples();
        const int channels = juce::jmin (2, output.getNumChannels());

        bool freshEngine = false;
        if (switchPending)
        {
            doSwitch();
            freshEngine = true;
        }

        settingsFor[static_cast<size_t> (current)] = settings;
        for (auto& e : engines)
            e->setTempo (tempo);

        const int fading = 1 - current;
        for (int start = 0; start < total; start += chunkSize)
        {
            const int n = juce::jmin (chunkSize, total - start);
            routeEvents (midi, start, n);

            // A fresh engine learns where the pedals are before its first note.
            if (freshEngine)
            {
                auto& first = midiFor[static_cast<size_t> (current)];
                if (sustainValue > 0)
                    first.addEvent (juce::MidiMessage::controllerEvent (1, 64, sustainValue), 0);
                if (expressionValue >= 0)
                    first.addEvent (juce::MidiMessage::controllerEvent (1, 11, expressionValue), 0);
                freshEngine = false;
            }

            juce::AudioBuffer<float> view (output.getArrayOfWritePointers(), channels, start, n);
            engines[static_cast<size_t> (current)]->process (view, midiFor[static_cast<size_t> (current)],
                                                             settingsFor[static_cast<size_t> (current)]);

            if (fadingActive)
            {
                auto& old = *engines[static_cast<size_t> (fading)];
                juce::AudioBuffer<float> tail (scratch.getArrayOfWritePointers(), channels, 0, n);
                old.process (tail, midiFor[static_cast<size_t> (fading)], settingsFor[static_cast<size_t> (fading)]);
                for (int ch = 0; ch < channels; ++ch)
                    view.addFrom (ch, 0, tail, ch, 0, n);

                // Finished once nothing is held there and every tail has died away.
                bool holding = false;
                for (auto owner : noteOwner)
                    holding = holding || owner == fading;
                if (! holding && ! old.isActive())
                    fadingActive = false;
            }
        }
    }
}
