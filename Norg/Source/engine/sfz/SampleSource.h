#pragma once

#include <juce_core/juce_core.h>

#include <memory>
#include <vector>

namespace norg::sfz
{
    // Read-only audio for one sample file. WAV files are memory-mapped, so a large library costs
    // address space rather than RAM and the OS pages in what is played. Other formats (FLAC, AIFF,
    // Ogg...) are transcoded once into a 16-bit WAV cache on disk and then mapped the same way; if
    // the cache can't be written they are decoded into memory instead.
    //
    // get() is safe to call from the audio thread.
    class SampleSource
    {
    public:
        static std::unique_ptr<SampleSource> open (const juce::File&, juce::String& error);

        int numChannels() const { return channels; }
        juce::int64 numFrames() const { return frames; }
        double sampleRate() const { return rate; }

        float get (int channel, juce::int64 frame) const
        {
            const auto* p = data + (frame * channels + juce::jmin (channel, channels - 1)) * bytesPerSample;
            switch (format)
            {
                case Format::pcm16: return static_cast<float> (static_cast<int16_t> (p[0] | (p[1] << 8))) * (1.0f / 32768.0f);
                case Format::pcm24:
                {
                    const int32_t v = (p[0] << 8) | (p[1] << 16) | (p[2] << 24);
                    return static_cast<float> (v >> 8) * (1.0f / 8388608.0f);
                }
                case Format::float32:
                {
                    float f;
                    std::memcpy (&f, p, 4);
                    return f;
                }
            }
            return 0.0f;
        }

        // Reads the start of the sample so it is in memory before the first note plays.
        void preload (double seconds) const;

    private:
        enum class Format { pcm16, pcm24, float32 };

        static std::unique_ptr<SampleSource> openWav (const juce::File&, juce::String& error);
        static std::unique_ptr<SampleSource> decode (const juce::File&, juce::String& error);

        Format format = Format::pcm16;
        int channels = 1;
        int bytesPerSample = 2;
        juce::int64 frames = 0;
        double rate = 44100.0;
        const uint8_t* data = nullptr;

        std::unique_ptr<juce::MemoryMappedFile> mapped;
        std::vector<uint8_t> owned;
    };
}
