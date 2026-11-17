#pragma once

#include "Store.h"

namespace tutti::audio
{
    struct Info
    {
        double rate = 0;
        int channels = 0;
        int64_t frames = 0;
        int bits = 0;
    };

    // Reads an audio file's format (WAV, AIFF, FLAC, Ogg, MP3...).
    std::optional<Info> probe (const juce::File& file);

    // Fills in the audio fields of the store's metadata for a file (no-op if not audio).
    void describe (Store&, const std::string& hash);

    // The compressed preview other members get first: Ogg Vorbis at ~128 kbps.
    bool encodePreview (const juce::File& source, const juce::File& oggOut, juce::String& error);

    // Turns a preview back into a WAV with exactly the original rate, channels and length, so
    // anything edited on the preview lines up when the full file replaces it.
    bool decodePreview (const juce::File& ogg, const Info& original, const juce::File& wavOut, juce::String& error);

    // Makes (or reuses) the preview for a stored file and records its hash in the metadata.
    bool ensurePreview (Store&, const std::string& hash, juce::String& error);
}
