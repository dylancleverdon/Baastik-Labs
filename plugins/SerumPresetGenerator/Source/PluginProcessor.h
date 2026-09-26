#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <baastik_updater/baastik_updater.h>

#include "ContentManager.h"
#include "serumgen/Generator.h"

// One generated (or loaded) preset in the session history.
struct HistoryEntry
{
    juce::File file;
    juce::String name;
    juce::String description; // category / genre / recipe
    juce::StringArray summary;
};

// The generator doesn't touch audio; the processor passes sound through and
// holds the generator settings, history and update machinery for the editor.
class SerumPresetGeneratorProcessor : public juce::AudioProcessor
{
public:
    SerumPresetGeneratorProcessor();
    ~SerumPresetGeneratorProcessor() override = default;

    // ---------------------------------------------------------------- audio
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    using AudioProcessor::processBlock;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}

    // --------------------------------------------------------------- plugin
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ------------------------------------------------------------ generator
    serumgen::Settings settings;
    int batchCount = 1;
    bool randomizeSeed = true;
    juce::File outputFolder;

    // Generates presets, writes them to outputFolder and adds them to the
    // history. Returns an error message, or empty on success.
    juce::String generate();

    // Loads a preset file as the base for locks and Mutate mode.
    juce::String loadBase (const juce::File& file);
    const HistoryEntry* selected() const;
    void select (int index);
    int selectedIndex() const { return selected_; }
    const std::vector<HistoryEntry>& history() const { return history_; }
    void removeFromHistory (int index, bool deleteFile);

    static juce::File defaultOutputFolder();

    // ------------------------------------------------------------- updates
    ContentManager content;
    baastik::UpdateChecker updater;

    std::function<void()> onHistoryChanged;

private:
    std::optional<serum2::Preset> basePreset() const;

    std::vector<HistoryEntry> history_;
    int selected_ = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SerumPresetGeneratorProcessor)
};
