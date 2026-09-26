#pragma once

#include "engine/MasterClock.h"
#include "engine/NorgEngine.h"
#include "params/Parameters.h"
#include "perform/EngineSlots.h"
#include "perform/ProgramLibrary.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

namespace norg
{
    class NorgProcessor final : public juce::AudioProcessor,
                                public juce::ChangeBroadcaster, // program, name or "modified" changed
                                private juce::Timer,
                                private juce::AudioProcessorParameter::Listener
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
        const juce::String getProgramName (int) override { return currentProgramName(); }
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

        // --- Programs (message thread) ------------------------------------------------------------
        // Program changes are seamless: held notes, pedals and effect tails keep the old sound.
        perform::ProgramLibrary& programLibrary() { return *library; }
        perform::Location currentLocation() const;
        juce::String currentProgramName() const;
        bool isModified() const { return modified.load(); }
        bool isLiveMode() const { return currentLocation().live; }

        void loadProgram (const perform::Location&);
        void storeProgram (const perform::Location&, const juce::String& name);
        void setLiveMode (bool);
        void initSound(); // an edit: every parameter back to its default (undoable)
        perform::Program currentSound() { return perform::capture (parameters); }

        // Undo and redo cover edits since the program was loaded (one step per knob turn or press).
        bool canUndo() const { return ! undoStack.empty(); }
        bool canRedo() const { return ! redoStack.empty(); }
        void undo();
        void redo();

        // Compare: listen to the stored program, then back to the edited one.
        bool isComparing() const { return comparing; }
        void toggleCompare();

        // Copy a part (organ, piano ...) of this sound, and paste it into another program.
        void copyPart (perform::Part);
        bool canPaste (perform::Part) const;
        void pastePart (perform::Part);

        // Loads a program asked for by MIDI Program Change (the timer calls this; tests may too).
        void processPendingProgramChange();

        // The master clock as of the last block, for the tempo LED and display.
        double clockTempo() const { return uiTempo.load(); }
        double clockBeat() const { return uiBeat.load(); }
        bool clockFollowingHost() const { return uiHostTempo.load(); }

    private:
        void syncLibraries();
        void timerCallback() override; // program changes from MIDI, Live autosave, new sample libraries
        void parameterValueChanged (int, float) override;
        void parameterGestureChanged (int, bool starting) override;

        void applySound (const perform::Program&, bool seamless);
        void pushUndo();
        void flushLiveEdits();

        sfz::LibraryManager libraries;
        juce::AudioProcessorValueTreeState parameters;
        ParamTable paramTable;
        ParamSnapshot snapshot;
        ParamSnapshot captureScratch;
        perform::EngineSlots panelA { 0, &libraries };
        MasterClock clock;
        fx::Limiter limiter;
        juce::MidiKeyboardState keyboard;
        std::atomic<double> uiTempo { 120.0 }, uiBeat { 0.0 };
        std::atomic<bool> uiHostTempo { false };

        // Programs. A load bumps the generation once every parameter is set; the audio thread
        // switches engines when it sees a new generation, and never captures a half-loaded sound.
        juce::SharedResourcePointer<perform::ProgramLibrary> library;
        std::atomic<int> programGeneration { 0 };
        std::atomic<bool> programLoading { false }, applying { false }, modified { false };
        std::atomic<int> pendingProgram { -1 };
        int seenGeneration = 0, bankSelect = 0;

        mutable juce::CriticalSection metaLock; // location and name are also read by getStateInformation
        perform::Location location, lastBankLocation;
        juce::String programName;

        std::vector<perform::Program> undoStack, redoStack;
        perform::Program compareStash;
        bool comparing = false, lastModifiedSent = false;
        int timerTicks = 0, liveSaveTicks = 0;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NorgProcessor)
    };
}
