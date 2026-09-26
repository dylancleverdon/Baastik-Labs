#pragma once

#include "Program.h"

#include <optional>
#include <vector>

namespace norg::perform
{
    // Where a program lives: Bank A-H, Page 1-10, Program 1-5, or one of the 5 Live slots.
    struct Location
    {
        static constexpr int banks = 8, pages = 10, slots = 5, liveSlots = 5;
        static constexpr int programsPerBank = pages * slots;
        static constexpr int numPrograms = banks * programsPerBank;

        bool live = false;
        int bank = 0, page = 0, slot = 0; // slot is also the Live slot index

        static Location program (int bank, int page, int slot) { return { false, bank, page, slot }; }
        static Location liveSlot (int slot) { return { true, 0, 0, slot }; }
        static Location fromIndex (int index); // 0..numPrograms-1
        static std::optional<Location> fromLabel (const juce::String&); // "A:1:3", "Live 3"

        int index() const { return (bank * pages + page) * slots + slot; }
        juce::String label() const; // "A:1:3" or "Live 3"
        bool isValid() const;

        bool operator== (const Location& o) const
        {
            return live == o.live && slot == o.slot && (live || (bank == o.bank && page == o.page));
        }
        bool operator!= (const Location& o) const { return ! operator== (o); }
    };

    // The shared program memory: 400 programs and 5 Live slots, kept in one file in the user's
    // Norg folder so every Norg (each DAW project, the standalone app) sees the same programs.
    // Message thread only.
    class ProgramLibrary
    {
    public:
        ProgramLibrary(); // the user's library file (created with the factory programs)
        explicit ProgramLibrary (juce::File file);

        static juce::File defaultFile();

        Program get (const Location&) const;
        void store (const Location&, const Program&);
        void rename (const Location&, const juce::String& name);

        // Picks up changes another Norg saved (e.g. the standalone app while a DAW is open).
        bool reloadIfChangedOnDisk();

        juce::File getFile() const { return file; }

        // The programs a brand-new library starts with.
        static std::vector<std::pair<Location, Program>> factoryPrograms();

    private:
        void load();
        void save();
        juce::ValueTree& slotTree (const Location&);

        juce::File file;
        juce::Time loadedTime;
        std::vector<juce::ValueTree> programs, live;
    };
}
