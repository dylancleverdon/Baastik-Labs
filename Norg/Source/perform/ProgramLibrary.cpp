#include "ProgramLibrary.h"

namespace norg::perform
{
    namespace
    {
        const juce::Identifier libraryType ("NorgPrograms"), liveType ("Live"), versionId ("version"), indexId ("index");

        // The slot number is only needed in the file; in memory a program is just its sound.
        juce::ValueTree withoutIndex (const juce::ValueTree& stored)
        {
            auto tree = Program (stored).toValueTree();
            tree.removeProperty (indexId, nullptr);
            return tree;
        }
    }

    //==============================================================================
    Location Location::fromIndex (int index)
    {
        index = juce::jlimit (0, numPrograms - 1, index);
        return program (index / programsPerBank, (index / slots) % pages, index % slots);
    }

    std::optional<Location> Location::fromLabel (const juce::String& text)
    {
        const auto t = text.trim().toUpperCase();
        if (t.startsWith ("LIVE"))
        {
            const auto loc = liveSlot (t.fromFirstOccurrenceOf ("LIVE", false, false).trim().getIntValue() - 1);
            return loc.isValid() ? std::optional<Location> (loc) : std::nullopt;
        }

        const auto parts = juce::StringArray::fromTokens (t, ":", {});
        if (parts.size() != 3 || parts[0].length() != 1)
            return std::nullopt;
        const auto loc = program (parts[0][0] - 'A', parts[1].getIntValue() - 1, parts[2].getIntValue() - 1);
        return loc.isValid() ? std::optional<Location> (loc) : std::nullopt;
    }

    juce::String Location::label() const
    {
        if (live)
            return "Live " + juce::String (slot + 1);
        return juce::String::charToString (static_cast<juce::juce_wchar> ('A' + bank)) + ":" + juce::String (page + 1) + ":" + juce::String (slot + 1);
    }

    bool Location::isValid() const
    {
        if (live)
            return juce::isPositiveAndBelow (slot, liveSlots);
        return juce::isPositiveAndBelow (bank, banks) && juce::isPositiveAndBelow (page, pages) && juce::isPositiveAndBelow (slot, slots);
    }

    //==============================================================================
    juce::File ProgramLibrary::defaultFile()
    {
        if (const auto overridden = juce::SystemStats::getEnvironmentVariable ("NORG_PROGRAM_LIBRARY", {}); overridden.isNotEmpty())
            return juce::File (overridden); // tests

       #if JUCE_MAC
        return juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Library/Application Support/Norg/Programs.norglib");
       #else
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Norg/Programs.norglib");
       #endif
    }

    ProgramLibrary::ProgramLibrary() : ProgramLibrary (defaultFile()) {}

    ProgramLibrary::ProgramLibrary (juce::File f) : file (std::move (f))
    {
        load();
    }

    void ProgramLibrary::load()
    {
        programs.assign (static_cast<size_t> (Location::numPrograms), Program().toValueTree());
        live.assign (static_cast<size_t> (Location::liveSlots), Program().toValueTree());

        std::unique_ptr<juce::XmlElement> xml;
        if (file.existsAsFile())
            xml = juce::parseXML (file);

        if (xml == nullptr || ! xml->hasTagName (libraryType.toString()))
        {
            // First run (or an unreadable file): start with the factory programs.
            for (auto& [location, program] : factoryPrograms())
                slotTree (location) = program.toValueTree();
            if (! file.existsAsFile())
                save();
            return;
        }

        const auto tree = juce::ValueTree::fromXml (*xml);
        for (const auto& child : tree)
        {
            const int index = child.getProperty (indexId, -1);
            if (child.hasType (Program::type) && juce::isPositiveAndBelow (index, Location::numPrograms))
                programs[static_cast<size_t> (index)] = withoutIndex (child);
        }

        for (const auto& child : tree.getChildWithName (liveType))
        {
            const int index = child.getProperty (indexId, -1);
            if (juce::isPositiveAndBelow (index, Location::liveSlots))
                live[static_cast<size_t> (index)] = withoutIndex (child);
        }

        loadedTime = file.getLastModificationTime();
    }

    void ProgramLibrary::save()
    {
        juce::ValueTree tree (libraryType);
        tree.setProperty (versionId, 1, nullptr);

        // Only programs that aren't plain Init are written.
        for (size_t i = 0; i < programs.size(); ++i)
            if (programs[i].getNumProperties() > 0)
            {
                auto child = programs[i].createCopy();
                child.setProperty (indexId, static_cast<int> (i), nullptr);
                tree.appendChild (child, nullptr);
            }

        juce::ValueTree liveTree (liveType);
        for (size_t i = 0; i < live.size(); ++i)
            if (live[i].getNumProperties() > 0)
            {
                auto child = live[i].createCopy();
                child.setProperty (indexId, static_cast<int> (i), nullptr);
                liveTree.appendChild (child, nullptr);
            }
        tree.appendChild (liveTree, nullptr);

        // Write beside the file and swap it in, so a crash never leaves half a library.
        file.getParentDirectory().createDirectory();
        const auto temp = file.getSiblingFile (file.getFileName() + ".saving");
        if (auto xml = tree.createXml(); xml != nullptr && xml->writeTo (temp))
            temp.moveFileTo (file);
        loadedTime = file.getLastModificationTime();
    }

    bool ProgramLibrary::reloadIfChangedOnDisk()
    {
        if (! file.existsAsFile() || file.getLastModificationTime() == loadedTime)
            return false;
        load();
        return true;
    }

    juce::ValueTree& ProgramLibrary::slotTree (const Location& location)
    {
        jassert (location.isValid());
        return location.live ? live[static_cast<size_t> (juce::jlimit (0, Location::liveSlots - 1, location.slot))]
                             : programs[static_cast<size_t> (juce::jlimit (0, Location::numPrograms - 1, location.index()))];
    }

    Program ProgramLibrary::get (const Location& location) const
    {
        return Program (const_cast<ProgramLibrary*> (this)->slotTree (location));
    }

    void ProgramLibrary::store (const Location& location, const Program& program)
    {
        reloadIfChangedOnDisk(); // don't overwrite what another Norg just saved
        auto stored = program.toValueTree();
        stored.removeProperty (indexId, nullptr);
        slotTree (location) = stored;
        save();
    }

    void ProgramLibrary::rename (const Location& location, const juce::String& name)
    {
        auto program = get (location);
        program.setName (name);
        store (location, program);
    }
}
