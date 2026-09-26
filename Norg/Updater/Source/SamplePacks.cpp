#include "SamplePacks.h"
#include "engine/sfz/SampleCache.h"

namespace norg::update
{
    namespace
    {
        juce::String str (const juce::var& o, const char* key) { return o[key].isVoid() ? juce::String() : o[key].toString(); }

        bool isSafeFolderName (const juce::String& name)
        {
            return name.isNotEmpty() && ! name.containsAnyOf ("/\\:") && ! name.startsWithChar ('.') && name.length() < 100;
        }
    }

    std::vector<SamplePack> parsePacks (const juce::String& json, juce::String& error)
    {
        std::vector<SamplePack> packs;
        const auto parsed = juce::JSON::parse (json);
        const auto* list = parsed["packs"].getArray();
        if (list == nullptr)
        {
            error = "no pack list";
            return packs;
        }

        for (const auto& item : *list)
        {
            SamplePack p;
            p.id = str (item, "id");
            p.name = str (item, "name");
            p.folder = str (item, "folder");
            p.url = str (item, "url");
            p.sha256 = str (item, "sha256").toLowerCase();
            p.license = str (item, "license");
            p.version = static_cast<int> (item["version"]);
            p.size = static_cast<juce::int64> (item["size"]);

            if (p.id.isEmpty() || ! p.id.containsOnly ("abcdefghijklmnopqrstuvwxyz0123456789-") || ! isSafeFolderName (p.folder)
                || p.sha256.length() != 64 || p.version <= 0)
            {
                error = "skipping malformed pack entry " + p.id;
                continue;
            }
            packs.push_back (p);
        }
        return packs;
    }

    juce::String toJson (const std::vector<PackStatus>& statuses)
    {
        juce::Array<juce::var> list;
        for (const auto& s : statuses)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("id", s.id);
            o->setProperty ("name", s.name);
            o->setProperty ("state", s.state);
            o->setProperty ("progress", s.progress);
            list.add (juce::var (o));
        }
        auto* root = new juce::DynamicObject();
        root->setProperty ("packs", list);
        return juce::JSON::toString (juce::var (root));
    }

    std::vector<PackStatus> parsePackStatus (const juce::String& json)
    {
        std::vector<PackStatus> out;
        const auto parsed = juce::JSON::parse (json);
        if (const auto* list = parsed["packs"].getArray())
            for (const auto& item : *list)
                out.push_back ({ str (item, "id"), str (item, "name"), str (item, "state"), static_cast<float> (item["progress"]) });
        return out;
    }

    //==============================================================================
    PackInstaller::PackInstaller (Layout l, Platform& p, FileOps& ops, Updater::LogFn logFn)
        : layout (std::move (l)), platform (p), fileOps (ops), log (std::move (logFn))
    {
    }

    bool PackInstaller::isInstalled (const SamplePack& pack) const
    {
        const auto state = juce::JSON::parse (layout.packsStateJson().loadFileAsString());
        const int installedVersion = static_cast<int> (state[pack.id.toRawUTF8()]);
        return installedVersion >= pack.version && layout.samplesDir().getChildFile (pack.folder).isDirectory();
    }

    void PackInstaller::markInstalled (const SamplePack& pack)
    {
        auto state = juce::JSON::parse (layout.packsStateJson().loadFileAsString());
        if (! state.isObject())
            state = juce::var (new juce::DynamicObject());
        state.getDynamicObject()->setProperty (pack.id, pack.version);
        layout.packsStateJson().replaceWithText (juce::JSON::toString (state));
    }

    void PackInstaller::setStatus (const SamplePack& pack, const juce::String& state, float progress)
    {
        bool found = false;
        for (auto& s : statuses)
            if (s.id == pack.id)
            {
                s.state = state;
                s.progress = progress;
                found = true;
            }
        if (! found)
            statuses.push_back ({ pack.id, pack.name, state, progress });

        // The plugin polls this file; no need to write it more than a couple of times a second.
        const auto now = juce::Time::getMillisecondCounter();
        if (state != "downloading" && state != "preparing")
            lastStatusWrite = 0;
        if (now - lastStatusWrite > 400)
        {
            layout.supportDir().createDirectory();
            layout.packsStatusJson().replaceWithText (toJson (statuses));
            lastStatusWrite = now;
        }
    }

    bool PackInstaller::installAll (const juce::String& listUrl, const juce::StringArray& extraAllowedPrefixes, bool allowUpgrades)
    {
        layout.packsStagingDir().createDirectory();
        const auto listFile = layout.packsStagingDir().getChildFile ("packs.json");

        juce::String error;
        if (! platform.download (listUrl, listFile, error))
        {
            log ("could not fetch the sample pack list: " + error);
            return false;
        }

        const auto packs = parsePacks (listFile.loadFileAsString(), error);
        if (error.isNotEmpty())
            log (error);

        bool allReady = true;
        for (const auto& pack : packs)
        {
            const auto state = juce::JSON::parse (layout.packsStateJson().loadFileAsString());
            const bool present = layout.samplesDir().getChildFile (pack.folder).isDirectory()
                              && static_cast<int> (state[pack.id.toRawUTF8()]) > 0;

            if (isInstalled (pack) || (present && ! allowUpgrades))
            {
                setStatus (pack, "ready", 1.0f);
                continue;
            }

            if (! install (pack, extraAllowedPrefixes))
            {
                setStatus (pack, "failed", 0.0f);
                allReady = false;
            }
        }

        layout.packsStatusJson().replaceWithText (toJson (statuses));
        return allReady;
    }

    bool PackInstaller::install (const SamplePack& pack, const juce::StringArray& extraAllowedPrefixes)
    {
        if (! isAllowedAssetUrl (pack.url, extraAllowedPrefixes))
        {
            log ("pack " + pack.id + " points outside Norg's releases; skipped");
            return false;
        }

        const auto staging = layout.packsStagingDir().getChildFile (pack.id);
        staging.createDirectory();
        const auto archive = staging.getChildFile (pack.id + "-v" + juce::String (pack.version) + ".tar.gz");

        // 1. Download (resuming a partial file from an interrupted run).
        log ("downloading sample pack " + pack.name);
        setStatus (pack, "downloading", 0.0f);
        juce::String error;
        const auto expected = juce::jmax<juce::int64> (1, pack.size);
        if (! platform.download (pack.url, archive, error, true,
                                 [this, &pack, expected] (juce::int64 bytes) { setStatus (pack, "downloading", juce::jlimit (0.0f, 1.0f, static_cast<float> (bytes) / static_cast<float> (expected))); }))
        {
            log ("pack download failed: " + error);
            return false;
        }

        if (sha256OfFile (archive) != pack.sha256)
        {
            log ("pack " + pack.id + " did not match its checksum; it will be downloaded again");
            archive.deleteFile();
            return false;
        }

        // 2. Unpack next to the archive.
        const auto unpacked = staging.getChildFile ("unpacked");
        fileOps.removeRecursively (unpacked);
        unpacked.createDirectory();
        if (! platform.extractArchive (archive, unpacked, error))
        {
            log ("could not unpack " + pack.name + ": " + error);
            return false;
        }

        auto root = unpacked.getChildFile (pack.folder);
        if (! root.isDirectory())
        {
            const auto dirs = unpacked.findChildFiles (juce::File::findDirectories, false);
            root = dirs.size() == 1 ? dirs[0] : unpacked;
        }

        // 3. Prepare the samples (the cache is keyed by file name, so it survives the move below).
        log ("preparing sample pack " + pack.name);
        setStatus (pack, "preparing", 0.0f);
        sfz::cache::prepareFolder (root, [this, &pack] (float p) { setStatus (pack, "preparing", p); return true; });

        // 4. Move it into the samples folder in one step, so Norg never sees half a library.
        const auto destination = layout.samplesDir().getChildFile (pack.folder);
        layout.samplesDir().createDirectory();
        const auto old = staging.getChildFile ("old");
        fileOps.removeRecursively (old);
        if (destination.exists() && ! fileOps.move (destination, old))
        {
            log ("could not replace the old " + pack.name);
            return false;
        }
        if (! fileOps.move (root, destination))
        {
            if (old.exists())
                fileOps.move (old, destination);
            log ("could not install " + pack.name);
            return false;
        }

        markInstalled (pack);
        fileOps.removeRecursively (staging);
        setStatus (pack, "ready", 1.0f);
        log ("sample pack ready: " + pack.name);
        return true;
    }
}
