#pragma once

#include "engine/NorgEngine.h"
#include "params/Parameters.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

namespace norg
{
    class NorgProcessor final : public juce::AudioProcessor
    {
    public:
        // Bumped whenever the saved-state format changes; older states are upgraded on load.
        static constexpr int stateSchemaVersion = 1;

        NorgProcessor();
        ~NorgProcessor() override;

        void prepareToPlay (double sampleRate, int samplesPerBlock) override;
        void releaseResources() override;
        bool isBusesLayoutSupported (const BusesLayout&) const override;
        void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
        using AudioProcessor::processBlock;

        juce::AudioProcessorEditor* createEditor() override;
        bool hasEditor() const override { return true; }

        const juce::String getName() const override { return JucePlugin_Name; }
        bool acceptsMidi() const override { return true; }
        bool producesMidi() const override { return false; }
        bool isMidiEffect() const override { return false; }
        double getTailLengthSeconds() const override { return 2.0; }

        int getNumPrograms() override { return 1; }
        int getCurrentProgram() override { return 0; }
        void setCurrentProgram (int) override {}
        const juce::String getProgramName (int) override { return "Norg"; }
        void changeProgramName (int, const juce::String&) override {}

        void getStateInformation (juce::MemoryBlock& destData) override;
        void setStateInformation (const void* data, int sizeInBytes) override;

        juce::AudioProcessorValueTreeState& state() { return parameters; }

        // The on-screen keyboard feeds notes in through this.
        juce::MidiKeyboardState& keyboardState() { return keyboard; }

    private:
        juce::AudioProcessorValueTreeState parameters;
        ParamTable paramTable;
        ParamSnapshot snapshot;
        NorgEngine engine { 0 };
        juce::MidiKeyboardState keyboard;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NorgProcessor)
    };
}
