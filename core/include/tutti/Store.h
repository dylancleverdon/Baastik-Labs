#pragma once

#include "Json.h"

#include <juce_core/juce_core.h>

#include <map>
#include <mutex>
#include <optional>

namespace tutti
{
    // What we know about one audio file in the store.
    struct AudioMeta
    {
        std::string hash;     // SHA-256 of the file's bytes, lowercase hex
        std::string name;     // original file name, e.g. "Vox take 3.wav"
        int64_t size = 0;
        double rate = 0;      // sample rate (0 if not audio we can read)
        int channels = 0;
        int64_t frames = 0;
        int bits = 0;
        std::string previewHash; // SHA-256 of the Ogg preview, if one was made
        int64_t previewSize = 0;

        Json toJson() const;
        static std::optional<AudioMeta> fromJson (const Json&);
        bool isAudio() const { return rate > 0 && channels > 0 && frames > 0; }
    };

    // Content-addressed files on this computer: every audio file a session uses, once, by hash.
    //   files/<ab>/<hash>/<name>        the file itself
    //   files/<ab>/<hash>/meta.json
    //   previews/<hash>/preview.ogg     the compressed preview other people get first
    //   previews/<hash>/<name>.wav      the preview decoded for Live (exact original length)
    //   partial/                        downloads in progress
    class Store
    {
    public:
        explicit Store (juce::File root);

        const juce::File& root() const { return base; }

        // Adds a file from disk (an APFS clone on macOS, a copy elsewhere). Returns its hash.
        std::optional<std::string> add (const juce::File& file, juce::String& error);

        bool has (const std::string& hash) const;
        bool hasPreview (const std::string& hash) const;
        bool hasPreviewWav (const std::string& hash) const;

        juce::File fileFor (const std::string& hash) const;   // may not exist yet
        juce::File folderFor (const std::string& hash) const;
        juce::File previewOggFor (const std::string& hash) const;
        juce::File previewWavFor (const std::string& hash) const;
        juce::File partialFor (const std::string& hash, const std::string& kind) const;

        std::optional<AudioMeta> meta (const std::string& hash) const;
        void writeMeta (const AudioMeta&);

        // Verifies a finished download and moves it into place. kind: "full" or "preview".
        bool commit (const juce::File& partial, const std::string& hash, const std::string& kind, juce::String& error);

        // Hash of a file on disk, cached by path, size and modification time.
        std::optional<std::string> hashOf (const juce::File& file);

        // The best local file for Live: the full file, else the decoded preview, else nothing.
        juce::File bestFor (const std::string& hash) const;
        bool isPreviewPath (const juce::File&) const;

        static std::string sha256 (const juce::File& file);
        static std::string sha256 (const void* data, size_t size);

    private:
        juce::File base;
        std::map<juce::String, std::pair<juce::String, std::string>> hashCache; // path -> (size:mtime, hash)
        std::mutex cacheLock;
    };

    // Copies a file, as a copy-on-write clone where the file system supports it (APFS).
    bool cloneOrCopy (const juce::File& from, const juce::File& to);
}
