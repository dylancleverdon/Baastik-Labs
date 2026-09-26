#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace norg
{
    NorgProcessor::NorgProcessor()
        : AudioProcessor (BusesProperties().withOutput ("Main", juce::AudioChannelSet::stereo(), true)),
          parameters (*this, nullptr, "NorgState", createParameterLayout()),
          paramTable (parameters)
    {
        snapshot.resetToDefaults();
    }

    NorgProcessor::~NorgProcessor() = default;

    void NorgProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
    {
        engine.prepare (sampleRate, samplesPerBlock);
        keyboard.reset();
    }

    void NorgProcessor::releaseResources()
    {
        engine.reset();
    }

    bool NorgProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
    {
        const auto out = layouts.getMainOutputChannelSet();
        return out == juce::AudioChannelSet::stereo();
    }

    void NorgProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
    {
        juce::ScopedNoDenormals noDenormals;

        keyboard.processNextMidiBuffer (midi, 0, buffer.getNumSamples(), true);
        paramTable.capture (snapshot);

        engine.process (buffer, midi, snapshot);
    }

    juce::AudioProcessorEditor* NorgProcessor::createEditor()
    {
        return new NorgEditor (*this);
    }

    void NorgProcessor::getStateInformation (juce::MemoryBlock& destData)
    {
        auto state = parameters.copyState();
        state.setProperty ("schemaVersion", stateSchemaVersion, nullptr);
        if (auto xml = state.createXml())
            copyXmlToBinary (*xml, destData);
    }

    void NorgProcessor::setStateInformation (const void* data, int sizeInBytes)
    {
        const auto xml = getXmlFromBinary (data, sizeInBytes);
        if (xml == nullptr || ! xml->hasTagName (parameters.state.getType()))
            return;

        auto state = juce::ValueTree::fromXml (*xml);
        // Future schema upgrades go here (state.getProperty ("schemaVersion") < stateSchemaVersion).
        parameters.replaceState (state);
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new norg::NorgProcessor();
}
