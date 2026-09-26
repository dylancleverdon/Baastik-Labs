#pragma once

#include "params/Parameters.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <initializer_list>

namespace norg::perform
{
    // The parts of a program that can be copied and pasted on their own.
    enum class Part { organ, piano, synth, sample, effects };
    juce::String partName (Part);
    Part partOf (P);

    // One complete sound: every panel parameter of both panels, the mode, the tempo and the
    // sample libraries it uses (global settings such as master level are not part of it).
    //
    // Values are stored by parameter ID and only when they differ from the default, so programs
    // stay small, and a program saved before a parameter existed simply gets its default.
    class Program
    {
    public:
        Program(); // "Init": everything at its default
        explicit Program (const juce::ValueTree& stored);

        static bool isInProgram (P);

        juce::String getName() const { return tree.getProperty (nameId).toString(); }
        void setName (const juce::String& name) { tree.setProperty (nameId, name, nullptr); }
        juce::String displayName() const { return getName().isEmpty() ? juce::String ("Init") : getName(); }

        float get (P, int panel = 0) const;
        void set (P, int panel, float plainValue);
        void set (P p, float plainValue) { set (p, 0, plainValue); }

        // Sample library choices, keyed like the processor's state properties ("lib_grand" ...).
        juce::String getLibrary (const juce::Identifier&) const;
        void setLibrary (const juce::Identifier&, const juce::String& choice);
        static const std::initializer_list<const char*>& libraryKeys();

        // Copies one part (e.g. the organ) from another program, panel to panel.
        void copyPart (Part, const Program& from, int fromPanel, int toPanel);

        bool sameSoundAs (const Program&) const; // ignores the name
        juce::ValueTree toValueTree() const { return tree.createCopy(); }

        static const juce::Identifier type, nameId;

    private:
        juce::ValueTree tree;
    };

    // Reads the sound the processor's parameters currently describe.
    Program capture (juce::AudioProcessorValueTreeState&);

    // Sets every program parameter (message thread). Parameters the program doesn't mention go
    // to their defaults, so nothing from the previous sound leaks through.
    void apply (const Program&, juce::AudioProcessorValueTreeState&);
}
