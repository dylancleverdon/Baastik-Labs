#include "SampleLibrary.h"

namespace norg::sfz
{
    std::shared_ptr<Instrument> Instrument::load (const juce::File& sfz, const std::atomic<bool>& cancel, juce::String& error,
                                                  std::atomic<float>* progress)
    {
        if (! sfz.existsAsFile())
        {
            error = "not found: " + sfz.getFullPathName();
            return nullptr;
        }

        auto parsed = parseFile (sfz);
        if (parsed.regions.empty())
        {
            error = "no playable regions in " + sfz.getFileName();
            return nullptr;
        }

        auto inst = std::make_shared<Instrument>();
        inst->file = sfz;
        inst->name = sfz.getFileNameWithoutExtension();
        inst->warnings = parsed.warnings;
        inst->swLoKey = parsed.swLoKey;
        inst->swHiKey = parsed.swHiKey;
        inst->swDefault = parsed.swDefault;

        std::map<juce::String, int> sourceIndex;
        size_t done = 0;
        for (auto& region : parsed.regions)
        {
            if (cancel.load())
                return nullptr;
            if (progress != nullptr)
                progress->store (static_cast<float> (done++) / static_cast<float> (parsed.regions.size()));

            auto it = sourceIndex.find (region.sample);
            if (it == sourceIndex.end())
            {
                juce::String sourceError;
                auto source = SampleSource::open (juce::File (region.sample), sourceError);
                if (source == nullptr)
                {
                    inst->warnings.add (sourceError);
                    sourceIndex[region.sample] = -1;
                    continue;
                }
                source->preload (1.0);
                inst->sources.push_back (std::move (source));
                it = sourceIndex.emplace (region.sample, static_cast<int> (inst->sources.size()) - 1).first;
            }

            region.sourceIndex = it->second;
            if (region.sourceIndex >= 0)
                inst->regions.push_back (region);
        }

        if (inst->regions.empty())
        {
            error = "none of the samples in " + sfz.getFileName() + " could be opened";
            return nullptr;
        }

        return inst;
    }

    //==============================================================================
    LibraryManager::LibraryManager() : juce::Thread ("Norg sample loader")
    {
        startThread (juce::Thread::Priority::background);
        startTimer (5000);
    }

    LibraryManager::~LibraryManager()
    {
        stopTimer();
        cancelLoad = true;
        signalThreadShouldExit();
        wake.signal();
        stopThread (10000);
    }

    juce::File LibraryManager::librariesFolder()
    {
       #if JUCE_MAC
        return juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Library/Application Support/Norg/Samples");
       #else
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Norg/Samples");
       #endif
    }

    juce::Array<juce::File> LibraryManager::findInstalledLibraries()
    {
        juce::Array<juce::File> found;
        const auto folder = librariesFolder();
        if (folder.isDirectory())
            for (const auto& entry : juce::RangedDirectoryIterator (folder, true, "*.sfz", juce::File::findFiles))
                found.add (entry.getFile());
        found.sort();
        return found;
    }

    void LibraryManager::request (int slot, const juce::String& path)
    {
        if (slot < 0 || slot >= numSlots)
            return;

        {
            const juce::ScopedLock sl (statusLock);
            if (wanted[static_cast<size_t> (slot)] == path && statuses[static_cast<size_t> (slot)].state != State::failed)
                return;
            wanted[static_cast<size_t> (slot)] = path;
            auto& s = statuses[static_cast<size_t> (slot)];
            s.path = path;
            s.error.clear();
            s.state = path.isEmpty() ? State::empty : State::loading;
        }

        if (path.isEmpty())
        {
            const juce::SpinLock::ScopedLockType sl (currentLock);
            current[static_cast<size_t> (slot)].reset(); // the cache still owns it, so nothing is freed here
            return;
        }

        wake.signal();
    }

    bool LibraryManager::tryGet (int slot, std::shared_ptr<const Instrument>& out)
    {
        const juce::SpinLock::ScopedTryLockType sl (currentLock);
        if (! sl.isLocked())
            return false;
        out = current[static_cast<size_t> (slot)];
        return true;
    }

    LibraryManager::Status LibraryManager::status (int slot) const
    {
        const juce::ScopedLock sl (statusLock);
        auto s = statuses[static_cast<size_t> (juce::jlimit (0, numSlots - 1, slot))];
        if (s.state == State::loading)
            s.progress = loadProgress.load();
        return s;
    }

    bool LibraryManager::waitUntilIdle (int timeoutMs)
    {
        const auto end = juce::Time::getMillisecondCounter() + static_cast<juce::uint32> (timeoutMs);
        const auto busy = [this]
        {
            const juce::ScopedLock sl (statusLock);
            for (const auto& s : statuses)
                if (s.state == State::loading)
                    return true;
            return false;
        };

        while (busy())
        {
            if (juce::Time::getMillisecondCounter() > end)
                return false;
            juce::Thread::sleep (5);
        }
        return true;
    }

    void LibraryManager::run()
    {
        while (! threadShouldExit())
        {
            wake.wait (500);

            for (int slot = 0; slot < numSlots && ! threadShouldExit(); ++slot)
            {
                juce::String path;
                {
                    const juce::ScopedLock sl (statusLock);
                    if (statuses[static_cast<size_t> (slot)].state != State::loading)
                        continue;
                    path = wanted[static_cast<size_t> (slot)];
                }

                std::shared_ptr<const Instrument> instrument;
                juce::String error;

                {
                    const juce::ScopedLock sl (statusLock);
                    if (auto it = cache.find (path); it != cache.end())
                        instrument = it->second;
                }

                if (instrument == nullptr)
                {
                    cancelLoad = false;
                    loadProgress = 0.0f;
                    instrument = Instrument::load (juce::File (path), cancelLoad, error, &loadProgress);
                    if (instrument != nullptr)
                    {
                        const juce::ScopedLock sl (statusLock);
                        cache[path] = instrument;
                    }
                }

                {
                    const juce::ScopedLock sl (statusLock);
                    auto& s = statuses[static_cast<size_t> (slot)];
                    if (wanted[static_cast<size_t> (slot)] != path)
                    {
                        wake.signal(); // superseded while loading: go round again for the newer request
                        continue;
                    }

                    if (instrument != nullptr)
                    {
                        s.state = State::ready;
                        s.name = instrument->name;
                    }
                    else
                    {
                        s.state = State::failed;
                        s.error = error;
                    }
                }

                {
                    const juce::SpinLock::ScopedLockType sl (currentLock);
                    current[static_cast<size_t> (slot)] = instrument;
                }
            }
        }
    }

    void LibraryManager::timerCallback()
    {
        // Free libraries nobody uses any more: not current in any slot and not held by a voice.
        std::vector<std::shared_ptr<const Instrument>> doomed;
        {
            const juce::ScopedLock sl (statusLock);
            for (auto it = cache.begin(); it != cache.end();)
            {
                bool inUse = false;
                for (const auto& w : wanted)
                    inUse = inUse || w == it->first;

                if (! inUse && it->second.use_count() == 1)
                {
                    doomed.push_back (std::move (it->second));
                    it = cache.erase (it);
                }
                else
                {
                    ++it;
                }
            }
        }
        doomed.clear(); // freed here, on the message thread
    }
}
