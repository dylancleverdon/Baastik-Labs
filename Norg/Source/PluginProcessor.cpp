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
        syncLibraries();
        startTimer (3000);
    }

    NorgProcessor::~NorgProcessor()
    {
        stopTimer();
    }

    void NorgProcessor::timerCallback()
    {
        // A slot left on "automatic" with nothing loaded yet: the background helper may have just
        // finished installing a library, so look again.
        for (int panel = 0; panel < numPanels; ++panel)
            for (auto use : { LibraryUse::grand, LibraryUse::upright })
            {
                const int slot = panel * 3 + static_cast<int> (use);
                if (getLibraryChoice (panel, use).isEmpty()
                    && libraries.status (slot).state == sfz::LibraryManager::State::empty
                    && resolveLibrary (panel, use).isNotEmpty())
                {
                    syncLibraries();
                    return;
                }
            }
    }

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
        syncLibraries();
    }

    namespace
    {
        juce::Identifier libraryProperty (int panel, NorgProcessor::LibraryUse use)
        {
            static const char* names[] = { "lib_grand", "lib_upright", "lib_sample" };
            return (panel == 1 ? juce::String ("b_") : juce::String()) + names[static_cast<int> (use)];
        }
    }

    juce::String NorgProcessor::getLibraryChoice (int panel, LibraryUse use) const
    {
        return parameters.state.getProperty (libraryProperty (panel, use)).toString();
    }

    void NorgProcessor::setLibraryChoice (int panel, LibraryUse use, const juce::String& path)
    {
        parameters.state.setProperty (libraryProperty (panel, use), path, nullptr);
        syncLibraries();
    }

    juce::String NorgProcessor::resolveLibrary (int panel, LibraryUse use) const
    {
        const auto choice = getLibraryChoice (panel, use);
        if (choice == "none")
            return {};
        if (choice.isNotEmpty())
            return choice;
        if (use == LibraryUse::sample)
            return {}; // the Sample section defaults to its built-in tape voices

        // Automatic: the first installed library that looks like the right kind of piano.
        const auto installed = sfz::LibraryManager::findInstalledLibraries();
        const auto* keyword = use == LibraryUse::grand ? "grand" : "upright";
        for (const auto& f : installed)
            if (f.getFullPathName().containsIgnoreCase (keyword))
                return f.getFullPathName();
        return {};
    }

    void NorgProcessor::syncLibraries()
    {
        for (int panel = 0; panel < numPanels; ++panel)
            for (auto use : { LibraryUse::grand, LibraryUse::upright, LibraryUse::sample })
                libraries.request (panel * 3 + static_cast<int> (use), resolveLibrary (panel, use));
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new norg::NorgProcessor();
}
