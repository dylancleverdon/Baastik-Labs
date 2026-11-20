// Engine: the group's shared Google Drive folder (optional).
//
// Live edits and saves always travel directly between computers over Tailscale. When the group
// also shares a Google Drive folder (Drive for desktop on every computer), Tutti uses it for the
// heavy, slow-changing things:
//
//   <shared>/Tutti files/<hash>/<name>   every audio file (and song file) the session uses, by
//                                        content hash: whoever has a file puts it there once,
//                                        and the others' Drive downloads it in the background,
//                                        even after the person who recorded it has gone offline.
//   <shared>/Versions/<song>/...         every saved version, with its audio (written by Player 1)
//
// Files are only ever added, never changed, and a file taken from the folder must match its
// SHA-256, so a half-synced or edited file is simply ignored and fetched from a friend instead.
// Nobody opens songs from this folder: Live works on Tutti's copy in Music/Tutti.
#include "Engine.h"

#include "Platform.h"

namespace tutti
{
    namespace
    {
        const char* const kSharedName = "Tutti Shared";
        const char* const kFilesDir = "Tutti files";

        bool isHash (const juce::String& s) { return s.length() == 64 && s.containsOnly ("0123456789abcdef"); }

        // The file inside <shared>/Tutti files/<hash>/ (names starting with "." are partial copies).
        juce::File sharedFileFor (const juce::File& shared, const std::string& hash)
        {
            auto dir = shared.getChildFile (kFilesDir).getChildFile (juce::String (hash));
            for (const auto& f : dir.findChildFiles (juce::File::findFiles, false))
                if (! f.getFileName().startsWithChar ('.') && ! f.getFileName().endsWith (".part"))
                    return f;
            return {};
        }
    }

    std::string Engine::memberName (const std::string& peer) const
    {
        if (const auto* m = sessionPtr->info().member (peer))
            return m->name;
        if (auto it = peerHellos.find (peer); it != peerHellos.end())
            return str (it->second, "name", peer);
        return peer;
    }

    void Engine::setSharedFolder (const juce::File& folder)
    {
        sharedFolder = folder;
        publishedShared.clear();
        notInShared.clear();
        lastSharedSearch = -1000;
        saveSettings();
        if (folder != juce::File())
            log ("shared folder: " + folder.getFullPathName().toStdString());
    }

    juce::File Engine::activeSharedFolder() const
    {
        if (sharedFolder != juce::File())
            return sharedFolder.isDirectory() ? sharedFolder : juce::File();
        // Not chosen: look for a folder called "Tutti Shared" in Google Drive (My Drive, a
        // shortcut to a folder someone shared, or a shared drive), once a minute.
        const double t = juce::Time::getMillisecondCounterHiRes() / 1000.0;
        if (t - lastSharedSearch > 60.0)
        {
            lastSharedSearch = t;
            juce::File found;
            for (const auto& drive : platform::googleDriveFolders())
            {
                if (auto d = drive.getChildFile (kSharedName); d.isDirectory())
                    found = d;
                else if (drive.getFileName() == "Shared drives")
                    for (const auto& sd : drive.findChildFiles (juce::File::findDirectories, false))
                        if (auto d2 = sd.getChildFile (kSharedName); d2.isDirectory())
                            found = d2;
                if (found != juce::File())
                    break;
            }
            foundSharedFolder = found;
        }
        return foundSharedFolder.isDirectory() ? foundSharedFolder : juce::File();
    }

    // Every so often: put the session's files that the folder doesn't have yet into it.
    void Engine::pumpShared()
    {
        const double t = now();
        if (t - lastSharedPump < 15.0)
            return;
        lastSharedPump = t;
        notInShared.clear();
        if (! sessionPtr->inSession())
            return;
        const auto shared = activeSharedFolder();
        if (shared == juce::File())
            return;

        std::set<std::string> hashes;
        const auto& doc = sessionPtr->replica().optimistic();
        for (const auto& id : doc.subtree (Doc::rootId))
            if (auto* e = doc.find (id); e != nullptr && e->props.contains ("file") && e->props["file"].is_string())
                hashes.insert (e->props["file"].get<std::string>());
        if (auto meta = loadSnapshot (latestSnapshot))
        {
            if (const auto& files = field (*meta, "files"); files.is_array())
                for (const auto& h : files)
                    if (h.is_string())
                        hashes.insert (h.get<std::string>());
            hashes.insert (str (*meta, "als"));
        }

        std::vector<std::pair<std::string, juce::File>> todo;
        for (const auto& h : hashes)
            if (isHash (juce::String (h)) && publishedShared.count (h) == 0 && publishingShared.count (h) == 0 && contentStore.has (h))
                todo.emplace_back (h, contentStore.fileFor (h));
        if (todo.empty())
            return;
        for (const auto& [h, f] : todo)
            publishingShared.insert (h);

        juce::Thread::launch ([this, shared, todo] {
            for (const auto& [hash, source] : todo)
            {
                bool ok = sharedFileFor (shared, hash) != juce::File();
                bool copied = false;
                if (! ok)
                {
                    auto dir = shared.getChildFile (kFilesDir).getChildFile (juce::String (hash));
                    dir.createDirectory();
                    auto part = dir.getChildFile ("." + source.getFileName() + ".part");
                    auto target = dir.getChildFile (source.getFileName());
                    ok = source.copyFileTo (part) && part.moveFileTo (target);
                    copied = ok;
                    if (! ok)
                        part.deleteFile();
                }
                post ([this, hash, ok, copied, name = source.getFileName().toStdString()] {
                    publishingShared.erase (hash);
                    if (ok)
                        publishedShared.insert (hash);
                    if (copied)
                        log ("put " + name + " (" + hash.substr (0, 12) + ") in the shared folder");
                    else if (! ok)
                        log ("couldn't put " + name + " in the shared folder");
                });
            }
        });
    }

    // A full file we need: take it from the shared folder if it's there (and intact).
    bool Engine::fetchFromShared (const std::string& hash)
    {
        if (fetchingShared.count (hash) != 0)
            return true;
        if (notInShared.count (hash) != 0)
            return false;
        const auto shared = activeSharedFolder();
        if (shared == juce::File())
            return false;
        const auto source = sharedFileFor (shared, hash);
        if (source == juce::File())
        {
            notInShared.insert (hash);
            return false;
        }
        fetchingShared.insert (hash);
        const auto part = contentStore.partialFor (hash, "full");
        log ("getting " + source.getFileName().toStdString() + " (" + hash.substr (0, 12) + ") from the shared folder");
        juce::Thread::launch ([this, hash, source, part] {
            const auto started = juce::Time::getMillisecondCounterHiRes();
            part.getParentDirectory().createDirectory();
            part.deleteFile();
            juce::String error;
            // Reading a file Drive hasn't downloaded yet makes Drive fetch it.
            bool ok = source.copyFileTo (part);
            if (! ok)
                error = "couldn't read it";
            else
                ok = contentStore.commit (part, hash, "full", error);
            const auto seconds = (juce::Time::getMillisecondCounterHiRes() - started) / 1000.0;
            post ([this, hash, ok, error, seconds, size = source.getSize()] {
                fetchingShared.erase (hash);
                auto it = transfers.find (hash + ":full");
                if (ok)
                {
                    log ("got " + hash.substr (0, 12) + " from the shared folder (" + std::to_string (size / 1024) + " KB in "
                         + juce::String (seconds, 1).toStdString() + " s)");
                    publishedShared.insert (hash);
                    if (it != transfers.end())
                        transfers.erase (it);
                    fileArrived (hash, "full");
                    return;
                }
                log ("the shared folder's copy of " + hash.substr (0, 12) + " didn't work (" + error.toStdString() + "); asking friends");
                notInShared.insert (hash);
                if (it != transfers.end())
                    it->second.active = false;
            });
        });
        return true;
    }
}
