// Engine: the save lane (Cmd+S shares what Live's API can't), snapshots, versions and backups.
//
//   1. We notice the session song was saved, copy it into the store (adding any audio it uses
//      that isn't there yet) and ask the Remote Script for the structure map.
//   2. The save goes to Player 1, who merges it into the latest snapshot (or takes it as is if
//      nobody saved in between) and tells everyone about the new snapshot.
//   3. The saver keeps going if their file *is* the new snapshot; if others' changes were merged
//      in, their Live reloads. Everyone else sees "Update available" until they save or update.
#include "Engine.h"

#include "Platform.h"
#include "tutti/AlsMerge.h"
#include "tutti/Ids.h"

namespace tutti
{
    namespace
    {
        bool isHash (const juce::String& s) { return s.length() == 64 && s.containsOnly ("0123456789abcdef"); }

        std::string hashOfStorePath (const juce::File& f)
        {
            const auto h = f.getParentDirectory().getFileName();
            return isHash (h) ? h.toStdString() : std::string();
        }

        juce::String stamp() { return juce::Time::getCurrentTime().toISO8601 (true); }

        juce::int64 fileStamp (const juce::File& f)
        {
            return f.getSize() ^ (f.getLastModificationTime().toMilliseconds() << 8);
        }

        juce::String friendlyTime (const std::string& iso)
        {
            const auto t = juce::Time::fromISO8601 (juce::String (iso));
            return t.toMilliseconds() == 0 ? juce::String (iso) : t.formatted ("%a %d %b, %H:%M");
        }
    }

    juce::File Engine::snapshotsDir() const
    {
        // One song, one history: every session of the same song adds to the same versions.
        if (! currentSongId.empty())
            return options.dataDir.getChildFile ("songs").getChildFile (juce::String (currentSongId)).getChildFile ("versions");
        return options.dataDir.getChildFile ("sessions").getChildFile (juce::String (sessionPtr->info().id)).getChildFile ("snapshots");
    }

    std::optional<Json> Engine::loadSnapshot (const std::string& id) const
    {
        if (id.empty())
            return std::nullopt;
        auto j = parseJson (snapshotsDir().getChildFile (juce::String (id) + ".json").loadFileAsString().toStdString());
        if (! j || ! j->is_object())
            return std::nullopt;
        return j;
    }

    void Engine::storeSnapshot (const Json& meta)
    {
        auto dir = snapshotsDir();
        dir.createDirectory();
        Json keep = meta;
        keep.erase ("type");
        keep.erase ("metas");
        dir.getChildFile (juce::String (str (meta, "id")) + ".json").replaceWithText (juce::String (keep.dump (1)));
    }

    // ================================================================== noticing a save

    void Engine::pollSave()
    {
        const double t = now();
        if (t - lastSavePoll < 1.0)
            return;
        lastSavePoll = t;
        if (! sessionPtr->inSession() || ! liveBound || reloading || ! currentSong.existsAsFile())
            return;
        // Saved under another name in the session folder ("Save As" into Music/Tutti/<song>, or
        // Live asked where to save and was pointed there): that file is the session song now.
        // Live's Save As puts the song in a "<name> Project" folder, so look one level down too
        // (but not in Live's Backup folders).
        const auto sessionFolder = workspaceFor (currentSongName);
        juce::Array<juce::File> songs = sessionFolder.findChildFiles (juce::File::findFiles, false, "*.als");
        for (const auto& sub : sessionFolder.findChildFiles (juce::File::findDirectories, false))
            if (sub.getFileName() != "Backup")
                songs.addArray (sub.findChildFiles (juce::File::findFiles, false, "*.als"));
        for (const auto& f : songs)
        {
            if (f == currentSong || f.getLastModificationTime() <= songFolderSeen)
                continue;
            songFolderSeen = juce::jmax (songFolderSeen, f.getLastModificationTime());
            if (tuttiWrote.count (f.getFullPathName().toStdString()) != 0
                && tuttiWrote[f.getFullPathName().toStdString()] == fileStamp (f))
                continue; // one of ours
            log ("the song was saved as " + f.getFileName().toStdString() + ": using that file from now on");
            notice ("Saved as " + f.getFileName().toStdString() + ": Tutti shares that file from now on.");
            currentSong = f;
            songStamp = 0;
            ignoreStamp = 0;
            songChangedAt = 0;
            break;
        }
        const auto s = fileStamp (currentSong);
        if (s != songStamp)
        {
            songStamp = s;
            songChangedAt = t; // wait until Live has finished writing it
            return;
        }
        if (songChangedAt > 0 && t - songChangedAt >= 1.0)
        {
            songChangedAt = 0;
            if (s != ignoreStamp && pendingSave.is_null())
                onSaved (currentSong);
        }
    }

    void Engine::onSaved (const juce::File& saved)
    {
        // The structure map must describe Live as it was saved: ask for it right away.
        pendingSave = Json { { "type", "save" }, { "id", newVersionId() }, { "session", sessionPtr->info().id },
                             { "base", currentSnapshot }, { "seq", sessionPtr->replica().seq() }, { "by", self.peer },
                             { "byName", self.name }, { "at", stamp().toStdString() }, { "song", currentSongName } };
        sendToLive (Json { { "type", "structure" }, { "req", ++structureRequest } });
        notice ("Sharing your save...");
        log ("saved in Live: sharing " + saved.getFullPathName().toStdString());
        editedSinceSave = false;
        myPluginChanges.clear();
        if (! mySaveNeeded.empty())
        {
            mySaveNeeded.clear();
            sessionPtr->broadcast (Json { { "type", "needs_save" }, { "what", Json::array() } });
        }

        const auto id = str (pendingSave, "id");
        const auto song = currentSongName;
        juce::Thread::launch ([this, saved, id, song] {
            juce::String error;
            auto set = als::load (saved, error);
            Json files = Json::array();
            std::string alsHash;
            if (set != nullptr)
            {
                for (auto& ref : als::fileRefs (*set))
                {
                    auto f = als::resolve (ref, saved.getParentDirectory());
                    if (f == juce::File())
                        continue;
                    auto hash = hashOfStorePath (f);
                    if (hash.empty() || ! (contentStore.has (hash) || contentStore.hasPreviewWav (hash)))
                    {
                        auto h = contentStore.add (f, error);
                        if (! h)
                            continue;
                        hash = *h;
                        audio::ensurePreview (contentStore, hash, error);
                        als::rewrite (ref, contentStore.fileFor (hash), saved.getParentDirectory());
                    }
                    files.push_back (hash);
                }
                auto temp = options.dataDir.getChildFile ("tmp").getChildFile (juce::File::createLegalFileName (juce::String (song)) + ".als");
                temp.getParentDirectory().createDirectory();
                if (als::save (*set, temp))
                    if (auto h = contentStore.add (temp, error))
                        alsHash = *h;
                temp.deleteFile();
            }
            post ([this, id, files, alsHash, error] {
                if (pendingSave.is_null() || str (pendingSave, "id") != id)
                    return;
                if (alsHash.empty())
                {
                    notice ("Couldn't read your saved song: " + error.toStdString());
                    pendingSave = nullptr;
                    return;
                }
                pendingSave["als"] = alsHash;
                pendingSave["files"] = files;
                if (pendingSave.contains ("map"))
                    sendSave (std::exchange (pendingSave, nullptr));
            });
        });
    }

    void Engine::sendSave (Json save)
    {
        if (sessionPtr->isPlayer1())
            handleSave (self.peer, save);
        else
            send (sessionPtr->info().p1, save);
    }

    // ================================================================== Player 1: merging saves

    void Engine::handleSave (const std::string& from, const Json& save)
    {
        if (! sessionPtr->isPlayer1())
            return;
        log (memberName (from) + " saved: merging it into the shared song (" + std::to_string (field (save, "files").size()) + " files)");
        wantFile (str (save, "als"), false);
        if (const auto& files = field (save, "files"); files.is_array())
            for (const auto& h : files)
                wantFile (h.get<std::string>(), false);
        saveQueue.push_back ({ from, save });
        processSaves();
    }

    void Engine::processSaves()
    {
        while (! saveQueue.empty() && sessionPtr->isPlayer1())
        {
            const auto [from, save] = saveQueue.front();
            if (! contentStore.has (str (save, "als")))
                return; // still downloading the saved song

            Json meta = save;
            meta["type"] = "snapshot";
            meta["merged"] = false;
            const auto current = loadSnapshot (sessionPtr->info().snapshot);
            if (current && str (save, "base") != str (*current, "id"))
            {
                // Someone else saved since this save started from its base: merge by track.
                const auto base = loadSnapshot (str (save, "base"));
                juce::String error;
                std::unique_ptr<juce::XmlElement> b, c, t;
                if (base && contentStore.has (str (*base, "als")) && contentStore.has (str (*current, "als")))
                {
                    b = als::load (contentStore.fileFor (str (*base, "als")), error);
                    c = als::load (contentStore.fileFor (str (*current, "als")), error);
                    t = als::load (contentStore.fileFor (str (save, "als")), error);
                }
                if (b && c && t)
                {
                    const auto& info = sessionPtr->info();
                    const bool theirsWins = from == info.p1 || str (*current, "by") != info.p1;
                    auto r = als::merge (*b, field (*base, "map"), *c, field (*current, "map"), field (*current, "notes"),
                                         *t, field (save, "map"), field (save, "notes"), theirsWins);
                    if (r.ok())
                    {
                        auto temp = options.dataDir.getChildFile ("tmp").getChildFile ("merged.als");
                        temp.getParentDirectory().createDirectory();
                        if (als::save (*r.set, temp))
                            if (auto h = contentStore.add (temp, error))
                            {
                                meta["als"] = *h;
                                meta["map"] = r.map;
                                meta["notes"] = r.notes;
                                meta["merged"] = true;
                                meta["conflicts"] = r.conflicts;
                                std::set<std::string> all;
                                for (const auto* list : { &field (*current, "files"), &field (save, "files") })
                                    if (list->is_array())
                                        for (const auto& h2 : *list)
                                            all.insert (h2.get<std::string>());
                                meta["files"] = Json (std::vector<std::string> (all.begin(), all.end()));
                            }
                        temp.deleteFile();
                    }
                    else
                        log ("merge failed (" + r.error + "); the newest save wins");
                }
                if (! boolean (meta, "merged"))
                    log ("couldn't merge with the earlier snapshot; the newest save wins (the earlier one stays in history)");
            }
            meta["prev"] = current ? str (*current, "id") : std::string();
            meta["summary"] = str (save, "byName") + (boolean (meta, "merged") ? " saved (merged with earlier saves)" : " saved");

            storeSnapshot (meta);
            sessionPtr->setSnapshot (str (meta, "id"));
            sessionPtr->broadcast (meta);
            saveQueue.pop_front();
            handleSnapshotNotice (self.peer, meta);
            backUp (meta);
        }
    }

    // ================================================================== everyone: a new snapshot

    void Engine::handleSnapshotNotice (const std::string& from, const Json& meta)
    {
        const auto id = str (meta, "id");
        // The same version can arrive twice (over two links, or relayed): handle it once, or
        // Live would reopen the song twice.
        if (id.empty() || ! handledSnapshots.insert (id).second)
            return;
        storeSnapshot (meta);
        log ("new version from " + memberName (from) + " (saved by " + str (meta, "byName") + "): " + id);
        latestSnapshot = id;
        wantFile (str (meta, "als"), false);
        if (const auto& files = field (meta, "files"); files.is_array())
            for (const auto& h : files)
                wantFile (h.get<std::string>(), true);

        const bool mine = str (meta, "by") == self.peer;
        addActivity ((mine ? std::string ("You") : str (meta, "byName", "Someone")) + " saved"
                     + (boolean (meta, "merged") ? " (combined with other saves)" : ""));
        if (! mine)
            updateSeenAt = now();
        if (mine && ! boolean (meta, "merged"))
        {
            // Our file is the new snapshot: nothing to reload.
            currentSnapshot = id;
            currentMap = field (meta, "map");
            currentNotes = field (meta, "notes");
            updateBy.clear();
            notice ("Your save is shared.");
            return;
        }
        if (mine || boolean (meta, "restore_all"))
        {
            notice (mine ? "Your save was merged with others' changes: updating your Live..." : "Player 1 restored an earlier version: updating your Live...");
            reloadInto (meta);
            return;
        }
        updateBy = str (meta, "byName");
        notice (updateBy + " shared changes that need an update. Save in Live (Cmd+S) to get them; yours are merged in.");
    }

    std::unique_ptr<juce::XmlElement> Engine::localCopyOf (const Json& meta, const juce::File& folder, juce::String& error)
    {
        auto set = als::load (contentStore.fileFor (str (meta, "als")), error);
        if (set == nullptr)
            return nullptr;
        for (auto& ref : als::fileRefs (*set))
        {
            const auto hash = hashOfStorePath (juce::File (ref.path.replaceCharacter ('\\', '/')));
            if (hash.empty())
                continue;
            const auto local = contentStore.bestFor (hash);
            if (local != juce::File())
                als::rewrite (ref, local, folder);
        }
        return set;
    }

    void Engine::reloadInto (const Json& meta)
    {
        if (! contentStore.has (str (meta, "als")))
        {
            wantFile (str (meta, "als"), false);
            notice ("Getting the update...");
            // pump() retries once the file is here (see updateNow / reloadWhenMerged).
            reloadWhenMerged = true;
            latestSnapshot = str (meta, "id");
            return;
        }
        if (liveIsBusy())
        {
            // Never pull the song out from under someone who's playing or recording.
            if (pendingReload.is_null())
                notice ("The update loads when you stop playing.");
            pendingReload = meta;
            return;
        }
        pendingReload = nullptr;
        juce::String error;
        auto folder = currentSong.getParentDirectory();
        makeLiveProject (folder);
        auto set = localCopyOf (meta, folder, error);
        if (set == nullptr)
        {
            notice ("Couldn't open the update: " + error.toStdString());
            return;
        }
        checkPlugins (*set);
        if (auto mine = als::load (currentSong, error))
            als::keepPersonalState (*set, field (meta, "map"), *mine, currentMap);

        // Live won't reload a file it already has open, so alternate between two names.
        auto target = alternateSong (currentSong);
        if (! als::save (*set, target))
        {
            notice ("Couldn't write the updated song.");
            return;
        }
        currentSong = target;
        currentMap = field (meta, "map");
        currentNotes = field (meta, "notes");
        currentSnapshot = str (meta, "id");
        songStamp = ignoreStamp = fileStamp (target);
        tuttiWrote[target.getFullPathName().toStdString()] = songStamp;
        songFolderSeen = juce::jmax (songFolderSeen, target.getLastModificationTime());
        songChangedAt = 0;
        updateBy.clear();
        reloadWhenMerged = false;
        reloading = true;
        liveBound = false;
        liveStatus = "Updating your Live...";
        openInLiveAndWait (target);
    }

    // ================================================================== saving for the person

    void Engine::saveAndShare()
    {
        if (! sessionPtr->inSession() || ! liveBound)
        {
            notice ("Join or start a session first.");
            return;
        }
        if (! options.pressSaveInLive)
            return;
        switch (pressSave (true))
        {
            case saveKeyWrongSong:
                notice ("Live isn't on the session song, so Tutti didn't press Save (it would ask where to save). "
                        "Click Open the song, then try again.");
                break;
            case saveKeyPressed:
                saveNeedsPermission = false;
                log ("pressed Save in Live");
                notice ("Saving in Live...");
                break;
            case saveKeyNeedsPermission:
                saveNeedsPermission = true;
               #if JUCE_MAC
                notice ("Allow Tutti in System Settings > Privacy & Security > Accessibility so it can press Save in Live. "
                        "Until then, press Cmd+S in Live.");
               #else
                notice ("Tutti isn't allowed to press Save in Live. Press Ctrl+S in Live.");
               #endif
                break;
            case saveKeyBusy:
                notice ("Let go of the keys and mouse, then try again (or press Cmd+S / Ctrl+S in Live).");
                break;
            case saveKeyNoLive:
                notice ("Live isn't open.");
                break;
            default:
               #if JUCE_MAC
                notice ("Couldn't press Save in Live. Press Cmd+S in Live.");
               #else
                notice ("Couldn't press Save in Live. Press Ctrl+S in Live.");
               #endif
                break;
        }
    }

    void Engine::setAutoSave (bool on)
    {
        setAutoSaveSeconds (on ? 60 : 0);
    }

    void Engine::setAutoSaveSeconds (int seconds)
    {
        autoSaveSeconds = juce::jlimit (0, 3600, seconds);
        autoSave = autoSaveSeconds > 0;
        saveSettings();
    }

    // Presses Save in Live only if Live's window shows the session song (anything else would
    // make Live ask where to save), and checks a moment later that no Save dialog appeared.
    int Engine::pressSave (bool bringToFront)
    {
        const auto expected = currentSong.getFileNameWithoutExtension().toStdString();
        const int result = options.pressSaveInLive (bringToFront, expected);
        if (result == saveKeyPressed)
            saveDialogCheckAt = now() + 1.5;
        else if (result == saveKeyWrongSong)
            log ("didn't press Save: Live's window isn't on " + expected);
        return result;
    }

    void Engine::autoSaveIfDue()
    {
        // After a plugin was added or changed, save once the person has stopped tweaking for a
        // few seconds, isn't playing or recording, and is working in Live (Tutti never pulls
        // Live to the front by itself).
        if (autoSaveSeconds <= 0 || noAutoSaveThisSession || ! options.pressSaveInLive || ! sessionPtr->inSession() || ! liveBound
            || reloading || ! pendingSave.is_null() || (mySaveNeeded.empty() && myPluginChanges.empty()))
            return;
        const double t = now();
        // Not constantly: at most once every autoSaveSeconds (Settings), and only once the
        // person has stopped tweaking for a few seconds.
        if (t - pluginChangedAt < 8.0 || t - lastAutoSaveTry < 5.0 || t - lastAutoSaveAt < autoSaveSeconds || liveIsBusy())
            return;
        lastAutoSaveTry = t;
        switch (pressSave (false))
        {
            case saveKeyPressed:
                lastAutoSaveAt = t;
                saveNeedsPermission = false;
                log ("saved automatically after plugin changes");
                notice ("Saved your plugin changes so everyone gets them.");
                break;
            case saveKeyNeedsPermission:
                saveNeedsPermission = true;
                if (! permissionNoticeShown)
                {
                    permissionNoticeShown = true;
                    notice ("Tutti can save for you after plugin changes: allow it in Settings.");
                }
                lastAutoSaveTry = t + 55.0; // ask Live again in a minute
                break;
            default:
                break; // Live not in front, keys held...: try again shortly
        }
    }

    bool Engine::liveIsBusy() const
    {
        auto it = presence.find (self.peer);
        return liveBound && it != presence.end() && (it->second.playing || it->second.recording);
    }

    void Engine::updateNow()
    {
        if (auto meta = loadSnapshot (latestSnapshot))
            reloadInto (*meta);
    }

    // ================================================================== versions

    void Engine::labelVersion (const std::string& label)
    {
        auto meta = loadSnapshot (latestSnapshot.empty() ? currentSnapshot : latestSnapshot);
        if (! meta)
            return;
        (*meta)["label"] = label;
        storeSnapshot (*meta);
        sessionPtr->broadcast (Json { { "type", "label" }, { "id", str (*meta, "id") }, { "label", label } });
        notice ("Version named \"" + label + "\".");
    }

    void Engine::restoreVersion (const std::string& id)
    {
        if (! sessionPtr->isPlayer1())
            return;
        auto old = loadSnapshot (id);
        if (! old)
            return;
        Json meta = *old;
        meta["type"] = "snapshot";
        meta["id"] = newVersionId();
        meta["by"] = self.peer;
        meta["byName"] = self.name;
        meta["at"] = stamp().toStdString();
        meta["prev"] = sessionPtr->info().snapshot;
        meta["restored_from"] = id;
        meta["restore_all"] = true;
        meta["merged"] = true;
        meta["summary"] = self.name + " restored the version from " + friendlyTime (str (*old, "at")).toStdString();
        meta.erase ("label");
        storeSnapshot (meta);
        sessionPtr->setSnapshot (str (meta, "id"));
        sessionPtr->broadcast (meta);
        handleSnapshotNotice (self.peer, meta);
        backUp (meta);
    }

    void Engine::openVersionCopy (const std::string& id)
    {
        auto meta = loadSnapshot (id);
        if (! meta)
            return;
        juce::String error;
        auto folder = options.songsDir.getChildFile (juce::File::createLegalFileName (juce::String (currentSongName) + " versions"));
        folder.createDirectory();
        auto set = localCopyOf (*meta, folder, error);
        if (set == nullptr)
        {
            notice ("Couldn't open that version: " + error.toStdString());
            return;
        }
        auto file = folder.getChildFile (juce::File::createLegalFileName (juce::String (currentSongName) + " - "
                                                                          + friendlyTime (str (*meta, "at")).replace (":", ".")) + ".als");
        als::save (*set, file);
        options.openInLive (file);
        notice ("Opened a copy of that version. Use \"Open the song\" to go back to the session.");
    }

    void Engine::setBackupFolder (const juce::File& folder)
    {
        backupFolder = folder;
        saveSettings();
    }

    void Engine::backUp (const Json& meta)
    {
        if (backupFolder == juce::File() || ! backupFolder.isDirectory())
            return;
        auto dir = backupFolder.getChildFile (juce::File::createLegalFileName (juce::String (currentSongName.empty() ? "Song" : currentSongName)));
        const auto when = friendlyTime (str (meta, "at")).replace (":", ".");
        const auto name = juce::File::createLegalFileName (when + " " + juce::String (str (meta, "byName"))
                                                           + (str (meta, "label").empty() ? juce::String() : " - " + juce::String (str (meta, "label"))));
        writeCompleteCopy (meta, dir, name, false);
    }

    // The song with all its audio copied next to it: opens in Live without Tutti.
    void Engine::writeCompleteCopy (const Json& meta, const juce::File& dir, const juce::String& name, bool reveal)
    {
        juce::Thread::launch ([this, meta, dir, name, reveal] {
            juce::String error;
            auto set = als::load (contentStore.fileFor (str (meta, "als")), error);
            if (set == nullptr)
            {
                post ([this, error] { notice ("Couldn't make the copy: " + error.toStdString()); });
                return;
            }
            dir.createDirectory();
            dir.getChildFile ("Ableton Project Info").createDirectory();
            auto samples = dir.getChildFile ("Samples");
            samples.createDirectory();
            int missing = 0;
            for (auto& ref : als::fileRefs (*set))
            {
                const auto hash = hashOfStorePath (juce::File (ref.path.replaceCharacter ('\\', '/')));
                if (hash.empty() || ! contentStore.has (hash))
                {
                    missing += hash.empty() ? 0 : 1;
                    continue;
                }
                auto source = contentStore.fileFor (hash);
                auto target = samples.getChildFile (source.getFileName());
                if (target.existsAsFile() && target.getSize() != source.getSize())
                    target = samples.getChildFile (juce::String (hash.substr (0, 8)) + " " + source.getFileName());
                if (! target.existsAsFile())
                    source.copyFileTo (target);
                als::rewrite (ref, target, dir);
            }
            const auto file = dir.getChildFile (name + ".als");
            const bool ok = als::save (*set, file);
            if (reveal)
                post ([this, ok, file, missing] {
                    if (! ok)
                    {
                        notice ("Couldn't write the copy.");
                        return;
                    }
                    notice ("Saved a complete copy: " + file.getFullPathName().toStdString()
                            + (missing > 0 ? " (" + std::to_string (missing) + " audio file(s) hadn't arrived yet)" : ""));
                    file.revealToUser();
                });
        });
    }
}
