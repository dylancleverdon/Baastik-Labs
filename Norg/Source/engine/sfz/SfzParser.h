#pragma once

#include <juce_core/juce_core.h>

#include <vector>

namespace norg::sfz
{
    enum class Trigger { attack, release, first, legato };
    enum class LoopMode { none, oneShot, continuous, sustain };

    // One <region> after <control>/<global>/<master>/<group> inheritance has been applied.
    struct Region
    {
        juce::String sample;          // absolute path, forward slashes
        int loKey = 0, hiKey = 127, keyCenter = 60;
        int loVel = 1, hiVel = 127;
        float tuneCents = 0.0f;
        float transpose = 0.0f;
        float volumeDb = 0.0f;
        float pan = 0.0f;             // -100 .. 100
        float ampVeltrack = 100.0f;   // percent
        float ampegAttack = 0.0f;     // seconds
        float ampegRelease = 0.001f;  // seconds (SFZ default)
        Trigger trigger = Trigger::attack;
        float rtDecay = 0.0f;         // dB per second of note length, for release samples
        juce::int64 offset = 0;
        juce::int64 group = 0, offBy = 0;
        LoopMode loopMode = LoopMode::none;
        juce::int64 loopStart = 0, loopEnd = 0;
        float loRand = 0.0f, hiRand = 1.0f;   // random round-robin window
        int swLast = -1;                      // keyswitch this region belongs to (-1 = any)
        int notePolyphony = 0;                // 0 = unlimited
        float offTime = 0.006f;               // fade when choked by note_polyphony / off_by

        int sourceIndex = -1;         // filled in when the samples are loaded

        bool matches (int note, int velocity) const
        {
            return loKey >= 0 && note >= loKey && note <= hiKey && velocity >= loVel && velocity <= hiVel;
        }
    };

    struct ParseResult
    {
        std::vector<Region> regions;
        int swLoKey = -1, swHiKey = -1, swDefault = -1; // keyswitch range and starting switch
        juce::StringArray warnings;
    };

    // "c4" = 60, "C#4"/"db4" = 61, or plain numbers. Returns -1 if it isn't a note.
    int parseNoteName (const juce::String& text);

    // Parses SFZ text. `sfzFile` locates relative sample paths and #include files.
    ParseResult parse (const juce::String& text, const juce::File& sfzFile);
    ParseResult parseFile (const juce::File& sfzFile);
}
