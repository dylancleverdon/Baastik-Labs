#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <optional>

namespace norg
{
    namespace
    {
        // Copy and paste works across every Norg in the same DAW.
        struct Clipboard
        {
            perform::Program program;
            std::optional<perform::Part> part;
        };

        Clipboard& clipboard()
        {
            static Clipboard c;
            return c;
        }

        constexpr int timerHz = 20;
        constexpr size_t maxUndo = 50;
    }

    NorgProcessor::NorgProcessor()
        : AudioProcessor (BusesProperties().withOutput ("Main", juce::AudioChannelSet::stereo(), true)),
          parameters (*this, nullptr, "NorgState", createParameterLayout()),
          paramTable (parameters)
    {
        snapshot.resetToDefaults();
        captureScratch.resetToDefaults();
        for (auto* param : getParameters())
            param->addListener (this);

        // A new Norg starts on the first program, like the hardware at power-up. (A DAW project
        // being reopened then restores its own sound through setStateInformation.)
        loadProgram (perform::Location::program (0, 0, 0));
        seenGeneration = programGeneration.load();

        startTimerHz (timerHz);
    }

    NorgProcessor::~NorgProcessor()
    {
        stopTimer();
        flushLiveEdits();
        for (auto* param : getParameters())
            param->removeListener (this);
    }

    //==============================================================================
    void NorgProcessor::timerCallback()
    {
        ++timerTicks;

        processPendingProgramChange();

        // Live mode keeps every tweak: save the edit a moment after it's made.
        if (isLiveMode() && modified.load())
        {
            if (++liveSaveTicks >= timerHz * 3 / 2)
            {
                flushLiveEdits();
                liveSaveTicks = 0;
            }
        }
        else
            liveSaveTicks = 0;

        // Another Norg (say, the standalone app) may have stored programs.
        if (timerTicks % (timerHz * 2) == 0 && library->reloadIfChangedOnDisk())
            sendChangeMessage();

        if (const bool m = modified.load(); m != lastModifiedSent)
        {
            lastModifiedSent = m;
            sendChangeMessage();
        }

        // A slot left on "automatic" with nothing loaded yet: the background helper may have just
        // finished installing a library, so look again.
        if (timerTicks % (timerHz * 3) == 0)
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

    void NorgProcessor::parameterValueChanged (int, float)
    {
        // Any thread (host automation arrives on the audio thread): only flag the edit.
        if (! applying.load())
            modified.store (true);
    }

    void NorgProcessor::parameterGestureChanged (int, bool starting)
    {
        if (! starting || ! juce::MessageManager::existsAndIsCurrentThread())
            return;

        if (comparing)
        {
            // Editing while comparing edits the stored version; the earlier edit is let go.
            comparing = false;
            sendChangeMessage();
        }
        pushUndo();
    }

    //==============================================================================
    perform::Location NorgProcessor::currentLocation() const
    {
        const juce::ScopedLock lock (metaLock);
        return location;
    }

    juce::String NorgProcessor::currentProgramName() const
    {
        const juce::ScopedLock lock (metaLock);
        return programName.isEmpty() ? juce::String ("Init") : programName;
    }

    void NorgProcessor::applySound (const perform::Program& program, bool seamless)
    {
        applying.store (true);
        if (seamless)
            programLoading.store (true);

        perform::apply (program, parameters);

        if (seamless)
        {
            programGeneration.fetch_add (1);
            programLoading.store (false);
        }
        applying.store (false);
        syncLibraries();
    }

    void NorgProcessor::loadProgram (const perform::Location& target)
    {
        if (! target.isValid())
            return;

        flushLiveEdits();
        const auto program = library->get (target);
        applySound (program, true);

        {
            const juce::ScopedLock lock (metaLock);
            location = target;
            if (! target.live)
                lastBankLocation = target;
            programName = program.getName();
        }

        modified.store (false);
        comparing = false;
        undoStack.clear();
        redoStack.clear();
        sendChangeMessage();
    }

    void NorgProcessor::storeProgram (const perform::Location& target, const juce::String& name)
    {
        if (! target.isValid())
            return;

        auto program = perform::capture (parameters);
        program.setName (name);
        library->store (target, program);

        {
            const juce::ScopedLock lock (metaLock);
            location = target;
            if (! target.live)
                lastBankLocation = target;
            programName = name;
        }

        modified.store (false);
        comparing = false;
        sendChangeMessage();
    }

    void NorgProcessor::setLiveMode (bool shouldBeLive)
    {
        if (shouldBeLive == isLiveMode())
            return;

        if (shouldBeLive)
            loadProgram (perform::Location::liveSlot (0));
        else
        {
            perform::Location back;
            {
                const juce::ScopedLock lock (metaLock);
                back = lastBankLocation;
            }
            loadProgram (back);
        }
    }

    void NorgProcessor::flushLiveEdits()
    {
        const auto where = currentLocation();
        if (! where.live || ! modified.load())
            return;

        auto program = perform::capture (parameters);
        program.setName (currentProgramName() == "Init" ? juce::String() : currentProgramName());
        library->store (where, program);
        modified.store (false);
    }

    void NorgProcessor::processPendingProgramChange()
    {
        const int bankAndProgram = pendingProgram.exchange (-1);
        if (bankAndProgram < 0)
            return;

        // Bank Select (CC0) picks bank A-H; programs 0-49 are the bank's pages x programs.
        const int bank = juce::jlimit (0, perform::Location::banks - 1, bankAndProgram / 128);
        const int number = bankAndProgram % 128;

        if (isLiveMode() && number < perform::Location::liveSlots)
            loadProgram (perform::Location::liveSlot (number));
        else if (number < perform::Location::programsPerBank)
            loadProgram (perform::Location::program (bank, number / perform::Location::slots, number % perform::Location::slots));
    }

    //==============================================================================
    void NorgProcessor::pushUndo()
    {
        undoStack.push_back (perform::capture (parameters));
        if (undoStack.size() > maxUndo)
            undoStack.erase (undoStack.begin());
        redoStack.clear();
    }

    void NorgProcessor::undo()
    {
        if (undoStack.empty())
            return;
        redoStack.push_back (perform::capture (parameters));
        const auto previous = undoStack.back();
        undoStack.pop_back();
        applySound (previous, false);
        modified.store (true);
        sendChangeMessage();
    }

    void NorgProcessor::redo()
    {
        if (redoStack.empty())
            return;
        undoStack.push_back (perform::capture (parameters));
        const auto next = redoStack.back();
        redoStack.pop_back();
        applySound (next, false);
        modified.store (true);
        sendChangeMessage();
    }

    void NorgProcessor::toggleCompare()
    {
        if (! comparing)
        {
            if (! modified.load())
                return;
            compareStash = perform::capture (parameters);
            applySound (library->get (currentLocation()), false);
            comparing = true;
        }
        else
        {
            applySound (compareStash, false);
            comparing = false;
        }
        sendChangeMessage();
    }

    void NorgProcessor::initSound()
    {
        pushUndo();
        applySound (perform::Program(), false);
        modified.store (true);
        sendChangeMessage();
    }

    void NorgProcessor::copyPart (perform::Part part)
    {
        clipboard().program = perform::capture (parameters);
        clipboard().part = part;
    }

    bool NorgProcessor::canPaste (perform::Part part) const
    {
        return clipboard().part == part;
    }

    void NorgProcessor::pastePart (perform::Part part)
    {
        if (! canPaste (part))
            return;
        pushUndo();
        auto sound = perform::capture (parameters);
        sound.copyPart (part, clipboard().program, 0, 0);
        applySound (sound, false);
        modified.store (true);
        sendChangeMessage();
    }

    //==============================================================================
    void NorgProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
    {
        panelA.prepare (sampleRate, samplesPerBlock);
        clock.prepare (sampleRate);
        limiter.prepare (sampleRate);
        keyboard.reset();
    }

    void NorgProcessor::releaseResources()
    {
        panelA.reset();
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

        // Program changes and bank select: the message thread loads the program.
        for (const auto metadata : midi)
        {
            const auto m = metadata.getMessage();
            if (m.isControllerOfType (0))
                bankSelect = m.getControllerValue();
            else if (m.isProgramChange())
                pendingProgram.store (bankSelect * 128 + m.getProgramChangeNumber());
        }

        // Take this block's settings, unless a program is halfway through loading (then the old
        // settings carry on for another block). A newly loaded program starts a seamless switch.
        const int generation = programGeneration.load();
        if (! programLoading.load())
        {
            paramTable.capture (captureScratch);
            if (! programLoading.load() && programGeneration.load() == generation)
            {
                snapshot = captureScratch;
                if (generation != seenGeneration)
                {
                    seenGeneration = generation;
                    panelA.switchSound();
                }
            }
        }

        clock.advance (getPlayHead(), snapshot.getBool (P::clockHostSync), snapshot.get (P::clockBpm), buffer.getNumSamples());
        panelA.process (buffer, midi, snapshot, clock.bpm());

        // A safety limiter on the output; it only acts on peaks near full scale.
        limiter.process (buffer.getWritePointer (0), buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr,
                         buffer.getNumSamples());

        uiTempo.store (clock.bpm());
        uiBeat.store (clock.beat());
        uiHostTempo.store (clock.followingHost());
    }

    juce::AudioProcessorEditor* NorgProcessor::createEditor()
    {
        return new NorgEditor (*this);
    }

    void NorgProcessor::getStateInformation (juce::MemoryBlock& destData)
    {
        auto state = parameters.copyState();
        state.setProperty ("schemaVersion", stateSchemaVersion, nullptr);
        {
            const juce::ScopedLock lock (metaLock);
            state.setProperty ("prog_live", location.live, nullptr);
            state.setProperty ("prog_bank", location.bank, nullptr);
            state.setProperty ("prog_page", location.page, nullptr);
            state.setProperty ("prog_slot", location.slot, nullptr);
            state.setProperty ("prog_name", programName, nullptr);
        }
        state.setProperty ("prog_modified", modified.load(), nullptr);
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

        // The project's own sound comes back exactly as saved, along with where it came from.
        applying.store (true);
        parameters.replaceState (state);
        applying.store (false);

        perform::Location restored { static_cast<bool> (state.getProperty ("prog_live", false)),
                                     state.getProperty ("prog_bank", 0), state.getProperty ("prog_page", 0),
                                     state.getProperty ("prog_slot", 0) };
        if (! restored.isValid())
            restored = {};
        {
            const juce::ScopedLock lock (metaLock);
            location = restored;
            if (! restored.live)
                lastBankLocation = restored;
            programName = state.getProperty ("prog_name").toString();
        }
        modified.store (static_cast<bool> (state.getProperty ("prog_modified", false)));
        comparing = false;
        undoStack.clear();
        redoStack.clear();

        syncLibraries();
        sendChangeMessage(); // thread-safe: the UI hears about it on the message thread
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
