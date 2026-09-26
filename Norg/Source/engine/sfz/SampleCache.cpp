#include "SampleCache.h"

#include <juce_audio_formats/juce_audio_formats.h>

namespace norg::sfz::cache
{
    juce::File folder()
    {
        if (const auto overridden = juce::SystemStats::getEnvironmentVariable ("NORG_SAMPLE_CACHE", {}); overridden.isNotEmpty())
            return juce::File (overridden); // tests

       #if JUCE_MAC
        return juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Library/Caches/Norg/Samples");
       #else
        return juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile (".cache/norg/samples");
       #endif
    }

    juce::File fileFor (const juce::File& sample)
    {
        const auto key = sample.getFileName() + "|" + juce::String (sample.getSize()) + "|"
                       + juce::String (sample.getLastModificationTime().toMilliseconds() / 1000);
        return folder().getChildFile (juce::String::toHexString (key.hashCode64()) + ".wav");
    }

    bool needsTranscoding (const juce::File& sample)
    {
        return sample.hasFileExtension ("flac;ogg;aif;aiff;aifc");
    }

    bool transcode (const juce::File& from, const juce::File& to)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (from));
        if (reader == nullptr || ! to.getParentDirectory().createDirectory())
            return false;

        const int channels = static_cast<int> (juce::jlimit (1u, 2u, reader->numChannels));
        const auto temp = to.getSiblingFile (to.getFileNameWithoutExtension() + "."
                                             + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64())
                                             + ".partial");

        {
            std::unique_ptr<juce::OutputStream> stream (temp.createOutputStream());
            if (stream == nullptr)
                return false;

            juce::WavAudioFormat wav;
            auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions()
                                                           .withSampleRate (reader->sampleRate)
                                                           .withNumChannels (channels)
                                                           .withBitsPerSample (16));
            if (writer == nullptr)
            {
                temp.deleteFile();
                return false;
            }

            constexpr int chunk = 32768;
            juce::AudioBuffer<float> buffer (channels, chunk);
            for (juce::int64 start = 0; start < reader->lengthInSamples; start += chunk)
            {
                const int n = static_cast<int> (juce::jmin<juce::int64> (chunk, reader->lengthInSamples - start));
                reader->read (&buffer, 0, n, start, true, channels > 1);
                if (! writer->writeFromAudioSampleBuffer (buffer, 0, n))
                {
                    writer.reset();
                    temp.deleteFile();
                    return false;
                }
            }
        }

        // Only a complete file ever appears under the final name; if another process got there
        // first, its copy is just as good.
        if (to.existsAsFile())
        {
            temp.deleteFile();
            return true;
        }
        return temp.moveFileTo (to);
    }

    bool prepareFolder (const juce::File& root, const std::function<bool (float)>& progress)
    {
        juce::Array<juce::File> todo;
        for (const auto& entry : juce::RangedDirectoryIterator (root, true, "*", juce::File::findFiles))
            if (needsTranscoding (entry.getFile()))
                todo.add (entry.getFile());

        for (int i = 0; i < todo.size(); ++i)
        {
            if (progress && ! progress (static_cast<float> (i) / static_cast<float> (juce::jmax (1, todo.size()))))
                return false;

            const auto cached = fileFor (todo[i]);
            if (! cached.existsAsFile())
                transcode (todo[i], cached);
        }

        if (progress)
            progress (1.0f);
        return true;
    }
}
