#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <array>
#include <optional>

namespace norg
{
    // The master clock: follows the host's tempo and beat position when it has them, otherwise
    // free-runs at the panel's own tempo. Drives tempo-synced effects and the tempo LED.
    class MasterClock
    {
    public:
        void prepare (double newSampleRate) { sampleRate = newSampleRate; }

        // Call once at the start of each block.
        void advance (juce::AudioPlayHead*, bool followHost, double internalBpm, int numSamples);

        double bpm() const { return tempo; }
        double beat() const { return beatAtBlockStart; } // in quarter notes
        bool followingHost() const { return hostTempo; }

    private:
        double sampleRate = 44100.0;
        double tempo = 120.0;
        double beatAtBlockStart = 0.0, nextBeat = 0.0;
        bool hostTempo = false;
    };

    // Tap tempo: averages the last few taps; a pause of more than two seconds starts over.
    class TapTempo
    {
    public:
        // Returns the new tempo once there are at least two taps.
        std::optional<double> tap (double nowSeconds);

    private:
        std::array<double, 5> taps {};
        int count = 0;
    };
}
