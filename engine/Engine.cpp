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
        discover(); // starts the servers too
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
        // Tutti normally runs every 20 ms. A long gap means the computer slept or the system
        // paused Tutti (macOS App Nap), which friends see as Tutti dropping out.
        if (lastPumpAt > 0 && t - lastPumpAt > 2.5)
            log ("Tutti didn't run for " + juce::String (t - lastPumpAt, 1).toStdString()
                 + " s (the computer slept, or the system paused Tutti)");
        lastPumpAt = t;
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
        if (liveLink != 0 && ! liveBound && (sessionPtr->inSession() || (waitingForCapture && captureSentAt == 0))
            && t - lastBindAttempt > 3.0)
            bindLive();
        // The session's idea of who we're connected to must match the links we really have.
        if (t - lastLinkCheck > 1.0)
        {
            lastLinkCheck = t;
            // Who comes and goes, so a friend dropping out shows up in the logs.
            if (sessionPtr->inSession())
            {
                for (const auto& m : sessionPtr->info().members)
                {
                    if (m.peer == self.peer)
                        continue;
                    auto known = lastOnline.find (m.peer);
                    if (known == lastOnline.end() || known->second != m.online)
                    {
                        if (known != lastOnline.end() || ! m.online)
                            log (m.name + (m.online ? " is back online" : " went offline")
                                 + (peerLinks.count (m.peer) != 0 ? " (link still open)" : " (no link)"));
                        lastOnline[m.peer] = m.online;
                    }
                }
            }
            else
                lastOnline.clear();
            for (const auto& [peer, lid] : peerLinks)
                if (! sessionPtr->hasLink (peer))
                {
                    log ("the session had lost track of the link to " + peer + "; fixed");
                    sessionPtr->connected (peer, t);
                }
        }
        // Joining: the request for the song can be lost while both computers connect to each other
        // and one of the two connections is dropped. Ask again until the song arrives.
        if (sessionPtr->inSession() && ! sessionPtr->isPlayer1() && currentSong == juce::File()
            && joinSnapshot.is_null() && snapshotRequestedAt > 0 && t - snapshotRequestedAt > 5.0)
        {
            log ("asking Player 1 for the song again");
            requestSnapshot();
        }
        // Joining: the song opens once its file and the audio previews are here, or after 30 s
        // with whatever has arrived. Check every second, not only when a file arrives, or a
        // preview that never comes leaves the song stuck at "Downloading".
        if (! joinSnapshot.is_null() && t - lastJoinCheck > 1.0)
        {
            lastJoinCheck = t;
            maybeOpenJoinedSong();
        }
        if (waitingForCapture && captureSentAt > 0 && t - captureSentAt > 90.0)
            cancelStart ("Live didn't finish reading the song. Restart Live and try again.");

        if ((busy || t - lastTick > 0.5) && onChange)
        {
            lastTick = t;
            onChange();
        }
    }

    void Engine::closeLink (uint64_t linkId, const std::string& why)
    {
        if (auto it = links.find (linkId); it != links.end() && ! it->second.link->closed())
        {
            if (! it->second.local)
                log ("closing link " + std::to_string (linkId) + ": " + why);
            it->second.link->closeBecause (why);
        }
    }

    void Engine::restartTransfersFrom (const std::string& peer)
    {
        for (auto& [key, t] : transfers)
            if (t.active && t.from == peer)
            {
                t.active = false;
                t.retryAt = 0;
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
        // Live's Remote Script and the Stand-ins connect here. Retried with every discovery until it
        // works: a just-updated Tutti can start while the old one is still letting go of the port.
        if (! localServer.running()
            && ! localServer.start (options.localPort, "127.0.0.1", [this] (std::unique_ptr<juce::StreamingSocket> s) {
                   auto shared = std::make_shared<std::unique_ptr<juce::StreamingSocket>> (std::move (s));
                   post ([this, shared] { adoptSocket (std::move (*shared), false, true, "local"); });
               }))
            log ("couldn't listen on 127.0.0.1:" + std::to_string (options.localPort) + " yet");

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
        startServers();
        for (const auto& extra : options.extraPeers)
        {
            const auto [host, port] = splitAddr (extra.toStdString(), options.peerPort);
            connectPeer (host, port);
        }
        if (! options.useTailscale || discovering)
            return;
        discovering = true;
        juce::Thread::launch ([this] {
            std::string problem;
            auto status = tailscale::query (&problem);
            const auto local = tailscale::localAddress();
            const bool installed = tailscale::findCli() != juce::File() || ! local.empty();
            post ([this, status, installed, problem, local] {
                discovering = false;
                tailscaleInstalled = installed;
                auto effective = status;
                if (! effective && ! local.empty())
                {
                    // The tool couldn't be asked, but Tailscale is clearly up: use our address
                    // from the network settings and reach the friends we've met before.
                    tailscale::Status s;
                    s.backendState = "Running";
                    s.selfIp4 = local;
                    effective = s;
                }
                if (problem != lastTailscaleProblem)
                {
                    if (! problem.empty())
                        log ("Tailscale check: " + problem
                             + (local.empty() ? std::string() : " (using " + local + " from the network settings)"));
                    lastTailscaleProblem = problem;
                }
                tailscaleStatus = effective;
                if (! effective || ! effective->running())
                    return;
                startServers();
                bool learned = false;
                // What Tailscale itself says about each computer: if a friend drops out here too,
                // it's their network (or their computer sleeping), not Tutti.
                for (const auto& p : effective->peers)
                {
                    auto& was = tailscaleOnline[p.ip4];
                    if (was != (p.online ? 1 : 2))
                    {
                        if (was != 0 || ! p.online)
                            log ("Tailscale: " + p.hostName + " (" + p.ip4 + ") is " + (p.online ? "online" : "offline"));
                        was = p.online ? 1 : 2;
                    }
                }
                for (const auto& p : effective->peers)
                {
                    learned |= knownPeers.insert (p.ip4).second;
                    if (p.online)
                        connectPeer (p.ip4, options.peerPort);
                }
                if (effective->peers.empty())
                    for (const auto& ip : knownPeers)
                        connectPeer (ip, options.peerPort);
                if (learned)
                    saveSettings();
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
        state.openedAt = now();
        if (outgoing && ! local)
            state.tag = juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()).toStdString();
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
        auto hello = helloMessage();
        if (state.link->outgoing() && ! state.tag.empty())
            hello["link"] = state.tag; // so both ends rank this link the same way
        state.link->send (protocol::encode (hello));
    }

    void Engine::broadcastStatus()
    {
        for (auto& [id, l] : links)
            if (! l.local)
                sendHello (l);
    }

    // Two links to the same friend: both ends must keep the same one, whatever order their hellos
    // arrived in. Prefer the link dialled by the smaller peer id, then the smaller dialler's tag.
    bool Engine::keepNewLink (const LinkState& existing, const LinkState& added, const std::string& peer) const
    {
        const auto& smaller = std::min (self.peer, peer);
        auto rank = [&] (const LinkState& l) {
            const auto& dialler = l.link->outgoing() ? self.peer : peer;
            return std::make_pair (dialler == smaller ? 0 : 1, l.tag);
        };
        return rank (added) < rank (existing);
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
        // A link we've closed can still deliver what it had already read. Acting on that (say, a
        // hello on a replaced duplicate) would undo the decision to close it.
        if (state.link->closed())
            return;

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
            closeLink (linkId, "different protocol");
            return;
        }
        if (peer.empty() || peer == self.peer)
        {
            closeLink (linkId, "no peer id, or ourselves");
            return;
        }

        if (! state.link->outgoing() && state.tag.empty())
            state.tag = str (hello, "link"); // the dialler's tag for this link

        // Remember where friends are, for when Tailscale can't list them.
        if (const auto host = juce::String (str (hello, "addr")).upToFirstOccurrenceOf (":", false, false).toStdString();
            tailscale::isTailscaleAddress (host) && knownPeers.insert (host).second)
            saveSettings();

        if (state.peer == peer)
        {
            peerHellos[peer] = hello; // a status update on an existing link
            if (onChange)
                onChange();
            return;
        }

        if (auto existing = peerLinks.find (peer); existing != peerLinks.end() && existing->second != linkId)
        {
            // Both sides dialled each other (or one side twice): both ends keep the same link.
            auto& current = links[existing->second];
            const bool keepNew = keepNewLink (current, state, peer);
            if (! keepNew)
            {
                log ("two links to " + str (hello, "name") + ": keeping " + std::to_string (existing->second)
                     + ", closing " + std::to_string (linkId));
                closeLink (linkId, "duplicate");
                return;
            }
            auto old = existing->second;
            log ("two links to " + str (hello, "name") + ": keeping " + std::to_string (linkId)
                 + ", closing " + std::to_string (old));
            peerLinks.erase (existing);
            if (auto o = links.find (old); o != links.end())
                o->second.peer.clear();
            closeLink (old, "replaced by a duplicate");
            restartTransfersFrom (peer); // whatever was on the old link is lost
        }

        state.peer = peer;
        const auto addr = str (hello, "addr");
        if (! addr.empty())
            state.addr = addr;
        log ("link " + std::to_string (linkId) + (state.link->outgoing() ? " to " : " from ") + str (hello, "name")
             + " (" + state.addr + ")");
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
        if (! state.local)
            log ("link " + std::to_string (linkId) + (state.peer.empty() ? std::string() : " with " + memberName (state.peer))
                 + " (" + state.addr + ") closed after " + juce::String (now() - state.openedAt, 1).toStdString() + " s: "
                 + state.link->closeReason());

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
                log ("link " + std::to_string (linkId) + " closed: no link to that peer now");
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
        snapshotRequestedAt = 0;
        stale.clear();
        presence.clear();
        pendingSave = nullptr;
        saveQueue.clear();
        latestSnapshot.clear();
        currentSnapshot.clear();
        reloadWhenMerged = false;
        reloading = false;
        updateBy.clear();
        mySaveNeeded.clear();
        othersSaveNeeded.clear();
        openedAt = 0;
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
        else if (type == "needs_save")
        {
            std::vector<std::string> what;
            if (const auto& list = field (message, "what"); list.is_array())
                for (const auto& w : list)
                    if (w.is_string())
                        what.push_back (w.get<std::string>());
            if (what.empty())
                othersSaveNeeded.erase (peer);
            else
            {
                const auto name = str (message, "byName", "Someone");
                notice (name + " added " + what.back() + ". You'll get it when " + name + " saves in Live.");
                othersSaveNeeded[peer] = { name, what };
            }
        }
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

    namespace
    {
        // The Remote Script this app ships (remote_script/Tutti/engine.py SCRIPT_VERSION). Live loads
        // scripts only when it starts, so after an update it may still run the old one.
        constexpr const char* kRemoteScriptVersion = "3";
    }

    bool Engine::liveScriptOutdated() const
    {
        return liveLink != 0 && liveHello.is_object() && str (liveHello, "script") != kRemoteScriptVersion;
    }

    void Engine::cancelStart (const std::string& why)
    {
        waitingForCapture = false;
        captureSentAt = 0;
        startStatus.clear();
        pendingStartSource = pendingStartCopy = juce::File();
        liveStatus = "Ready";
        notice (why);
    }

    void Engine::sendToLive (const Json& message)
    {
        if (liveLink != 0)
            sendTo (liveLink, message);
    }

    void Engine::openInLiveAndWait (const juce::File& file)
    {
        docAtOpen = liveLink != 0 ? str (liveHello, "doc") : std::string();
        openedAt = now();
        strictMatchLogged = false;
        log ("opening " + file.getFullPathName().toStdString() + " in Live");
        options.openInLive (file);
    }

    bool Engine::songOpenInLive()
    {
        if (openedAt > 0)
        {
            const auto doc = str (liveHello, "doc");
            if (liveLink != 0 && ! doc.empty() && doc != docAtOpen)
            {
                openedAt = 0; // Live loaded a Set since we asked: check it's ours below
            }
            else if (now() - openedAt < 20.0)
                return false;
            else if (! strictMatchLogged)
            {
                strictMatchLogged = true;
                log ("Live hasn't opened another Set since Tutti asked it to; checking the open one closely");
            }
        }
        const bool strict = openedAt > 0;
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
        const auto score = als::matchScore (entry.second, names);
        if (! strict)
            return score >= 0.8;
        int fileTracks = 0;
        for (const auto& t : entry.second)
            fileTracks += (t.kind != "return" && t.kind != "master") ? 1 : 0;
        return score >= 0.999 && integer (fp, "n") == fileTracks;
    }

    void Engine::bindLive()
    {
        lastBindAttempt = now();
        if (liveLink == 0)
            return;
        if (liveScriptOutdated())
        {
            liveStatus = "Restart Live to finish updating Tutti";
            if (waitingForCapture)
                cancelStart ("Restart Live, then share the song again: Tutti was updated and Live is still running the old version.");
            return;
        }
        auto& s = *sessionPtr;
        if (! s.inSession())
        {
            if (waitingForCapture && songOpenInLive())
            {
                liveStatus = "Reading your song...";
                startStatus = "Reading your song in Live...";
                captureSentAt = now();
                sendToLive (Json { { "type", "capture" } });
            }
            else
                liveStatus = ! waitingForCapture ? "Ready"
                           : (openedAt > 0 && now() - openedAt >= 20.0 ? "Live didn't open the song: check Live for a question, or open "
                                                                              + pendingStartCopy.getFileName().toStdString() + " from Music/Tutti"
                                                                        : "Waiting for Live to open the song...");
            return;
        }
        if (currentSong == juce::File())
        {
            liveStatus = "Downloading the song...";
            return;
        }
        if (! songOpenInLive())
        {
            liveStatus = openedAt > 0 && now() - openedAt < 20.0 ? "Waiting for Live to open the song..."
                                                                   : "Open the session song in Live";
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
        else if (type == "capture_failed")
        {
            log ("Live couldn't read the song: " + str (m, "error"));
            if (waitingForCapture)
                cancelStart ("Live couldn't read the song, so the session didn't start. Details are in Tutti's log (Settings > Show logs).");
        }
        else if (type == "bind_failed")
        {
            log ("Live couldn't load the session song: " + str (m, "error"));
            liveBound = false;
            reloading = false;
            liveStatus = "Couldn't connect your Live to the session";
            if (bindFailures++ == 0)
                notice ("Live couldn't load the session song. Details are in Tutti's log (Settings > Show logs).");
            lastBindAttempt = now() + 57.0; // try again in a minute, not every few seconds
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
            bindFailures = 0;
            liveStatus = "In sync";
            stale.clear();
            const auto& stats = field (m, "stats");
            log ("Live bound to " + currentSong.getFullPathName().toStdString() + ": " + stats.dump());
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

    void Engine::noteSaveNeeded (const Ops& ops)
    {
        // Adding a plugin can't be copied into other people's Live while they play: it travels
        // with a save. Say so, here and on everyone else's Tutti, instead of leaving it a mystery.
        for (const auto& op : ops)
        {
            if (op.kind != Op::Kind::create || op.type != "device" || ! op.props.contains ("plugin") || op.props["plugin"].is_null())
                continue;
            const auto what = str (op.props, "name", "a plugin");
            if (std::find (mySaveNeeded.begin(), mySaveNeeded.end(), what) != mySaveNeeded.end())
                continue;
            mySaveNeeded.push_back (what);
            log ("added " + what + " (plugin): shared on the next save");
            sessionPtr->broadcast (Json { { "type", "needs_save" }, { "what", mySaveNeeded }, { "byName", self.name } });
           #if JUCE_MAC
            notice ("You added " + what + ". Press Cmd+S in Live to share it with everyone.");
           #else
            notice ("You added " + what + ". Press Ctrl+S in Live to share it with everyone.");
           #endif
        }
    }

    void Engine::submitFromLive (Ops ops)
    {
        if (sessionPtr->inSession())
            noteSaveNeeded (ops);
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
        if (liveScriptOutdated())
        {
            error = "Restart Live first: Tutti was updated and Live is still running the old version.";
            return false;
        }
        auto set = als::load (source, error);
        if (set == nullptr)
            return false;
        log ("sharing " + source.getFullPathName().toStdString() + " (saved "
             + source.getLastModificationTime().toString (true, true).toStdString() + ", "
             + std::to_string (als::tracks (*set).size()) + " tracks incl. returns and Main)");

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
            juce::StringArray missingNames;
            for (auto& ref : als::fileRefs (*shared))
            {
                auto file = als::resolve (ref, source.getParentDirectory());
                if (file == juce::File())
                {
                    ++missing;
                    missingNames.add (ref.path.isNotEmpty() ? ref.path : ref.relative);
                    continue;
                }
                auto hash = contentStore.add (file, err);
                if (! hash)
                {
                    ++missing;
                    missingNames.add (file.getFullPathName() + " (" + err + ")");
                    continue;
                }
                audio::describe (contentStore, *hash);
                als::rewrite (ref, contentStore.fileFor (*hash), folder);
            }
            folder.createDirectory();
            const bool saved = copy == source || als::save (*shared, copy);
            post ([this, saved, missing, missingNames, copy] {
                if (missing > 0)
                    log ("files the song refers to that couldn't be found: " + missingNames.joinIntoString ("; ").toStdString());
                if (! saved)
                {
                    waitingForCapture = false;
                    startStatus.clear();
                    notice ("Couldn't write the session copy of the song.");
                    return;
                }
                if (missing > 0)
                    notice (std::to_string (missing) + " sample(s) in the song couldn't be found; they'll be missing for everyone.");
                startStatus = "Opening the song in Live...";
                openInLiveAndWait (copy);
                bindLive();
            });
        });
        return true;
    }

    void Engine::finishStart (const Json& captured)
    {
        log ("Live read the song: " + std::to_string (field (field (liveHello, "fingerprint"), "tracks").size())
             + " tracks open in Live");
        waitingForCapture = false;
        captureSentAt = 0;
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
        if (liveScriptOutdated())
        {
            notice ("Restart Live first: Tutti was updated and Live is still running the old version.");
            return;
        }
        MemberInfo host;
        host.peer = hostPeer;
        host.name = str (it->second, "name");
        host.addr = str (it->second, "addr");
        sessionPtr->join (host, now());
        notice ("Asking " + host.name + " to let you in...");
    }

    void Engine::requestSnapshot()
    {
        snapshotRequestedAt = now();
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
        {
            startStatus = "Downloading the song file...";
            return;
        }
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
            if (startStatus.find (std::to_string (waiting) + " audio") == std::string::npos)
                log ("song file is here; waiting for " + std::to_string (waiting) + " audio preview(s)");
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
        openInLiveAndWait (currentSong);
        bindLive();
    }

    void Engine::openSongInLive()
    {
        if (currentSong.existsAsFile())
            openInLiveAndWait (currentSong);
    }

    void Engine::revealSong()
    {
        if (currentSong.existsAsFile())
            currentSong.revealToUser();
    }

    void Engine::leaveSession()
    {
        sessionPtr->leave (now());
    }

    void Engine::approveJoin (const std::string& peer, bool allow)
    {
        log ((allow ? "letting in " : "not letting in ") + memberName (peer));
        sessionPtr->approveJoin (peer, allow, now());
    }
    void Engine::answerPlayer1Request (const std::string& peer, bool allow) { sessionPtr->answerPlayer1Request (peer, allow, now()); }
    void Engine::requestPlayer1() { sessionPtr->requestPlayer1(); }
    void Engine::handOver (const std::string& peer) { sessionPtr->handOver (peer, now()); }
    void Engine::takeOver() { sessionPtr->takeOver (now()); }
}
