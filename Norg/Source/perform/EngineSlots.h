#pragma once

#include "engine/NorgEngine.h"

#include <array>
#include <memory>

namespace norg::perform
{
    // Seamless program changes for one panel. Two engines take turns: when the sound changes,
    // the engine that was playing keeps its held notes, sustain and effect tails with its old
    // settings, while new notes go to a fresh engine with the new ones. A note's release always
    // goes to the engine that started it.
    class EngineSlots
    {
    public:
        EngineSlots (int panel, sfz::LibraryManager*);

        void prepare (double sampleRate, int maxBlockSize);
        void reset();

        // Call from the audio thread when a new program has been loaded, before process().
        void switchSound() { switchPending = true; }

        // Renders one block (replacing `output`) from `settings`, the current sound.
        void process (juce::AudioBuffer<float>& output, const juce::MidiBuffer&, const ParamSnapshot& settings, double tempo);

        bool isFading() const { return fadingActive; }
        bool isActive() const;

    private:
        void doSwitch();
        void routeEvents (const juce::MidiBuffer&, int start, int length);

        static constexpr int chunkSize = 1024;

        std::array<std::unique_ptr<NorgEngine>, 2> engines;
        std::array<ParamSnapshot, 2> settingsFor;
        std::array<juce::MidiBuffer, 2> midiFor;
        std::array<int, 128> noteOwner {};
        juce::AudioBuffer<float> scratch;
        int current = 0;
        bool switchPending = false, fadingActive = false;

        // Pedal state, handed to a fresh engine so held pedals keep working across a switch.
        int sustainValue = 0, expressionValue = -1;
    };
}
