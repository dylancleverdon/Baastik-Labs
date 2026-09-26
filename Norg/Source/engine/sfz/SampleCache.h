#pragma once

#include <juce_core/juce_core.h>

#include <functional>

// Compressed samples (FLAC, Ogg, AIFF...) are transcoded once into 16-bit WAVs that can be
// memory-mapped. The background updater prepares downloaded libraries this way, and the plugin
// finds the same cache files, so a freshly installed library opens instantly.
namespace norg::sfz::cache
{
    juce::File folder();

    // Keyed on file name, size and date (not the folder), so moving a library keeps its cache.
    juce::File fileFor (const juce::File& sample);

    bool needsTranscoding (const juce::File& sample);

    // Writes `to` atomically (via a temporary file); safe if two processes race.
    bool transcode (const juce::File& from, const juce::File& to);

    // Transcodes every compressed sample under `root` that isn't cached yet. `progress` gets 0..1
    // and returns false to cancel. Returns false if cancelled.
    bool prepareFolder (const juce::File& root, const std::function<bool (float)>& progress);
}
