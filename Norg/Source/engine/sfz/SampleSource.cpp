#include "SampleSource.h"
#include "SampleCache.h"

#include <juce_audio_formats/juce_audio_formats.h>

namespace norg::sfz
{
    namespace
    {
        uint32_t readU32 (const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t> (p[3]) << 24); }
        uint16_t readU16 (const uint8_t* p) { return static_cast<uint16_t> (p[0] | (p[1] << 8)); }
    }

    std::unique_ptr<SampleSource> SampleSource::open (const juce::File& file, juce::String& error)
    {
        if (! file.existsAsFile())
        {
            error = "missing sample " + file.getFullPathName();
            return nullptr;
        }

        if (file.hasFileExtension ("wav;wave"))
            if (auto wav = openWav (file, error))
                return wav;

        // Compressed or unusual formats: map a WAV copy from the cache, making it the first time.
        const auto cached = cache::fileFor (file);
        if (cached.existsAsFile() || cache::transcode (file, cached))
            if (auto wav = openWav (cached, error))
                return wav;

        return decode (file, error);
    }

    std::unique_ptr<SampleSource> SampleSource::openWav (const juce::File& file, juce::String& error)
    {
        auto map = std::make_unique<juce::MemoryMappedFile> (file, juce::MemoryMappedFile::readOnly);
        const auto* bytes = static_cast<const uint8_t*> (map->getData());
        const auto size = static_cast<juce::int64> (map->getSize());

        if (bytes == nullptr || size < 44 || std::memcmp (bytes, "RIFF", 4) != 0 || std::memcmp (bytes + 8, "WAVE", 4) != 0)
            return nullptr; // not a plain RIFF/WAVE: let the decoder try

        int formatTag = 0, channels = 0, bits = 0;
        double rate = 0.0;
        const uint8_t* data = nullptr;
        juce::int64 dataSize = 0;

        juce::int64 pos = 12;
        while (pos + 8 <= size)
        {
            const auto* chunk = bytes + pos;
            const auto chunkSize = static_cast<juce::int64> (readU32 (chunk + 4));
            if (std::memcmp (chunk, "fmt ", 4) == 0 && chunkSize >= 16)
            {
                formatTag = readU16 (chunk + 8);
                channels = readU16 (chunk + 10);
                rate = readU32 (chunk + 12);
                bits = readU16 (chunk + 22);
                if (formatTag == 0xfffe && chunkSize >= 40) // WAVE_FORMAT_EXTENSIBLE: sub-format GUID
                    formatTag = readU16 (chunk + 32);
            }
            else if (std::memcmp (chunk, "data", 4) == 0)
            {
                data = chunk + 8;
                dataSize = juce::jmin (chunkSize, size - (pos + 8));
                break;
            }
            pos += 8 + chunkSize + (chunkSize & 1);
        }

        auto source = std::unique_ptr<SampleSource> (new SampleSource());
        if (formatTag == 1 && bits == 16)      source->format = Format::pcm16;
        else if (formatTag == 1 && bits == 24) source->format = Format::pcm24;
        else if (formatTag == 3 && bits == 32) source->format = Format::float32;
        else
            return nullptr; // other encodings go through the decoder

        if (data == nullptr || channels < 1 || rate <= 0.0)
        {
            error = "unreadable WAV " + file.getFileName();
            return nullptr;
        }

        source->channels = channels;
        source->bytesPerSample = bits / 8;
        source->rate = rate;
        source->frames = dataSize / (channels * source->bytesPerSample);
        source->data = data;
        source->mapped = std::move (map);
        return source;
    }

    std::unique_ptr<SampleSource> SampleSource::decode (const juce::File& file, juce::String& error)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        if (reader == nullptr)
        {
            error = "unsupported sample format " + file.getFileName();
            return nullptr;
        }

        auto source = std::unique_ptr<SampleSource> (new SampleSource());
        source->format = Format::pcm16;
        source->channels = static_cast<int> (juce::jlimit (1u, 2u, reader->numChannels));
        source->bytesPerSample = 2;
        source->rate = reader->sampleRate;
        source->frames = reader->lengthInSamples;
        source->owned.resize (static_cast<size_t> (source->frames * source->channels * 2));

        constexpr int chunk = 16384;
        juce::AudioBuffer<float> buffer (source->channels, chunk);
        for (juce::int64 start = 0; start < source->frames; start += chunk)
        {
            const int n = static_cast<int> (juce::jmin<juce::int64> (chunk, source->frames - start));
            reader->read (&buffer, 0, n, start, true, source->channels > 1);
            for (int i = 0; i < n; ++i)
                for (int ch = 0; ch < source->channels; ++ch)
                {
                    const auto v = static_cast<int16_t> (juce::jlimit (-32768, 32767, juce::roundToInt (buffer.getSample (ch, i) * 32767.0f)));
                    auto* p = source->owned.data() + ((start + i) * source->channels + ch) * 2;
                    p[0] = static_cast<uint8_t> (v & 0xff);
                    p[1] = static_cast<uint8_t> ((v >> 8) & 0xff);
                }
        }

        source->data = source->owned.data();
        return source;
    }

    void SampleSource::preload (double seconds) const
    {
        const auto count = juce::jmin (frames, static_cast<juce::int64> (seconds * rate));
        const auto stride = static_cast<juce::int64> (4096 / juce::jmax (1, channels * bytesPerSample));
        volatile float sink = 0.0f;
        for (juce::int64 f = 0; f < count; f += juce::jmax<juce::int64> (1, stride))
            sink = sink + get (0, f); // touch each page
    }
}
