// Engine: file transfers, presence, Play together, suggestion-mode ideas, settings and the view.
#include "Engine.h"

#include "Platform.h"
#include "tutti/Ids.h"

namespace tutti
{
    namespace
    {
        constexpr int64_t kChunk = 256 * 1024;
        constexpr size_t kMaxQueued = 4u * 1024u * 1024u;
        constexpr int kMaxActive = 6;

        // Live's track colour indices for Tutti's eight member colours.
        constexpr int kLiveColours[] = { 13, 26, 16, 1, 24, 4, 9, 21 };
    }

    // ================================================================== files

    std::string Engine::memberName (const std::string& peer) const
    {
        if (const auto* m = sessionPtr->info().member (peer))
            return m->name;
        if (auto it = peerHellos.find (peer); it != peerHellos.end())
            return str (it->second, "name", peer);
        return peer;
    }

    void Engine::Flow::add (int64_t bytes, double t)
    {
        done += bytes;
        lastProgress = t;
        samples.emplace_back (t, done);
        while (samples.size() > 2 && t - samples.front().first > 8.0)
            samples.pop_front();
    }

    EngineView::Transfer Engine::Flow::view (double t, const std::string& who) const
    {
        EngineView::Transfer v;
        if (total <= 0)
            return v;
        v.active = true;
        v.done = std::min (done, total);
        v.total = total;
        v.who = who;
        if (samples.size() >= 2 && samples.back().first - samples.front().first > 0.5)
            v.bytesPerSecond = (double) (samples.back().second - samples.front().second) / (samples.back().first - samples.front().first);
        v.quietFor = lastProgress > 0 ? t - lastProgress : 0;
        if (v.quietFor > 4.0)
            v.bytesPerSecond = 0;
        v.secondsLeft = v.bytesPerSecond > 0 ? (double) (total - v.done) / v.bytesPerSecond : -1;
        v.stalled = v.quietFor > 15.0;
        return v;
    }

    void Engine::wantFile (const std::string& hash, bool previewFirst)
    {
        if (hash.size() != 64 || contentStore.has (hash))
            return;
        if (previewFirst && ! contentStore.hasPreviewWav (hash) && transfers.count (hash + ":preview") == 0)
            transfers[hash + ":preview"] = Transfer { hash, "preview" };
        if (transfers.count (hash + ":full") == 0)
        {
            Transfer tr { hash, "full" };
            if (downloads.total == 0)
                downloads.lastProgress = now();
            if (auto m = contentStore.meta (hash); m && m->size > 0)
            {
                downloads.total += m->size; // counted up front, so the bar shows the whole song
                tr.expected = m->size;
            }
            transfers[hash + ":full"] = tr;
        }
        // Not audio (the song file): fetch it before any audio, or a song with many recordings
        // sits at "Downloading the song..." until every preview has arrived.
        if (! previewFirst)
            transfers[hash + ":full"].first = true;
    }

    void Engine::pumpTransfers()
    {
        if (transfers.empty())
        {
            downloads = {};
            return;
        }
        const double t = now();
        int active = 0;
        for (auto& [key, tr] : transfers)
        {
            if (tr.active && t - tr.lastProgress > 15.0)
            {
                // Nothing for 15 s (the request or chunks were lost, or the sender is stuck):
                // ask again, from someone else if anyone else has it.
                log ("transfer of " + tr.hash.substr (0, 12) + " (" + tr.kind + ") from " + tr.from + " stalled; asking again");
                tr.active = false;
                tr.tried.push_back (tr.from);
            }
            if (tr.active)
                ++active;
        }

        // The song file first, then previews, then full files.
        for (int pass = 0; pass < 3; ++pass)
            for (auto& [key, tr] : transfers)
            {
                if (active >= kMaxActive + (pass == 0 ? 1 : 0))
                    break;
                const bool wanted = pass == 0 ? tr.first : (pass == 1 ? tr.kind == "preview" : (tr.kind == "full" && ! tr.first));
                if (! wanted || tr.active || tr.retryAt > t)
                    continue;
                if (tr.kind == "preview" && contentStore.has (tr.hash))
                    continue;

                // Player 1 first (it has everything), then the others.
                std::vector<std::string> candidates;
                const auto& info = sessionPtr->info();
                if (! info.p1.empty() && info.p1 != self.peer)
                    candidates.push_back (info.p1);
                for (const auto& m : info.members)
                    if (m.peer != self.peer && m.peer != info.p1)
                        candidates.push_back (m.peer);
                std::string pick;
                for (const auto& c : candidates)
                    if (peerLinks.count (c) != 0 && std::find (tr.tried.begin(), tr.tried.end(), c) == tr.tried.end())
                    {
                        pick = c;
                        break;
                    }
                if (pick.empty())
                {
                    tr.tried.clear();
                    tr.retryAt = t + 10.0;
                    continue;
                }
                tr.active = true;
                tr.from = pick;
                tr.started = t;
                tr.lastProgress = t;
                tr.received = 0;
                tr.total = -1;
                contentStore.partialFor (tr.hash, tr.kind).deleteFile();
                send (pick, Json { { "type", "get" }, { "hash", tr.hash }, { "kind", tr.kind } });
                log ("asking " + memberName (pick) + " for " + tr.hash.substr (0, 12) + " (" + tr.kind + ")");
                ++active;
            }
    }

    void Engine::prepareLocalFile (const std::string& hash)
    {
        if (preparing.count (hash) != 0 || ! contentStore.has (hash))
            return;
        preparing.insert (hash);
        juce::Thread::launch ([this, hash] {
            juce::String error;
            audio::ensurePreview (contentStore, hash, error);
            post ([this, hash] { preparing.erase (hash); });
        });
    }

    void Engine::handleFileMessage (const std::string& peer, const Json& m, const protocol::Frame* binary)
    {
        const auto type = str (m, "type");
        const auto hash = str (m, "hash");
        const auto kind = str (m, "kind");

        if (type == "get")
        {
            juce::File file;
            if (kind == "preview")
            {
                if (contentStore.hasPreview (hash))
                    file = contentStore.previewOggFor (hash);
                else if (contentStore.has (hash))
                {
                    auto meta = contentStore.meta (hash);
                    if (meta && ! meta->isAudio())
                    {
                        send (peer, Json { { "type", "missing" }, { "hash", hash }, { "kind", kind }, { "no_preview", true } });
                        return;
                    }
                    prepareLocalFile (hash);
                    send (peer, Json { { "type", "missing" }, { "hash", hash }, { "kind", kind }, { "retry", true } });
                    return;
                }
            }
            else if (contentStore.has (hash))
                file = contentStore.fileFor (hash);

            if (file == juce::File())
            {
                send (peer, Json { { "type", "missing" }, { "hash", hash }, { "kind", kind } });
                return;
            }
            auto it = peerLinks.find (peer);
            if (it == peerLinks.end())
                return;
            Outgoing out;
            out.link = it->second;
            out.hash = hash;
            out.kind = kind;
            out.file = file;
            out.total = file.getSize();
            if (auto meta = contentStore.meta (hash))
                out.meta = meta->toJson();
            serving.push_back (out);
            if (uploads.total == 0)
                uploads.lastProgress = now();
            uploads.total += out.total;
            log ("sending " + file.getFileName().toStdString() + " (" + hash.substr (0, 12) + ", " + kind + ", "
                 + std::to_string (out.total / 1024) + " KB) to " + memberName (peer));
            return;
        }

        if (type == "missing")
        {
            auto it = transfers.find (hash + ":" + kind);
            if (it == transfers.end() || it->second.from != peer)
                return;
            auto& tr = it->second;
            tr.active = false;
            log (memberName (peer) + " doesn't have " + hash.substr (0, 12) + " (" + kind + ")"
                 + (boolean (m, "retry") ? " yet" : ""));
            if (boolean (m, "no_preview"))
            {
                transfers.erase (it); // not audio: just get the file
                return;
            }
            if (boolean (m, "retry") && tr.retries < 8)
            {
                ++tr.retries;
                tr.retryAt = now() + 1.5;
            }
            else
                tr.tried.push_back (peer);
            return;
        }

        if (binary == nullptr)
            return;

        // A chunk of a file we asked for.
        auto it = transfers.find (hash + ":" + kind);
        if (it == transfers.end() || ! it->second.active || it->second.from != peer)
            return;
        auto& tr = it->second;
        const auto offset = integer (m, "off");
        const auto total = integer (m, "total");
        if (offset != tr.received)
        {
            tr.active = false; // out of step: start over
            return;
        }
        if (offset == 0)
        {
            tr.total = total;
            if (const auto& meta = field (m, "meta"); meta.is_object())
                if (auto parsed = AudioMeta::fromJson (meta); parsed && ! contentStore.meta (hash))
                    contentStore.writeMeta (*parsed);
        }
        auto part = contentStore.partialFor (hash, kind);
        part.getParentDirectory().createDirectory();
        {
            juce::FileOutputStream out (part);
            if (! out.openedOk())
            {
                tr.active = false;
                return;
            }
            out.write (binary->data.data(), binary->data.size());
        }
        tr.received += static_cast<int64_t> (binary->data.size());
        tr.lastProgress = now();
        if (tr.expected == 0 && tr.total > 0)
        {
            tr.expected = tr.total;
            downloads.total += tr.total;
        }
        downloads.add (static_cast<int64_t> (binary->data.size()), now());
        if (tr.received < tr.total)
            return;

        juce::String error;
        const bool ok = contentStore.commit (part, hash, kind, error);
        if (! ok)
        {
            log ("download of " + hash + " failed: " + error.toStdString());
            tr.active = false;
            tr.tried.push_back (peer);
            return;
        }
        log ("got " + hash.substr (0, 12) + " (" + kind + ") from " + memberName (peer) + ": "
             + std::to_string (tr.total / 1024) + " KB in " + juce::String (now() - tr.started, 1).toStdString() + " s");
        transfers.erase (it);
        fileArrived (hash, kind);
    }

    void Engine::serveChunks()
    {
        if (serving.empty())
        {
            uploads = {};
            return;
        }
        int budget = 16; // chunks per pump
        while (! serving.empty() && budget-- > 0)
        {
            auto out = serving.front();
            serving.pop_front();
            auto it = links.find (out.link);
            if (it == links.end())
                continue;
            if (it->second.link->queuedBytes() > kMaxQueued)
            {
                serving.push_back (out); // that link is busy; come back to it
                continue;
            }
            juce::FileInputStream in (out.file);
            if (! in.openedOk())
                continue;
            in.setPosition (out.offset);
            juce::HeapBlock<char> data (static_cast<size_t> (kChunk));
            const auto n = in.read (data.getData(), static_cast<int> (std::min<int64_t> (kChunk, out.total - out.offset)));
            if (n < 0)
                continue;
            Json header { { "type", "chunk" }, { "hash", out.hash }, { "kind", out.kind }, { "off", out.offset },
                          { "total", out.total } };
            if (out.offset == 0 && ! out.meta.is_null())
                header["meta"] = out.meta;
            it->second.link->send (protocol::encodeBinary (header, data.getData(), static_cast<size_t> (n)));
            out.offset += n;
            uploads.add (n, now());
            if (out.offset < out.total && n > 0)
                serving.push_back (out);
        }
    }

    void Engine::fileArrived (const std::string& hash, const std::string& kind)
    {
        auto tellLive = [this] (const std::string& h) {
            if (liveBound)
            {
                Ops ops;
                const auto doc = sessionPtr->replica().optimistic();
                const auto local = contentStore.bestFor (h);
                for (const auto& id : doc.subtree (Doc::rootId))
                    if (auto* e = doc.find (id); e != nullptr && e->type == "clip" && str (e->props, "file") == h)
                        ops.push_back (Op::set (id, { { "path", local.getFullPathName().toStdString() } }));
                if (! ops.empty())
                    sendToLive (Json { { "type", "apply" }, { "ops", toJson (ops) } });
            }
            maybeOpenJoinedSong();
            if (onChange)
                onChange();
        };

        if (kind == "full")
        {
            transfers.erase (hash + ":preview");
            tellLive (hash);
            return;
        }
        // A preview: decode it to a WAV of exactly the original length, off this thread.
        auto meta = contentStore.meta (hash);
        if (! meta || ! meta->isAudio())
        {
            maybeOpenJoinedSong();
            return;
        }
        const auto info = audio::Info { meta->rate, meta->channels, meta->frames, meta->bits };
        juce::Thread::launch ([this, hash, info, tellLive] {
            juce::String error;
            const bool ok = audio::decodePreview (contentStore.previewOggFor (hash), info, contentStore.previewWavFor (hash), error);
            post ([this, hash, ok, error, tellLive] {
                if (! ok)
                {
                    log ("couldn't decode preview " + hash + ": " + error.toStdString());
                    return;
                }
                if (! contentStore.has (hash))
                    tellLive (hash);
            });
        });
    }

    // ================================================================== presence

    void Engine::handlePresence (const std::string& peer, const Json& m)
    {
        Presence p;
        p.track = str (m, "track");
        p.clip = str (m, "clip");
        p.time = number (m, "time");
        p.playing = boolean (m, "playing");
        p.recording = boolean (m, "rec");
        presence[peer] = p;
    }

    std::string Engine::trackName (const std::string& trackId) const
    {
        if (trackId.empty())
            return {};
        const auto& doc = sessionPtr->replica().confirmed();
        if (auto* e = doc.find (trackId))
            return str (e->props, "name");
        return {};
    }

    double Engine::barOf (double beats) const
    {
        const auto& song = sessionPtr->replica().confirmed().find (Doc::rootId)->props;
        const double n = number (song, "sig_n", 4.0);
        const double d = number (song, "sig_d", 4.0);
        const double beatsPerBar = n * 4.0 / (d > 0 ? d : 4.0);
        return 1.0 + beats / (beatsPerBar > 0 ? beatsPerBar : 4.0);
    }

    // ================================================================== Play together

    void Engine::playTogether()
    {
        if (! sessionPtr->inSession())
            return;
        const double pos = presence.count (self.peer) != 0 ? presence[self.peer].time : 0.0;
        if (sessionPtr->isPlayer1())
        {
            Json play { { "type", "play" }, { "pos", pos }, { "at", now() + 0.6 }, { "by", self.peer } };
            sessionPtr->broadcast (play);
            handlePlay (play);
        }
        else
            send (sessionPtr->info().p1, Json { { "type", "play_req" }, { "pos", pos } });
    }

    void Engine::handlePlay (const Json& m)
    {
        const auto by = str (m, "by");
        if (by != self.peer)
        {
            if (playMode == "never")
                return;
            if (playMode == "ask")
            {
                playRequest = m;
                playRequestAt = now();
                const auto* who = sessionPtr->info().member (by);
                notice ((who != nullptr ? who->name : std::string ("Someone")) + " wants to play together.");
                return;
            }
        }
        playRequest = m;
        answerPlayTogether (true);
    }

    void Engine::answerPlayTogether (bool join)
    {
        if (playRequest.is_null())
            return;
        const auto request = playRequest;
        playRequest = nullptr;
        if (! join)
            return;
        // `at` is on Player 1's clock: convert it to ours, then to wall-clock time for the script.
        const double localAt = number (request, "at") - sessionPtr->player1ClockOffset();
        double pos = number (request, "pos");
        double delay = localAt - now();
        if (delay < 0.05)
        {
            // Joining late: start where everyone else is by now.
            const auto& song = sessionPtr->replica().confirmed().find (Doc::rootId)->props;
            pos += (-delay + 0.15) * number (song, "tempo", 120.0) / 60.0;
            delay = 0.15;
        }
        const double atWallMs = static_cast<double> (juce::Time::currentTimeMillis()) + delay * 1000.0;
        sendToLive (Json { { "type", "play_at" }, { "pos", pos }, { "at", atWallMs } });
        joinedPlay = true;
    }

    void Engine::stopTogether()
    {
        sessionPtr->broadcast (Json { { "type", "stop_play" }, { "by", self.peer } });
        sendToLive (Json { { "type", "stop_play" } });
        joinedPlay = false;
    }

    void Engine::setPlayTogetherMode (const std::string& mode)
    {
        if (mode == "ask" || mode == "auto" || mode == "never")
        {
            playMode = mode;
            saveSettings();
        }
    }

    // ================================================================== suggestion mode

    void Engine::makeIdea (const std::string& trackId)
    {
        if (! liveBound || trackName (trackId).empty())
            return;
        const auto name = trackName (trackId) + " (" + self.name + "'s idea)";
        sendToLive (Json { { "type", "idea" }, { "track", trackId }, { "name", name },
                           { "color", kLiveColours[static_cast<size_t> (std::abs (self.color)) % 8] } });
    }

    void Engine::makeIdeaOnSelectedTrack()
    {
        auto it = presence.find (self.peer);
        if (it == presence.end() || it->second.track.empty() || trackName (it->second.track).empty())
        {
            notice ("Select the track you want to try an idea on in Live first.");
            return;
        }
        makeIdea (it->second.track);
    }

    void Engine::acceptIdea (const std::string& ideaTrack)
    {
        if (! sessionPtr->isPlayer1())
            return;
        const auto& doc = sessionPtr->replica().confirmed();
        auto* idea = doc.find (ideaTrack);
        if (idea == nullptr)
            return;
        const auto orig = str (idea->props, "idea_of");
        auto* original = doc.find (orig);
        Json props { { "idea_of", nullptr }, { "idea_by", nullptr }, { "mute", false } };
        if (original != nullptr)
        {
            props["name"] = str (original->props, "name");
            props["color"] = integer (original->props, "color");
            props["mute"] = boolean (original->props, "mute");
        }
        Ops ops;
        if (original != nullptr)
            ops.push_back (Op::remove (orig));
        ops.push_back (Op::set (ideaTrack, props));
        applyLocalAndSubmit (ops);
        notice ("Idea accepted.");
    }

    void Engine::rejectIdea (const std::string& ideaTrack)
    {
        const auto& doc = sessionPtr->replica().confirmed();
        auto* idea = doc.find (ideaTrack);
        if (idea == nullptr)
            return;
        if (! sessionPtr->isPlayer1() && str (idea->props, "idea_by") != self.peer)
            return;
        applyLocalAndSubmit ({ Op::remove (ideaTrack) });
    }

    // ================================================================== identity and settings

    void Engine::setIdentity (const std::string& name, int color)
    {
        if (! name.empty())
            self.name = name;
        self.color = ((color % 8) + 8) % 8;
        saveSettings();
        sessionPtr->updateSelf (self);
        broadcastStatus();
    }

    void Engine::loadSettings()
    {
        auto file = options.dataDir.getChildFile ("settings.json");
        auto j = parseJson (file.loadFileAsString().toStdString()).value_or (Json::object());
        self.peer = str (j, "peer");
        if (self.peer.empty())
            self.peer = newPeerId();
        self.name = str (j, "name", options.name.toStdString());
        if (self.name.empty())
            self.name = juce::SystemStats::getFullUserName().upToFirstOccurrenceOf (" ", false, false).toStdString();
        if (self.name.empty())
            self.name = juce::SystemStats::getComputerName().toStdString();
        self.color = static_cast<int> (integer (j, "color", static_cast<int64_t> (std::hash<std::string> {} (self.peer) % 8)));
        playMode = str (j, "play", "ask");
        autoSave = boolean (j, "autosave", true);
        autoSaveSeconds = static_cast<int> (integer (j, "autosave_s", autoSave ? 60 : 0));
        autoSave = autoSaveSeconds > 0;
        if (const auto& linked = field (j, "linked"); linked.is_object())
            for (const auto& [song, path] : linked.items())
                if (path.is_string())
                    linkedFolders[song] = juce::File (juce::String (path.get<std::string>()));
        autoGet = boolean (j, "autoget", true);
        lockTracks = boolean (j, "lock", false);
        if (const auto& t = field (j, "trusted"); t.is_object())
            for (const auto& [peer, name] : t.items())
                if (name.is_string())
                    trustedPeers[peer] = name.get<std::string>();
        if (const auto& last = field (j, "last"); last.is_object())
        {
            lastSessionId = str (last, "session");
            lastHost = str (last, "host");
            lastSong = str (last, "song");
            rejoinWanted = boolean (last, "rejoin");
            if (const auto f = str (last, "file"); ! f.empty())
                lastSongFile = juce::File (juce::String (f));
        }
        if (const auto b = str (j, "backup"); ! b.empty())
            backupFolder = juce::File (juce::String (b));
        if (const auto& known = field (j, "known"); known.is_array())
            for (const auto& ip : known)
                if (ip.is_string() && tailscale::isTailscaleAddress (ip.get<std::string>()))
                    knownPeers.insert (ip.get<std::string>());
        saveSettings();
    }

    void Engine::saveSettings() const
    {
        auto file = options.dataDir.getChildFile ("settings.json");
        auto j = parseJson (file.loadFileAsString().toStdString()).value_or (Json::object());
        j["peer"] = self.peer;
        j["name"] = self.name;
        j["color"] = self.color;
        j["play"] = playMode;
        j["autosave"] = autoSave;
        j["autosave_s"] = autoSaveSeconds;
        Json linked = Json::object();
        for (const auto& [song, folder] : linkedFolders)
            linked[song] = folder.getFullPathName().toStdString();
        j["linked"] = linked;
        j["autoget"] = autoGet;
        j["lock"] = lockTracks;
        Json trusted = Json::object();
        for (const auto& [peer, name] : trustedPeers)
            trusted[peer] = name;
        j["trusted"] = trusted;
        j["last"] = Json { { "session", lastSessionId }, { "host", lastHost }, { "song", lastSong }, { "rejoin", rejoinWanted },
                           { "file", lastSongFile.getFullPathName().toStdString() } };
        j["backup"] = backupFolder.getFullPathName().toStdString();
        j["known"] = Json (std::vector<std::string> (knownPeers.begin(), knownPeers.end()));
        file.replaceWithText (juce::String (j.dump (2)));
    }

    // ================================================================== view

    EngineView Engine::view() const
    {
        EngineView v;
        const auto& s = *sessionPtr;
        const double t = now();
        v.tailscaleInstalled = tailscaleInstalled;
        v.tailscaleRunning = tailscaleStatus && tailscaleStatus->running();
        v.selfAddress = self.addr;
        v.liveConnected = liveLink != 0;
        v.liveNeedsRestart = liveScriptOutdated();
        v.liveBound = liveBound;
        v.liveVersion = self.live;
        v.liveStatus = liveStatus;
        v.role = s.role();
        v.session = s.info();
        v.canTakeOver = s.canTakeOver (t);
        v.askedForP1 = s.player1Requested();
        v.myName = self.name;
        v.myColor = self.color;
        v.playTogetherMode = playMode;
        v.startStatus = startStatus;
        v.staleCount = static_cast<int> (stale.size());
        v.notices.assign (notices.begin(), notices.end());
        if (! playRequest.is_null())
        {
            const auto by = str (playRequest, "by");
            const auto* who = s.info().member (by);
            v.playRequestBy = who != nullptr ? who->name : by;
            v.playRequestPos = barOf (number (playRequest, "pos"));
        }
        for (const auto& p : s.pendingJoinRequests())
            v.joinRequests.push_back (p);
        for (const auto& p : s.pendingPlayer1Requests())
            v.p1Requests.push_back (p);

        for (const auto& [peer, hello] : peerHellos)
        {
            EngineView::Peer p;
            p.peer = peer;
            p.name = str (hello, "name");
            p.addr = str (hello, "addr");
            p.os = str (hello, "os");
            p.app = str (hello, "app");
            p.color = static_cast<int> (integer (hello, "color"));
            const auto& summary = field (hello, "session");
            if (summary.is_object())
            {
                p.sessionId = str (summary, "id");
                p.song = str (summary, "song");
                p.members = static_cast<int> (integer (summary, "members"));
                p.hosting = str (summary, "p1") == peer;
            }
            v.peers.push_back (p);
        }

        for (const auto& m : s.info().members)
        {
            EngineView::Member mv;
            mv.info = m;
            mv.isMe = m.peer == self.peer;
            mv.isPlayer1 = m.peer == s.info().p1;
            mv.info.online = mv.isMe || s.isOnline (m.peer, t);
            if (auto it = presence.find (m.peer); it != presence.end())
            {
                mv.trackName = trackName (it->second.track);
                mv.bar = barOf (it->second.time);
                mv.playing = it->second.playing;
                mv.recording = it->second.recording;
            }
            v.members.push_back (mv);
        }

        if (s.inSession())
        {
            const auto& doc = s.replica().confirmed();
            for (const auto& id : doc.children (Doc::rootId))
                if (auto* e = doc.find (id); e != nullptr && e->type == "track" && e->props.contains ("idea_of"))
                {
                    EngineView::Idea idea;
                    idea.track = id;
                    idea.orig = str (e->props, "idea_of");
                    idea.by = str (e->props, "idea_by");
                    const auto* who = s.info().member (idea.by);
                    idea.byName = who != nullptr ? who->name : std::string ("Someone");
                    idea.name = str (e->props, "name");
                    idea.origName = trackName (idea.orig);
                    v.ideas.push_back (idea);
                }
        }

        for (const auto& [key, tr] : transfers)
            (tr.kind == "preview" ? v.previewsWaiting : v.filesWaiting)++;

        v.updateAvailable = s.inSession() && ! latestSnapshot.empty() && latestSnapshot != currentSnapshot && ! reloading;
        v.updateBy = updateBy;
        {
            std::set<std::string> from, to;
            for (const auto& [key, tr] : transfers)
                if (tr.active && ! tr.from.empty())
                    from.insert (memberName (tr.from));
            for (const auto& out : serving)
                for (const auto& [peer, linkId] : peerLinks)
                    if (linkId == out.link)
                        to.insert (memberName (peer));
            auto names = [] (const std::set<std::string>& s) {
                juce::StringArray a;
                for (const auto& n : s)
                    a.add (juce::String (n));
                return a.joinIntoString (", ").toStdString();
            };
            v.download = downloads.view (now(), names (from));
            v.upload = serving.empty() ? EngineView::Transfer {} : uploads.view (now(), names (to));
        }
        if (s.inSession())
        {
            v.songFile = currentSong.getFullPathName().toStdString();
            v.updateWhenStopped = ! pendingReload.is_null();
            v.saveToShare = mySaveNeeded;
            v.saveToShare.insert (v.saveToShare.end(), myPluginChanges.begin(), myPluginChanges.end());
            for (const auto& [peer, entry] : othersSaveNeeded)
                for (const auto& what : entry.second)
                    v.waitingForSave.push_back ({ entry.first, what });
        }
        v.backupFolder = backupFolder.getFullPathName().toStdString();
        v.canSaveForYou = static_cast<bool> (options.pressSaveInLive);
        v.autoSave = autoSave;
        v.autoSaveSeconds = autoSaveSeconds;
        if (s.inSession() && isLinkedFolder (currentSong.getParentDirectory()))
            v.linkedFolder = currentSong.getParentDirectory().getFullPathName().toStdString();
        v.saveNeedsPermission = saveNeedsPermission;
        v.autoGet = autoGet;
        v.lockTracks = s.inSession() ? sessionLock : lockTracks;
        v.canChangeLock = ! s.inSession() || s.isPlayer1();
        v.undoSteps = static_cast<int> (undoStack.size());
        for (const auto& m : messageLog)
            if (v.messages.size() < 8)
                v.messages.push_back ({ m.when.formatted ("%H:%M").toStdString(), m.who, m.text, m.track, m.time });
        v.pluginTable = buildPluginTable();
        if (! s.inSession() && copyOfferMeta.is_object())
            v.copyOffer = str (copyOfferMeta, "song", "the song");
        for (const auto& [when, text] : activityLog)
            if (v.activity.size() < 6)
                v.activity.push_back ((when.formatted ("%H:%M") + "  " + juce::String (text)).toStdString());
        v.pluginProblems = pluginProblems;
        for (const auto& [peer, name] : trustedPeers)
            v.trusted.push_back (name);
        if (! s.inSession() && lastSongFile.existsAsFile())
        {
            v.continueSong = lastSongFile.getFullPathName().toStdString();
            v.continueName = lastSong.empty() ? lastSongFile.getFileNameWithoutExtension().toStdString() : lastSong;
        }
        if (s.inSession())
        {
            std::vector<Json> metas;
            for (const auto& f : snapshotsDir().findChildFiles (juce::File::findFiles, false, "*.json"))
                if (auto j = parseJson (f.loadFileAsString().toStdString()); j && j->is_object())
                    metas.push_back (*j);
            std::sort (metas.begin(), metas.end(), [] (const Json& a, const Json& b) { return str (a, "at") > str (b, "at"); });
            for (const auto& m : metas)
            {
                EngineView::Version ver;
                ver.id = str (m, "id");
                const auto t = juce::Time::fromISO8601 (juce::String (str (m, "at")));
                ver.when = t.formatted ("%a %d %b, %H:%M").toStdString();
                ver.byName = str (m, "byName");
                ver.label = str (m, "label");
                ver.summary = str (m, "summary", str (m, "prev").empty() ? "Session started" : "Saved");
                ver.current = ver.id == currentSnapshot;
                v.versions.push_back (ver);
            }
        }
        return v;
    }
}
