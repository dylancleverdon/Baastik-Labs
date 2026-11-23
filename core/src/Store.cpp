#include "tutti/Store.h"

#include <juce_cryptography/juce_cryptography.h>

#if JUCE_MAC
 #include <sys/clonefile.h>
#endif

namespace tutti
{
    Json AudioMeta::toJson() const
    {
        return Json { { "hash", hash }, { "name", name }, { "size", size }, { "rate", rate }, { "ch", channels },
                      { "frames", frames }, { "bits", bits }, { "phash", previewHash }, { "psize", previewSize } };
    }

    std::optional<AudioMeta> AudioMeta::fromJson (const Json& j)
    {
        AudioMeta m;
        m.hash = str (j, "hash");
        if (m.hash.size() != 64)
            return std::nullopt;
        m.name = str (j, "name");
        m.size = integer (j, "size");
        m.rate = number (j, "rate");
        m.channels = static_cast<int> (integer (j, "ch"));
        m.frames = integer (j, "frames");
        m.bits = static_cast<int> (integer (j, "bits"));
        m.previewHash = str (j, "phash");
        m.previewSize = integer (j, "psize");
        return m;
    }

    bool cloneOrCopy (const juce::File& from, const juce::File& to)
    {
        to.getParentDirectory().createDirectory();
        to.deleteFile();
       #if JUCE_MAC
        if (clonefile (from.getFullPathName().toRawUTF8(), to.getFullPathName().toRawUTF8(), 0) == 0)
            return true;
       #endif
        if (from.copyFileTo (to))
            return true;
        // Windows: CopyFile can't read a file another program still has open for writing (Live,
        // just after a recording). Reading it as a stream shares access, so it works.
        to.deleteFile();
        {
            juce::FileInputStream in (from);
            juce::FileOutputStream out (to);
            if (! in.openedOk() || ! out.openedOk())
                return false;
            if (out.writeFromInputStream (in, -1) != in.getTotalLength())
                return false;
            out.flush();
            if (out.getStatus().failed())
                return false;
        }
        return to.getSize() == from.getSize();
    }

    namespace
    {
        bool validHash (const std::string& hash)
        {
            if (hash.size() != 64)
                return false;
            for (char c : hash)
                if (! ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
                    return false;
            return true;
        }

        juce::String safeName (const std::string& name)
        {
            auto n = juce::File::createLegalFileName (juce::String (name));
            return n.isEmpty() ? juce::String ("audio") : n;
        }
    }

    Store::Store (juce::File root) : base (std::move (root))
    {
        base.createDirectory();
    }

    std::string Store::sha256 (const juce::File& file)
    {
        return juce::SHA256 (file).toHexString().toLowerCase().toStdString();
    }

    std::string Store::sha256 (const void* data, size_t size)
    {
        return juce::SHA256 (data, size).toHexString().toLowerCase().toStdString();
    }

    juce::File Store::folderFor (const std::string& hash) const
    {
        return base.getChildFile ("files").getChildFile (juce::String (hash.substr (0, 2))).getChildFile (juce::String (hash));
    }

    juce::File Store::fileFor (const std::string& hash) const
    {
        if (auto m = meta (hash))
            return folderFor (hash).getChildFile (safeName (m->name));
        // No metadata yet: whatever single file is in the folder.
        auto folder = folderFor (hash);
        for (const auto& f : folder.findChildFiles (juce::File::findFiles, false))
            if (f.getFileName() != "meta.json")
                return f;
        return folder.getChildFile ("audio");
    }

    juce::File Store::previewOggFor (const std::string& hash) const
    {
        return base.getChildFile ("previews").getChildFile (juce::String (hash)).getChildFile ("preview.ogg");
    }

    juce::File Store::previewWavFor (const std::string& hash) const
    {
        auto name = meta (hash) ? juce::File::createLegalFileName (juce::String (meta (hash)->name)) : juce::String ("preview");
        if (name.containsChar ('.'))
            name = name.upToLastOccurrenceOf (".", false, false);
        return base.getChildFile ("previews").getChildFile (juce::String (hash)).getChildFile (name + " (preview).wav");
    }

    juce::File Store::partialFor (const std::string& hash, const std::string& kind) const
    {
        return base.getChildFile ("partial").getChildFile (juce::String (hash) + "." + juce::String (kind) + ".part");
    }

    bool Store::has (const std::string& hash) const
    {
        return validHash (hash) && fileFor (hash).existsAsFile() && fileFor (hash).getFileName() != "audio";
    }

    bool Store::hasPreview (const std::string& hash) const { return validHash (hash) && previewOggFor (hash).existsAsFile(); }
    bool Store::hasPreviewWav (const std::string& hash) const { return validHash (hash) && previewWavFor (hash).existsAsFile(); }

    std::optional<AudioMeta> Store::meta (const std::string& hash) const
    {
        if (! validHash (hash))
            return std::nullopt;
        auto file = folderFor (hash).getChildFile ("meta.json");
        if (! file.existsAsFile())
            return std::nullopt;
        auto j = parseJson (file.loadFileAsString().toStdString());
        return j ? AudioMeta::fromJson (*j) : std::nullopt;
    }

    void Store::writeMeta (const AudioMeta& m)
    {
        if (! validHash (m.hash))
            return;
        auto folder = folderFor (m.hash);
        folder.createDirectory();
        folder.getChildFile ("meta.json").replaceWithText (juce::String (m.toJson().dump (2)));
    }

    std::optional<std::string> Store::add (const juce::File& file, juce::String& error)
    {
        if (! file.existsAsFile())
        {
            error = "missing file " + file.getFullPathName();
            return std::nullopt;
        }
        const auto hash = hashOf (file);
        if (! hash)
        {
            error = "couldn't read " + file.getFullPathName();
            return std::nullopt;
        }
        auto m = meta (*hash).value_or (AudioMeta {});
        if (m.hash.empty())
        {
            m.hash = *hash;
            m.name = file.getFileName().toStdString();
            m.size = file.getSize();
            writeMeta (m);
        }
        auto target = fileFor (*hash);
        if (! target.existsAsFile())
        {
            // Copy, then check the copy is exactly what was hashed (the file may still have been
            // changing), and only then put it in place.
            auto tmp = base.getChildFile ("partial").getChildFile (juce::String (*hash) + ".add.part");
            if (! cloneOrCopy (file, tmp))
            {
                tmp.deleteFile();
                error = "couldn't copy " + file.getFullPathName() + " into Tutti's store";
                return std::nullopt;
            }
            if (sha256 (tmp) != *hash)
            {
                tmp.deleteFile();
                error = file.getFullPathName() + " changed while it was being copied";
                return std::nullopt;
            }
            target.getParentDirectory().createDirectory();
            if (! tmp.moveFileTo (target))
            {
                tmp.deleteFile();
                error = "couldn't move " + file.getFileName() + " into Tutti's store";
                return std::nullopt;
            }
        }
        return hash;
    }

    bool Store::commit (const juce::File& partial, const std::string& hash, const std::string& kind, juce::String& error)
    {
        if (kind == "full")
        {
            const auto actual = sha256 (partial);
            if (actual != hash)
            {
                error = "download didn't match its checksum";
                partial.deleteFile();
                return false;
            }
            auto target = fileFor (hash);
            target.getParentDirectory().createDirectory();
            target.deleteFile();
            return partial.moveFileTo (target);
        }
        auto m = meta (hash);
        if (m && ! m->previewHash.empty() && sha256 (partial) != m->previewHash)
        {
            error = "preview didn't match its checksum";
            partial.deleteFile();
            return false;
        }
        auto target = previewOggFor (hash);
        target.getParentDirectory().createDirectory();
        target.deleteFile();
        return partial.moveFileTo (target);
    }

    std::optional<std::string> Store::hashOf (const juce::File& file)
    {
        if (! file.existsAsFile())
            return std::nullopt;
        const auto stamp = juce::String (file.getSize()) + ":" + juce::String (file.getLastModificationTime().toMilliseconds());
        {
            std::lock_guard<std::mutex> g (cacheLock);
            auto it = hashCache.find (file.getFullPathName());
            if (it != hashCache.end() && it->second.first == stamp)
                return it->second.second;
        }
        const auto hash = sha256 (file);
        if (hash.empty())
            return std::nullopt;
        std::lock_guard<std::mutex> g (cacheLock);
        hashCache[file.getFullPathName()] = { stamp, hash };
        return hash;
    }

    juce::File Store::bestFor (const std::string& hash) const
    {
        if (has (hash))
            return fileFor (hash);
        if (hasPreviewWav (hash))
            return previewWavFor (hash);
        return {};
    }

    bool Store::isPreviewPath (const juce::File& f) const
    {
        return f.isAChildOf (base.getChildFile ("previews"));
    }
}
