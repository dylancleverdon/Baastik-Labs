#include "MasterClock.h"

namespace norg
{
    void MasterClock::advance (juce::AudioPlayHead* playHead, bool followHost, double internalBpm, int numSamples)
    {
        double newTempo = internalBpm;
        std::optional<double> hostBeat;
        hostTempo = false;

        if (followHost && playHead != nullptr)
            if (const auto position = playHead->getPosition())
            {
                if (const auto hostBpm = position->getBpm(); hostBpm.hasValue() && *hostBpm > 0.0)
                {
                    newTempo = *hostBpm;
                    hostTempo = true;
                }

                if (position->getIsPlaying())
                    if (const auto ppq = position->getPpqPosition())
                        hostBeat = *ppq;
            }

        tempo = juce::jlimit (20.0, 400.0, newTempo);
        beatAtBlockStart = hostBeat.value_or (nextBeat);
        nextBeat = beatAtBlockStart + static_cast<double> (numSamples) / sampleRate * tempo / 60.0;
    }

    std::optional<double> TapTempo::tap (double now)
    {
        if (count > 0 && (now - taps[static_cast<size_t> (count - 1)] > 2.0 || now <= taps[static_cast<size_t> (count - 1)]))
            count = 0;

        if (count == static_cast<int> (taps.size()))
        {
            std::rotate (taps.begin(), taps.begin() + 1, taps.end());
            --count;
        }
        taps[static_cast<size_t> (count++)] = now;

        if (count < 2)
            return std::nullopt;

        const double averageInterval = (taps[static_cast<size_t> (count - 1)] - taps[0]) / (count - 1);
        return juce::jlimit (40.0, 240.0, 60.0 / averageInterval);
    }
}
