#include "tutti/AudioFiles.h"

#include <juce_audio_formats/juce_audio_formats.h>

namespace tutti::audio
{
    namespace
    {
        std::unique_ptr<juce::AudioFormatReader> open (const juce::File& file)
        {
            // A manager per call: cheap, thread-safe, and nothing left over at shutdown.
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            return std::unique_ptr<juce::AudioFormatReader> (formats.createReaderFor (file));
        }

        // Copies `frames` frames from reader to writer in blocks, padding with silence if the
        // reader runs short.
        bool copyFrames (juce::AudioFormatReader& reader, juce::AudioFormatWriter& writer, int channels, int64_t frames)
        {
            constexpr int block = 16384;
            juce::AudioBuffer<float> buffer (channels, block);
            int64_t done = 0;
            while (done < frames)
            {
                const int n = static_cast<int> (std::min<int64_t> (block, frames - done));
                buffer.clear();
                if (done < reader.lengthInSamples)
                    reader.read (&buffer, 0, n, done, true, channels > 1);
                if (! writer.writeFromAudioSampleBuffer (buffer, 0, n))
                    return false;
                done += n;
            }
            return true;
        }
    }

    std::optional<Info> probe (const juce::File& file)
    {
        auto reader = open (file);
        if (reader == nullptr || reader->sampleRate <= 0 || reader->numChannels == 0)
            return std::nullopt;
        return Info { reader->sampleRate, static_cast<int> (reader->numChannels), reader->lengthInSamples,
                      static_cast<int> (reader->bitsPerSample) };
    }

    void describe (Store& store, const std::string& hash)
    {
        auto m = store.meta (hash);
        if (! m || m->isAudio())
            return;
        if (auto info = probe (store.fileFor (hash)))
        {
            m->rate = info->rate;
            m->channels = info->channels;
            m->frames = info->frames;
            m->bits = info->bits;
            store.writeMeta (*m);
        }
    }

    bool encodePreview (const juce::File& source, const juce::File& oggOut, juce::String& error)
    {
        auto reader = open (source);
        if (reader == nullptr)
        {
            error = "can't read " + source.getFileName();
            return false;
        }
        juce::OggVorbisAudioFormat ogg;
        const int channels = static_cast<int> (std::min<unsigned int> (reader->numChannels, 8u));
        oggOut.getParentDirectory().createDirectory();
        auto temp = oggOut.getSiblingFile (oggOut.getFileName() + ".tmp");
        temp.deleteFile();
        {
            std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (temp);
            if (! static_cast<juce::FileOutputStream&> (*stream).openedOk())
            {
                error = "can't write the preview";
                return false;
            }
            const auto options = juce::AudioFormatWriterOptions()
                                     .withSampleRate (reader->sampleRate)
                                     .withNumChannels (channels)
                                     .withBitsPerSample (16)
                                     .withQualityOptionIndex (4); // 128 kbps
            auto writer = ogg.createWriterFor (stream, options);
            if (writer == nullptr)
            {
                error = "this audio format can't be previewed";
                return false;
            }
            if (! copyFrames (*reader, *writer, channels, reader->lengthInSamples))
            {
                error = "couldn't encode the preview";
                return false;
            }
        }
        oggOut.deleteFile();
        return temp.moveFileTo (oggOut);
    }

    bool decodePreview (const juce::File& oggFile, const Info& original, const juce::File& wavOut, juce::String& error)
    {
        auto reader = open (oggFile);
        if (reader == nullptr)
        {
            error = "can't read the preview";
            return false;
        }
        juce::WavAudioFormat wav;
        wavOut.getParentDirectory().createDirectory();
        auto temp = wavOut.getSiblingFile (wavOut.getFileName() + ".tmp");
        temp.deleteFile();
        {
            std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (temp);
            const int channels = original.channels > 0 ? original.channels : static_cast<int> (reader->numChannels);
            const auto options = juce::AudioFormatWriterOptions()
                                     .withSampleRate (original.rate > 0 ? original.rate : reader->sampleRate)
                                     .withNumChannels (channels)
                                     .withBitsPerSample (24);
            auto writer = wav.createWriterFor (stream, options);
            if (writer == nullptr)
            {
                error = "couldn't write the preview WAV";
                return false;
            }
            const int64_t frames = original.frames > 0 ? original.frames : reader->lengthInSamples;
            if (! copyFrames (*reader, *writer, channels, frames))
            {
                error = "couldn't decode the preview";
                return false;
            }
        }
        wavOut.deleteFile();
        return temp.moveFileTo (wavOut);
    }

    bool ensurePreview (Store& store, const std::string& hash, juce::String& error)
    {
        describe (store, hash);
        auto m = store.meta (hash);
        if (! m || ! m->isAudio())
            return false;
        auto oggFile = store.previewOggFor (hash);
        if (! oggFile.existsAsFile() || m->previewHash.empty())
        {
            if (! encodePreview (store.fileFor (hash), oggFile, error))
                return false;
            m->previewHash = Store::sha256 (oggFile);
            m->previewSize = oggFile.getSize();
            store.writeMeta (*m);
        }
        return true;
    }
}
