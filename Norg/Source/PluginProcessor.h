#pragma once

#include "engine/MasterClock.h"
#include "engine/NorgEngine.h"
#include "params/Parameters.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

namespace norg
{
    class NorgProcessor final : public juce::AudioProcessor, private juce::Timer
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
        double getTailLengthSeconds() const override { return 4.0; }

        int getNumPrograms() override { return 1; }
        int getCurrentProgram() override { return 0; }
        void setCurrentProgram (int) override {}
        const juce::String getProgramName (int) override { return "Norg"; }
        void changeProgramName (int, const juce::String&) override {}

        void getStateInformation (juce::MemoryBlock& destData) override;
        void setStateInformation (const void* data, int sizeInBytes) override;

        juce::AudioProcessorValueTreeState& state() { return parameters; }

        // Sample libraries used by each panel. The choice is stored with the program; an empty
        // path means "automatic" (the first suitable installed library), "none" means modelled.
        enum class LibraryUse { grand = 0, upright = 1, sample = 2 };
        juce::String getLibraryChoice (int panel, LibraryUse) const;
        void setLibraryChoice (int panel, LibraryUse, const juce::String& path);
        juce::String resolveLibrary (int panel, LibraryUse) const;
        sfz::LibraryManager& libraryManager() { return libraries; }

        // The on-screen keyboard feeds notes in through this.
        juce::MidiKeyboardState& keyboardState() { return keyboard; }

        // The master clock as of the last block, for the tempo LED and display.
        double clockTempo() const { return uiTempo.load(); }
        double clockBeat() const { return uiBeat.load(); }
        bool clockFollowingHost() const { return uiHostTempo.load(); }

    private:
        void syncLibraries();
        void timerCallback() override; // picks up sample libraries that arrive while Norg is open

        sfz::LibraryManager libraries;
        juce::AudioProcessorValueTreeState parameters;
        ParamTable paramTable;
        ParamSnapshot snapshot;
        NorgEngine engine { 0, &libraries };
        MasterClock clock;
        fx::Limiter limiter;
        juce::MidiKeyboardState keyboard;
        std::atomic<double> uiTempo { 120.0 }, uiBeat { 0.0 };
        std::atomic<bool> uiHostTempo { false };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NorgProcessor)
    };
}
