#include "Engine.h"

#include "Platform.h"
#include "TuttiVersion.h"
#include "tutti/Als.h"
#include "tutti/Ids.h"

namespace tutti
{
    namespace
    {
        std::pair<juce::String, int> splitAddr (const std::string& addr, int defaultPort)
        {
            const juce::String a (addr);
            if (a.containsChar (':'))
                return { a.upToLastOccurrenceOf (":", false, false), a.fromLastOccurrenceOf (":", false, false).getIntValue() };
            return { a, defaultPort };
        }

        bool isHash (const juce::String& s)
        {
            return s.length() == 64 && s.containsOnly ("0123456789abcdef");
        }
    }

    // ================================================================== lifecycle

    Engine::Engine (EngineOptions o)
        : options (std::move (o)),
          contentStore ((options.dataDir == juce::File() ? platform::dataDir() : options.dataDir).getChildFile ("Store"))
    {
        if (options.dataDir == juce::File())
            options.dataDir = platform::dataDir();
        if (options.songsDir == juce::File())
            options.songsDir = platform::songsDir();
        if (! options.openInLive)
            options.openInLive = [] (const juce::File& f) { return platform::openInLive (f); };
        if (options.appVersion.isEmpty())
            options.appVersion = version::string;
        if (options.os.isEmpty())
            options.os = platform::osName();
        options.dataDir.createDirectory();
        options.songsDir.createDirectory();

        loadSettings();
        self.app = options.appVersion.toStdString();
        self.os = options.os.toStdString();
        const auto firstCseq = static_cast<uint64_t> (juce::Time::currentTimeMillis()) * 1000u;
        sessionPtr = std::make_unique<Session> (self, static_cast<Session::Listener&> (*this), Session::Timing {}, firstCseq);
    }

    Engine::~Engine() { shutdown(); }

    void Engine::startup()
    {
        startServers();
        discover();
    }

    void Engine::shutdown()
    {
        if (sessionPtr != nullptr && sessionPtr->inSession())
            sessionPtr->leave (now());
        peerServer.stop();
        localServer.stop();
        for (auto& [id, l] : links)
            l.link->close();
        links.clear();
        peerLinks.clear();
    }

    double Engine::now() const { return juce::Time::getMillisecondCounterHiRes() / 1000.0; }

    void Engine::post (std::function<void()> fn)
    {
        std::lock_guard<std::mutex> g (queueLock);
        queue.push_back (std::move (fn));
    }

    void Engine::pump()
    {
        std::deque<std::function<void()>> work;
        {
            std::lock_guard<std::mutex> g (queueLock);
            work.swap (queue);
        }
        const bool busy = ! work.empty();
        for (auto& fn : work)
            fn();

        const double t = now();
        sessionPtr->tick (t);
        if (t - lastDiscovery > (sessionPtr->inSession() ? 20.0 : 8.0))
            discover();
        pumpTransfers();
        serveChunks();
        pollSave();
        if (sessionPtr->isPlayer1())
            processSaves();
        if (! standIns.empty() && t - lastStandInUpdate > 1.0)
        {
            lastStandInUpdate = t;
            updateStandIns();
        }
        if (reloadWhenMerged && ! reloading)
            if (auto meta = loadSnapshot (latestSnapshot); meta && contentStore.has (str (*meta, "als")))
                reloadInto (*meta);

        if (! pendingPresence.is_null() && t - lastPresenceSent > 0.25)
        {
            sessionPtr->broadcast (pendingPresence);
            pendingPresence = nullptr;
            lastPresenceSent = t;
        }
        if (! playRequest.is_null() && t - playRequestAt > 8.0)
            playRequest = nullptr;
        if (liveLink != 0 && ! liveBound && sessionPtr->inSession() && t - lastBindAttempt > 3.0)
            bindLive();

        if ((busy || t - lastTick > 0.5) && onChange)
        {
            lastTick = t;
            onChange();
        }
    }

    void Engine::notice (const std::string& text)
    {
        notices.push_front (text);
        while (notices.size() > 12)
            notices.pop_back();
        log (text);
    }

    void Engine::log (const std::string& text)
    {
        auto dir = platform::logsDir();
        if (options.dataDir != platform::dataDir())
            dir = options.dataDir.getChildFile ("Logs");
        dir.createDirectory();
        auto file = dir.getChildFile ("tutti.log");
        if (file.getSize() > 2 * 1024 * 1024)
            file.moveFileTo (dir.getChildFile ("tutti.old.log"));
        file.appendText (juce::Time::getCurrentTime().toISO8601 (true) + "  " + juce::String (text) + "\n");
    }

    // ================================================================== networking

    void Engine::startServers()
    {
        localServer.start (options.localPort, "127.0.0.1", [this] (std::unique_ptr<juce::StreamingSocket> s) {
            auto shared = std::make_shared<std::unique_ptr<juce::StreamingSocket>> (std::move (s));
            post ([this, shared] { adoptSocket (std::move (*shared), false, true, "local"); });
        });

        juce::String bind = options.bindAddress;
        if (bind.isEmpty() && tailscaleStatus && tailscaleStatus->running())
            bind = tailscaleStatus->selfIp4;
        if (bind.isEmpty())
            return; // no Tailscale yet: don't listen on other networks
        if (peerServer.running() && peerServer.address() == bind)
            return;
        if (peerServer.start (options.peerPort, bind, [this] (std::unique_ptr<juce::StreamingSocket> s) {
                auto shared = std::make_shared<std::unique_ptr<juce::StreamingSocket>> (std::move (s));
                post ([this, shared] { adoptSocket (std::move (*shared), false, false, ""); });
            }))
        {
            self.addr = (bind + ":" + juce::String (options.peerPort)).toStdString();
            sessionPtr->updateSelf (self);
            log ("listening on " + self.addr);
        }
        else
        {
            notice ("Couldn't listen on " + bind.toStdString() + ":" + std::to_string (options.peerPort)
                    + " (is another Tutti running?)");
        }
    }

    void Engine::discover()
    {
        lastDiscovery = now();
        for (const auto& extra : options.extraPeers)
        {
            const auto [host, port] = splitAddr (extra.toStdString(), options.peerPort);
            connectPeer (host, port);
        }
        if (! options.useTailscale || discovering)
            return;
        discovering = true;
        juce::Thread::launch ([this] {
            auto status = tailscale::query();
            const bool installed = tailscale::findCli() != juce::File();
            post ([this, status, installed] {
                discovering = false;
                tailscaleInstalled = installed;
                tailscaleStatus = status;
                if (! status || ! status->running())
                    return;
                startServers();
                for (const auto& p : status->peers)
                    if (p.online)
                        connectPeer (p.ip4, options.peerPort);
            });
        });
    }

    void Engine::connectPeer (const juce::String& host, int port)
    {
        const auto key = host + ":" + juce::String (port);
        if (key.toStdString() == self.addr)
            return;
        for (const auto& [id, l] : links)
            if (! l.local && l.addr == key.toStdString())
                return;
        auto it = dialing.find (key);
        if (it != dialing.end() && now() - it->second < 15.0)
            return;
        dialing[key] = now();
        net::connectAsync (host, port, 3000, [this, key] (std::unique_ptr<juce::StreamingSocket> s) {
            if (s == nullptr)
                return;
            auto shared = std::make_shared<std::unique_ptr<juce::StreamingSocket>> (std::move (s));
            post ([this, shared, key] { adoptSocket (std::move (*shared), true, false, key); });
        });
    }

    void Engine::adoptSocket (std::unique_ptr<juce::StreamingSocket> socket, bool outgoing, bool local, const juce::String& addr)
    {
        if (socket == nullptr)
            return;
        const auto id = nextLinkId++;
        auto link = std::make_shared<net::Link> (std::move (socket), id, outgoing);
        LinkState state;
        state.link = link;
        state.local = local;
        state.addr = addr.toStdString();
        state.role = local ? "" : "peer";
        links[id] = state;
        link->start ([this] (uint64_t lid, protocol::Frame f) {
                         auto shared = std::make_shared<protocol::Frame> (std::move (f));
                         post ([this, lid, shared] { onFrame (lid, std::move (*shared)); });
                     },
                     [this] (uint64_t lid) { post ([this, lid] { onClosed (lid); }); });
        if (! local)
            sendHello (links[id]);
    }

    Json Engine::helloMessage() const
    {
        Json summary = nullptr;
        const auto& s = *sessionPtr;
        if (s.inSession())
            summary = Json { { "id", s.info().id }, { "song", s.info().song }, { "p1", s.info().p1 },
                             { "epoch", s.info().epoch }, { "members", s.info().members.size() } };
        return Json { { "type", "hello" }, { "proto", protocol::kProtocolVersion }, { "peer", self.peer },
                      { "name", self.name }, { "color", self.color }, { "os", self.os }, { "app", self.app },
                      { "live", self.live }, { "addr", self.addr }, { "session", summary } };
    }

    void Engine::sendHello (LinkState& state)
    {
        state.link->send (protocol::encode (helloMessage()));
    }

    void Engine::broadcastStatus()
    {
        const auto bytes = protocol::encode (helloMessage());
        for (auto& [id, l] : links)
            if (! l.local)
                l.link->send (bytes);
    }

    void Engine::sendTo (uint64_t linkId, const Json& message)
    {
        auto it = links.find (linkId);
        if (it != links.end())
            it->second.link->send (protocol::encode (message));
    }

    void Engine::onFrame (uint64_t linkId, protocol::Frame frame)
    {
        auto it = links.find (linkId);
        if (it == links.end())
            return;
        auto& state = it->second;

        if (state.local)
        {
            if (frame.kind != protocol::Frame::Kind::json)
                return;
            const auto type = str (frame.json, "type");
            if (type == "hello")
            {
                state.role = str (frame.json, "role", "live");
                if (state.role == "live")
                {
                    if (liveLink != 0 && liveLink != linkId)
                        if (auto old = links.find (liveLink); old != links.end())
                            old->second.link->close();
                    liveLink = linkId;
                    liveHello = frame.json;
                    liveBound = false;
                    self.live = str (frame.json, "live");
                    sessionPtr->updateSelf (self);
                    sendToLive (Json { { "type", "welcome" }, { "peer", self.peer }, { "name", self.name } });
                    bindLive();
                }
                else if (state.role == "standin")
                    handleStandIn (linkId, frame.json);
                return;
            }
            if (state.role == "live" && linkId == liveLink)
                handleLive (frame.json);
            else if (state.role == "standin")
                handleStandIn (linkId, frame.json);
            return;
        }

        if (frame.kind == protocol::Frame::Kind::binary)
        {
            if (! state.peer.empty())
                handleFileMessage (state.peer, frame.json, &frame);
            return;
        }
        const auto type = str (frame.json, "type");
        if (type == "hello")
        {
            handlePeerHello (linkId, frame.json);
            return;
        }
        if (state.peer.empty())
            return;
        sessionPtr->receive (state.peer, frame.json, now());
    }

    void Engine::handlePeerHello (uint64_t linkId, const Json& hello)
    {
        auto it = links.find (linkId);
        if (it == links.end())
            return;
        auto& state = it->second;
        const auto peer = str (hello, "peer");
        if (integer (hello, "proto") != protocol::kProtocolVersion)
        {
            notice (str (hello, "name", "Someone") + " has a different Tutti version; updates usually fix this within the hour.");
            state.link->close();
            return;
        }
        if (peer.empty() || peer == self.peer)
        {
            state.link->close();
            return;
        }

        if (state.peer == peer)
        {
            peerHellos[peer] = hello; // a status update on an existing link
            if (onChange)
                onChange();
            return;
        }

        if (auto existing = peerLinks.find (peer); existing != peerLinks.end() && existing->second != linkId)
        {
            // Both sides dialled each other: keep the link started by the smaller peer id.
            const bool keepNew = self.peer < peer ? state.link->outgoing() : ! state.link->outgoing();
            if (! keepNew)
            {
                state.link->close();
                return;
            }
            auto old = existing->second;
            peerLinks.erase (existing);
            if (auto o = links.find (old); o != links.end())
            {
                o->second.peer.clear();
                o->second.link->close();
            }
        }

        state.peer = peer;
        const auto addr = str (hello, "addr");
        if (! addr.empty())
            state.addr = addr;
        peerLinks[peer] = linkId;
        peerHellos[peer] = hello;
        sessionPtr->connected (peer, now());
        if (onChange)
            onChange();
    }

    void Engine::onClosed (uint64_t linkId)
    {
        auto it = links.find (linkId);
        if (it == links.end())
            return;
        const auto state = it->second;
        links.erase (it);

        standIns.erase (linkId);
        if (state.local && linkId == liveLink)
        {
            liveLink = 0;
            liveBound = false;
            liveHello = nullptr;
            liveStatus = "Live isn't connected";
        }
        if (! state.peer.empty())
        {
            auto p = peerLinks.find (state.peer);
            if (p != peerLinks.end() && p->second == linkId)
            {
                peerLinks.erase (p);
                peerHellos.erase (state.peer);
                sessionPtr->disconnected (state.peer, now());
            }
        }
        serving.erase (std::remove_if (serving.begin(), serving.end(), [linkId] (const Outgoing& o) { return o.link == linkId; }),
                       serving.end());
        for (auto& [key, t] : transfers)
            if (t.active && t.from == state.peer)
                t.active = false;
        if (onChange)
            onChange();
    }

    // ================================================================== Session::Listener

    void Engine::send (const std::string& peer, const Json& message)
    {
        auto it = peerLinks.find (peer);
        if (it != peerLinks.end())
            sendTo (it->second, message);
    }

    void Engine::connectTo (const MemberInfo& member)
    {
        if (member.peer == self.peer || peerLinks.count (member.peer) != 0 || member.addr.empty())
            return;
        const auto [host, port] = splitAddr (member.addr, options.peerPort);
        connectPeer (host, port);
    }

    void Engine::liveUpdate (const LiveUpdate& u)
    {
        for (const auto& op : u.apply)
            if (op.props.contains ("file") && op.props["file"].is_string())
                wantFile (op.props["file"].get<std::string>(), true);
        if (! liveBound)
            return;
        if (! u.apply.empty())
            sendToLive (Json { { "type", "apply" }, { "ops", toJson (toLive (u.apply)) } });
        if (! u.reconcile.empty())
            sendToLive (Json { { "type", "reconcile" }, { "items", toLive (u.reconcile) } });
    }

    void Engine::fullResync()
    {
        if (liveBound)
            sendToLive (Json { { "type", "full" }, { "doc", liveDoc (sessionPtr->replica().optimistic()) } });
    }

    void Engine::sessionChanged()
    {
        const auto& info = sessionPtr->info();
        if (sessionPtr->inSession() && ! info.snapshot.empty() && info.snapshot != latestSnapshot
            && ! currentSnapshot.empty() && ! sessionPtr->isPlayer1() && ! loadSnapshot (info.snapshot))
            send (info.p1, Json { { "type", "snap_req" }, { "id", info.snapshot }, { "session", info.id }, { "notice", true } });
        broadcastStatus();
        if (onChange)
            onChange();
    }

    void Engine::joinRequested (const MemberInfo& m)
    {
        notice (m.name + " wants to join.");
    }

    void Engine::player1Requested (const MemberInfo& m)
    {
        notice (m.name + " asked to be Player 1.");
    }

    void Engine::player1Denied()
    {
        notice ("Player 1 said not now.");
    }

    void Engine::joined()
    {
        notice ("You joined " + sessionPtr->info().song + ".");
        requestSnapshot();
    }

    void Engine::left (const std::string& reason)
    {
        notice (reason);
        if (liveLink != 0)
            sendToLive (Json { { "type", "unbind" } });
        liveBound = false;
        currentSong = juce::File();
        currentSongName.clear();
        currentMap = nullptr;
        currentNotes = nullptr;
        joinSnapshot = nullptr;
        joinedWaitingForSong = false;
        stale.clear();
        presence.clear();
        pendingSave = nullptr;
        saveQueue.clear();
        latestSnapshot.clear();
        currentSnapshot.clear();
        reloadWhenMerged = false;
        reloading = false;
        updateBy.clear();
    }

    void Engine::appMessage (const std::string& peer, const Json& message)
    {
        const auto type = str (message, "type");
        if (type == "snap_req")
            handleSnapshotRequest (peer, message);
        else if (type == "snap")
            handleSnapshot (peer, message);
        else if (type == "get" || type == "missing")
            handleFileMessage (peer, message, nullptr);
        else if (type == "presence")
            handlePresence (peer, message);
        else if (type == "save")
            handleSave (peer, message);
        else if (type == "snapshot")
            handleSnapshotNotice (peer, message);
        else if (type == "label")
        {
            if (auto meta = loadSnapshot (str (message, "id")))
            {
                (*meta)["label"] = str (message, "label");
                storeSnapshot (*meta);
            }
        }
        else if (type == "play_req" && sessionPtr->isPlayer1())
        {
            Json play { { "type", "play" }, { "pos", number (message, "pos") }, { "at", now() + 0.6 }, { "by", peer } };
            sessionPtr->broadcast (play);
            handlePlay (play);
        }
        else if (type == "play")
            handlePlay (message);
        else if (type == "stop_play")
        {
            if (joinedPlay)
                sendToLive (Json { { "type", "stop_play" } });
            joinedPlay = false;
            playRequest = nullptr;
        }
    }

    // ================================================================== Live

    void Engine::sendToLive (const Json& message)
    {
        if (liveLink != 0)
            sendTo (liveLink, message);
    }

    bool Engine::songOpenInLive() const
    {
        const auto& fp = field (liveHello, "fingerprint");
        juce::StringArray names;
        if (const auto& list = field (fp, "tracks"); list.is_array())
            for (const auto& n : list)
                names.add (juce::String (n.is_string() ? n.get<std::string>() : std::string()));
        const auto target = waitingForCapture ? pendingStartCopy : currentSong;
        if (target == juce::File() || ! target.existsAsFile())
            return false;
        static std::map<juce::String, std::pair<juce::int64, std::vector<als::Track>>> cache;
        auto& entry = cache[target.getFullPathName()];
        const auto stamp = target.getLastModificationTime().toMilliseconds();
        if (entry.first != stamp)
        {
            juce::String error;
            auto xml = als::load (target, error);
            entry = { stamp, xml ? als::tracks (*xml) : std::vector<als::Track> {} };
        }
        return als::matchScore (entry.second, names) >= 0.8;
    }

    void Engine::bindLive()
    {
        lastBindAttempt = now();
        if (liveLink == 0)
            return;
        auto& s = *sessionPtr;
        if (! s.inSession())
        {
            if (waitingForCapture && songOpenInLive())
            {
                liveStatus = "Reading your song...";
                startStatus = "Reading your song in Live...";
                sendToLive (Json { { "type", "capture" } });
            }
            else
                liveStatus = waitingForCapture ? "Waiting for Live to open the song..." : "Ready";
            return;
        }
        if (currentSong == juce::File())
        {
            liveStatus = "Downloading the song...";
            return;
        }
        if (! songOpenInLive())
        {
            liveStatus = "Open the session song in Live";
            return;
        }
        const auto doc = liveDoc (s.replica().optimistic());
        if (boolean (liveHello, "bound") && str (liveHello, "session") == s.info().id)
            sendToLive (Json { { "type", "full" }, { "doc", doc } });
        else
            sendToLive (Json { { "type", "bind" }, { "session", s.info().id }, { "doc", doc },
                               { "map", currentMap.is_null() ? Json::object() : currentMap },
                               { "notes", currentNotes.is_null() ? Json::object() : currentNotes } });
        liveStatus = "Connecting your Live...";
    }

    void Engine::handleLive (const Json& m)
    {
        const auto type = str (m, "type");
        if (type == "ops")
        {
            if (liveBound && sessionPtr->inSession())
                if (auto ops = opsFromJson (field (m, "ops")))
                    submitFromLive (std::move (*ops));
        }
        else if (type == "captured")
        {
            if (waitingForCapture)
                finishStart (m);
        }
        else if (type == "structure")
        {
            if (! pendingSave.is_null() && integer (m, "req") == structureRequest)
            {
                pendingSave["map"] = field (m, "map");
                pendingSave["notes"] = field (m, "notes");
                if (pendingSave.contains ("als"))
                    sendSave (std::exchange (pendingSave, nullptr));
            }
        }
        else if (type == "bound")
        {
            if (reloading)
            {
                // Updated: the other copy of the song file is now stale.
                reloading = false;
                const auto base = currentSong.getFileNameWithoutExtension().upToLastOccurrenceOf (" (Tutti)", false, false);
                for (const auto& name : { base + ".als", base + " (Tutti).als" })
                    if (auto f = currentSong.getSiblingFile (name); f != currentSong)
                        f.deleteFile();
                notice ("Your Live is up to date.");
            }
            liveBound = true;
            liveStatus = "In sync";
            stale.clear();
            const auto& stats = field (m, "stats");
            log ("Live bound: " + stats.dump());
            if (integer (stats, "new") > 0)
                notice ("Some things in your Live weren't in the shared song, so they were shared.");
            joinedWaitingForSong = false;
        }
        else if (type == "presence")
        {
            Presence p;
            p.track = str (m, "track");
            p.clip = str (m, "clip");
            p.time = number (m, "time");
            p.playing = boolean (m, "playing");
            p.recording = boolean (m, "rec");
            presence[self.peer] = p;
            pendingPresence = m;
            pendingPresence["type"] = "presence";
        }
        else if (type == "stale")
        {
            const bool first = stale.empty();
            if (const auto& ids = field (m, "ids"); ids.is_array())
                for (const auto& id : ids)
                    if (id.is_string())
                        stale.insert (id.get<std::string>());
            if (first)
                notice ("Some changes can't be copied into your Live directly: they'll arrive with the next Update.");
        }
        else if (type == "idea_created")
        {
            const auto track = str (m, "track");
            if (track.empty())
                notice (str (m, "error", "Couldn't make the idea copy."));
            else
                sessionPtr->submitLocal ({ Op::set (track, { { "idea_of", str (m, "orig") }, { "idea_by", self.peer } }) }, now());
        }
        else if (type == "log")
        {
            log ("live: " + str (m, "text"));
        }
    }

    Json Engine::liveDoc (const Doc& doc) const
    {
        Json j = doc.toJson();
        for (auto& e : j["e"])
        {
            auto& v = e["v"];
            if (v.contains ("file") && v["file"].is_string())
            {
                const auto hash = v["file"].get<std::string>();
                const auto local = contentStore.bestFor (hash);
                if (local != juce::File())
                    v["path"] = local.getFullPathName().toStdString();
                else
                    v.erase ("path");
            }
        }
        return j;
    }

    Ops Engine::toLive (const Ops& ops) const
    {
        Ops out = ops;
        for (auto& op : out)
            if (op.props.contains ("file") && op.props["file"].is_string())
            {
                const auto local = contentStore.bestFor (op.props["file"].get<std::string>());
                if (local != juce::File())
                    op.props["path"] = local.getFullPathName().toStdString();
            }
        return out;
    }

    Json Engine::toLive (const Json& items) const
    {
        Json out = items;
        for (auto& item : out)
            if (item["state"].is_array())
                for (auto& e : item["state"])
                    if (e["v"].contains ("file") && e["v"]["file"].is_string())
                    {
                        const auto local = contentStore.bestFor (e["v"]["file"].get<std::string>());
                        if (local != juce::File())
                            e["v"]["path"] = local.getFullPathName().toStdString();
                    }
        return out;
    }

    void Engine::submitFromLive (Ops ops)
    {
        // Local audio paths become content hashes. Hashing a big file takes a moment, so a batch
        // that needs it waits (and so does everything after it, to keep the order).
        if (! ops.empty())
            pendingLocalBatches.push_back (toJson (ops));
        while (! pendingLocalBatches.empty())
        {
            auto& front = pendingLocalBatches.front();
            bool ready = true;
            for (auto& op : front)
            {
                auto& v = op["v"];
                if (! v.is_object() || ! v.contains ("path"))
                    continue;
                if (! v["path"].is_string())
                {
                    v.erase ("path");
                    continue;
                }
                const juce::File path (juce::String (v["path"].get<std::string>()));
                std::string hash;
                if (contentStore.isPreviewPath (path) && isHash (path.getParentDirectory().getFileName()))
                    hash = path.getParentDirectory().getFileName().toStdString();
                else if (path.isAChildOf (contentStore.root().getChildFile ("files")))
                    hash = path.getParentDirectory().getFileName().toStdString();
                if (hash.empty())
                {
                    const auto key = path.getFullPathName().toStdString();
                    if (auto known = pathHashes.find (key); known != pathHashes.end())
                    {
                        if (! known->second.empty())
                            v["file"] = known->second;
                        v.erase ("path");
                        continue;
                    }
                    if (preparing.count (key) != 0)
                    {
                        ready = false;
                        continue;
                    }
                    if (! path.existsAsFile())
                    {
                        v.erase ("path");
                        continue;
                    }
                    preparing.insert (key);
                    ready = false;
                    juce::Thread::launch ([this, path, key] {
                        juce::String error;
                        auto h = contentStore.add (path, error);
                        if (h)
                            audio::ensurePreview (contentStore, *h, error);
                        const auto hash = h.value_or (std::string());
                        post ([this, key, hash, error] {
                            preparing.erase (key);
                            if (hash.empty())
                                log ("couldn't add " + key + " to the store: " + error.toStdString());
                            pathHashes[key] = hash; // empty: give up on this file, drop the path
                            submitFromLive ({});
                        });
                    });
                    continue;
                }
                v["file"] = hash;
                v.erase ("path");
            }
            if (! ready)
                break;
            if (auto parsed = opsFromJson (front); parsed && ! parsed->empty())
                sessionPtr->submitLocal (std::move (*parsed), now());
            pendingLocalBatches.pop_front();
        }
    }

    void Engine::applyLocalAndSubmit (const Ops& ops)
    {
        if (ops.empty())
            return;
        if (liveBound)
            sendToLive (Json { { "type", "apply" }, { "ops", toJson (toLive (ops)) } });
        sessionPtr->submitLocal (ops, now());
    }

    // ================================================================== starting a session

    juce::File Engine::workspaceFor (const std::string& song) const
    {
        auto name = juce::File::createLegalFileName (juce::String (song));
        if (name.isEmpty())
            name = "Song";
        return options.songsDir.getChildFile (name);
    }

    bool Engine::startSession (const juce::File& source, juce::String& error)
    {
        if (sessionPtr->inSession() || waitingForCapture)
        {
            error = "You're already in a session.";
            return false;
        }
        auto set = als::load (source, error);
        if (set == nullptr)
            return false;

        const auto song = source.getFileNameWithoutExtension().toStdString();
        auto folder = workspaceFor (song);
        auto copy = folder.getChildFile (source.getFileName());
        startStatus = "Collecting the song's audio...";
        waitingForCapture = true;
        pendingStartSource = source;
        pendingStartCopy = copy;
        currentSongName = song;

        auto shared = std::shared_ptr<juce::XmlElement> (set.release());
        juce::Thread::launch ([this, shared, source, folder, copy] {
            juce::String err;
            int missing = 0;
            for (auto& ref : als::fileRefs (*shared))
            {
                auto file = als::resolve (ref, source.getParentDirectory());
                if (file == juce::File())
                {
                    ++missing;
                    continue;
                }
                auto hash = contentStore.add (file, err);
                if (! hash)
                {
                    ++missing;
                    continue;
                }
                audio::describe (contentStore, *hash);
                als::rewrite (ref, contentStore.fileFor (*hash), folder);
            }
            folder.createDirectory();
            const bool saved = copy == source || als::save (*shared, copy);
            post ([this, saved, missing, copy] {
                if (! saved)
                {
                    waitingForCapture = false;
                    startStatus.clear();
                    notice ("Couldn't write the session copy of the song.");
                    return;
                }
                if (missing > 0)
                    notice (std::to_string (missing) + " audio file(s) in the song couldn't be found; they'll be missing for everyone.");
                startStatus = "Opening the song in Live...";
                options.openInLive (copy);
                bindLive();
            });
        });
        return true;
    }

    void Engine::finishStart (const Json& captured)
    {
        waitingForCapture = false;
        Json docJson = field (captured, "doc");
        // Audio paths in the captured document become content hashes.
        for (auto& e : docJson["e"])
        {
            auto& v = e["v"];
            if (v.contains ("path") && v["path"].is_string())
            {
                const juce::File path (juce::String (v["path"].get<std::string>()));
                if (auto hash = contentStore.hashOf (path))
                {
                    juce::String error;
                    contentStore.add (path, error);
                    v["file"] = *hash;
                }
                v.erase ("path");
            }
        }
        auto doc = Doc::fromJson (docJson);
        if (! doc)
        {
            startStatus.clear();
            notice ("Live sent a song Tutti couldn't read.");
            return;
        }

        // Snapshot 0: the song file as Live opened it, the structure map and the audio.
        juce::String error;
        auto alsHash = contentStore.add (pendingStartCopy, error);
        if (! alsHash)
        {
            startStatus.clear();
            notice ("Couldn't store the song: " + error.toStdString());
            return;
        }
        std::set<std::string> files;
        for (const auto& id : doc->subtree (Doc::rootId))
            if (auto* e = doc->find (id); e != nullptr && e->props.contains ("file"))
                files.insert (e->props["file"].get<std::string>());
        if (auto set = als::load (pendingStartCopy, error))
            for (auto& ref : als::fileRefs (*set))
            {
                juce::File f (ref.path);
                if (f.isAChildOf (contentStore.root()) && isHash (f.getParentDirectory().getFileName()))
                    files.insert (f.getParentDirectory().getFileName().toStdString());
            }

        const auto sessionId = newSessionId();
        const auto snapshotId = newVersionId();
        Json snap { { "type", "snap" }, { "id", snapshotId }, { "session", sessionId }, { "song", currentSongName },
                    { "file", pendingStartCopy.getFileName().toStdString() }, { "als", *alsHash }, { "seq", 0 },
                    { "map", field (captured, "map") }, { "notes", field (captured, "notes") },
                    { "files", Json (std::vector<std::string> (files.begin(), files.end())) },
                    { "by", self.peer }, { "byName", self.name }, { "at", juce::Time::getCurrentTime().toISO8601 (true).toStdString() } };
        auto dir = options.dataDir.getChildFile ("sessions").getChildFile (sessionId).getChildFile ("snapshots");
        dir.createDirectory();
        dir.getChildFile (snapshotId + ".json").replaceWithText (snap.dump (1));

        currentSong = pendingStartCopy;
        currentMap = field (captured, "map");
        currentNotes = field (captured, "notes");
        currentSnapshot = latestSnapshot = snapshotId;
        songStamp = ignoreStamp = currentSong.getSize() ^ (currentSong.getLastModificationTime().toMilliseconds() << 8);
        sessionPtr->start (sessionId, currentSongName, std::move (*doc), 0, snapshotId, now());
        liveBound = true;
        liveStatus = "In sync";
        startStatus.clear();
        notice ("Session started: " + currentSongName + ". You're Player 1.");

        // Previews for everything, ready for whoever joins.
        for (const auto& hash : files)
            prepareLocalFile (hash);
    }

    // ================================================================== joining

    void Engine::joinSession (const std::string& hostPeer)
    {
        auto it = peerHellos.find (hostPeer);
        if (it == peerHellos.end() || sessionPtr->inSession())
            return;
        MemberInfo host;
        host.peer = hostPeer;
        host.name = str (it->second, "name");
        host.addr = str (it->second, "addr");
        sessionPtr->join (host, now());
        notice ("Asking " + host.name + " to let you in...");
    }

    void Engine::requestSnapshot()
    {
        const auto& info = sessionPtr->info();
        send (info.p1, Json { { "type", "snap_req" }, { "id", info.snapshot }, { "session", info.id } });
    }

    void Engine::handleSnapshotRequest (const std::string& peer, const Json& message)
    {
        const auto id = str (message, "id");
        auto file = options.dataDir.getChildFile ("sessions").getChildFile (juce::String (str (message, "session")))
                        .getChildFile ("snapshots").getChildFile (juce::String (id) + ".json");
        auto snap = parseJson (file.loadFileAsString().toStdString());
        if (! snap)
        {
            send (peer, Json { { "type", "snap" }, { "id", id }, { "error", "missing" } });
            return;
        }
        Json metas = Json::array();
        auto all = field (*snap, "files");
        std::vector<std::string> hashes;
        if (all.is_array())
            for (const auto& h : all)
                hashes.push_back (h.get<std::string>());
        hashes.push_back (str (*snap, "als"));
        for (const auto& h : hashes)
            if (auto m = contentStore.meta (h))
                metas.push_back (m->toJson());
        Json reply = *snap;
        reply["type"] = boolean (message, "notice") ? "snapshot" : "snap";
        reply["metas"] = std::move (metas);
        send (peer, reply);
    }

    void Engine::handleSnapshot (const std::string& peer, const Json& message)
    {
        if (! sessionPtr->inSession() || str (message, "error") == "missing")
        {
            if (sessionPtr->inSession())
                notice ("Player 1 couldn't find the song file to send. Ask them to save (Cmd+S) in Live.");
            return;
        }
        (void) peer;
        if (const auto& metas = field (message, "metas"); metas.is_array())
            for (const auto& mj : metas)
                if (auto m = AudioMeta::fromJson (mj); m && ! contentStore.meta (m->hash))
                    contentStore.writeMeta (*m);

        auto dir = options.dataDir.getChildFile ("sessions").getChildFile (juce::String (sessionPtr->info().id)).getChildFile ("snapshots");
        dir.createDirectory();
        Json keep = message;
        keep.erase ("metas");
        dir.getChildFile (juce::String (str (message, "id")) + ".json").replaceWithText (keep.dump (1));

        joinSnapshot = message;
        joinSnapshot["received_at"] = now();
        wantFile (str (message, "als"), false);
        if (const auto& files = field (message, "files"); files.is_array())
            for (const auto& h : files)
                wantFile (h.get<std::string>(), true);
        startStatus = "Downloading the song...";
        maybeOpenJoinedSong();
    }

    void Engine::maybeOpenJoinedSong()
    {
        if (joinSnapshot.is_null())
            return;
        const auto alsHash = str (joinSnapshot, "als");
        if (! contentStore.has (alsHash))
            return;
        int waiting = 0;
        if (const auto& files = field (joinSnapshot, "files"); files.is_array())
            for (const auto& h : files)
            {
                const auto hash = h.get<std::string>();
                auto m = contentStore.meta (hash);
                const bool audio = m && m->isAudio() && ! m->previewHash.empty();
                if (! contentStore.has (hash) && (! audio || ! contentStore.hasPreviewWav (hash)))
                    ++waiting;
            }
        // Open as soon as every preview is here (or after 30 s with whatever has arrived).
        if (waiting > 0 && now() - number (joinSnapshot, "received_at") < 30.0)
        {
            startStatus = "Downloading the song (" + std::to_string (waiting) + " audio files to go)...";
            return;
        }

        juce::String error;
        auto set = als::load (contentStore.fileFor (alsHash), error);
        if (set == nullptr)
        {
            notice ("The song file didn't arrive intact: " + error.toStdString());
            joinSnapshot = nullptr;
            return;
        }
        const auto song = str (joinSnapshot, "song", "Song");
        auto folder = workspaceFor (song);
        folder.createDirectory();
        auto target = folder.getChildFile (juce::String (str (joinSnapshot, "file", song + ".als")));
        for (auto& ref : als::fileRefs (*set))
        {
            juce::File original (ref.path.replaceCharacter ('\\', '/'));
            const auto hash = original.getParentDirectory().getFileName();
            if (! isHash (hash))
                continue;
            const auto local = contentStore.bestFor (hash.toStdString());
            if (local != juce::File())
                als::rewrite (ref, local, folder);
        }
        if (! als::save (*set, target))
        {
            notice ("Couldn't write the song to " + target.getFullPathName().toStdString());
            return;
        }
        currentSong = target;
        currentSongName = song;
        currentMap = field (joinSnapshot, "map");
        currentNotes = field (joinSnapshot, "notes");
        currentSnapshot = latestSnapshot = str (joinSnapshot, "id");
        songStamp = ignoreStamp = currentSong.getSize() ^ (currentSong.getLastModificationTime().toMilliseconds() << 8);
        joinSnapshot = nullptr;
        joinedWaitingForSong = true;
        startStatus.clear();
        notice ("Opening " + song + " in Live...");
        options.openInLive (currentSong);
        bindLive();
    }

    void Engine::openSongInLive()
    {
        if (currentSong.existsAsFile())
            options.openInLive (currentSong);
    }

    void Engine::leaveSession()
    {
        sessionPtr->leave (now());
    }

    void Engine::approveJoin (const std::string& peer, bool allow) { sessionPtr->approveJoin (peer, allow, now()); }
    void Engine::answerPlayer1Request (const std::string& peer, bool allow) { sessionPtr->answerPlayer1Request (peer, allow, now()); }
    void Engine::requestPlayer1() { sessionPtr->requestPlayer1(); }
    void Engine::handOver (const std::string& peer) { sessionPtr->handOver (peer, now()); }
    void Engine::takeOver() { sessionPtr->takeOver (now()); }
}
