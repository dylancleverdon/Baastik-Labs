#pragma once

#include "SampleSource.h"
#include "SfzParser.h"

#include <juce_events/juce_events.h>

#include <array>
#include <atomic>
#include <map>
#include <memory>

namespace norg::sfz
{
    // A loaded SFZ instrument: its regions plus the sample data they play.
    struct Instrument
    {
        juce::File file;
        juce::String name;
        std::vector<Region> regions;
        std::vector<std::unique_ptr<SampleSource>> sources;
        int swLoKey = -1, swHiKey = -1, swDefault = -1;
        juce::StringArray warnings;

        const SampleSource* source (const Region& r) const
        {
            return r.sourceIndex >= 0 ? sources[static_cast<size_t> (r.sourceIndex)].get() : nullptr;
        }

        static std::shared_ptr<Instrument> load (const juce::File& sfz, const std::atomic<bool>& cancel, juce::String& error,
                                                 std::atomic<float>* progress = nullptr);
    };

    // Loads sample libraries in the background and hands them to the audio thread without locking
    // it or ever freeing memory there. There is one slot per (panel, grand/upright/sample) use.
    class LibraryManager final : private juce::Thread, private juce::Timer
    {
    public:
        static constexpr int numSlots = 6;
        enum class State { empty, loading, ready, failed };

        struct Status
        {
            State state = State::empty;
            juce::String path, name, error;
            float progress = 0.0f; // while loading, 0..1
        };

        LibraryManager();
        ~LibraryManager() override;

        // Message thread. An empty path clears the slot.
        void request (int slot, const juce::String& path);

        // Audio thread: copies the slot's current instrument if the lock is free; never blocks.
        bool tryGet (int slot, std::shared_ptr<const Instrument>& out);

        Status status (int slot) const;

        // Where downloaded libraries live, and the .sfz files found there.
        static juce::File librariesFolder();
        static juce::Array<juce::File> findInstalledLibraries();

        // Blocks until pending loads have finished (for tests and offline rendering).
        bool waitUntilIdle (int timeoutMs);

    private:
        void run() override;
        void timerCallback() override;

        mutable juce::CriticalSection statusLock;
        juce::SpinLock currentLock;
        std::array<std::shared_ptr<const Instrument>, numSlots> current;
        std::array<Status, numSlots> statuses;
        std::array<juce::String, numSlots> wanted;
        std::map<juce::String, std::shared_ptr<const Instrument>> cache;
        std::atomic<bool> cancelLoad { false };
        std::atomic<float> loadProgress { 0.0f };
        juce::WaitableEvent wake;
    };
}
