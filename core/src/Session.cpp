#include "tutti/Session.h"

#include <algorithm>

namespace tutti
{
    // ------------------------------------------------------------------ info

    Json MemberInfo::toJson() const
    {
        return Json { { "peer", peer }, { "name", name }, { "addr", addr }, { "app", app },
                      { "live", live }, { "os", os }, { "color", color }, { "online", online } };
    }

    MemberInfo MemberInfo::fromJson (const Json& j)
    {
        MemberInfo m;
        m.peer = str (j, "peer");
        m.name = str (j, "name");
        m.addr = str (j, "addr");
        m.app = str (j, "app");
        m.live = str (j, "live");
        m.os = str (j, "os");
        m.color = static_cast<int> (integer (j, "color"));
        m.online = boolean (j, "online", true);
        return m;
    }

    Json SessionInfo::toJson() const
    {
        Json list = Json::array();
        for (const auto& m : members)
            list.push_back (m.toJson());
        return Json { { "id", id }, { "song", song }, { "p1", p1 }, { "snapshot", snapshot },
                      { "epoch", epoch }, { "base", base }, { "members", std::move (list) } };
    }

    SessionInfo SessionInfo::fromJson (const Json& j)
    {
        SessionInfo s;
        s.id = str (j, "id");
        s.song = str (j, "song");
        s.p1 = str (j, "p1");
        s.snapshot = str (j, "snapshot");
        s.epoch = uinteger (j, "epoch");
        s.base = uinteger (j, "base");
        if (const auto& list = field (j, "members"); list.is_array())
            for (const auto& m : list)
                s.members.push_back (MemberInfo::fromJson (m));
        return s;
    }

    const MemberInfo* SessionInfo::member (const std::string& peer) const
    {
        for (const auto& m : members)
            if (m.peer == peer)
                return &m;
        return nullptr;
    }

    const char* toString (Session::Role role)
    {
        switch (role)
        {
            case Session::Role::idle:    return "idle";
            case Session::Role::joining: return "joining";
            case Session::Role::guest:   return "guest";
            case Session::Role::player1: return "player1";
        }
        return "?";
    }

    // ------------------------------------------------------------------ basics

    Session::Session (MemberInfo self, Listener& l, Timing t, uint64_t firstCseq)
        : me (std::move (self)), listener (l), timing (t), rep (me.peer, firstCseq)
    {
    }

    void Session::updateSelf (const MemberInfo& updated)
    {
        me.name = updated.name;
        me.color = updated.color;
        me.addr = updated.addr;
        me.app = updated.app;
        me.live = updated.live;
        me.os = updated.os;
        for (auto& m : sessionInfo.members)
            if (m.peer == me.peer)
            {
                m = me;
                m.online = true;
            }
        if (isPlayer1())
            broadcastInfo();
    }

    void Session::sendTo (const std::string& peer, const Json& message)
    {
        if (peer.empty() || peer == me.peer)
            return;
        listener.send (peer, message);
    }

    void Session::sendToMembers (const Json& message, const std::string& except)
    {
        for (const auto& m : sessionInfo.members)
            if (m.peer != me.peer && m.peer != except && links.count (m.peer) != 0)
                listener.send (m.peer, message);
    }

    void Session::broadcast (const Json& message) { sendToMembers (message); }

    void Session::broadcastInfo()
    {
        sendToMembers (Json { { "type", "info" }, { "epoch", sessionInfo.epoch }, { "info", sessionInfo.toJson() } });
        listener.sessionChanged();
    }

    bool Session::isOnline (const std::string& peer, double now) const
    {
        if (peer == me.peer)
            return true;
        auto it = lastHeard.find (peer);
        return links.count (peer) != 0 && it != lastHeard.end() && now - it->second < timing.lostAfter;
    }

    void Session::deliver (const LiveUpdate& u)
    {
        if (u.needResync)
            requestResync (false);
        if (! u.apply.empty() || ! u.reconcile.empty())
            listener.liveUpdate (u);
    }

    void Session::resetToIdle (const std::string& reason)
    {
        currentRole = Role::idle;
        sequencer.reset();
        sessionInfo = {};
        electing = false;
        handingOff = false;
        awaitingResync = false;
        askedForP1 = false;
        joinRequests.clear();
        p1Requests.clear();
        waitingToJoin.clear();
        listener.left (reason);
        listener.sessionChanged();
    }

    // ------------------------------------------------------------------ lifecycle

    void Session::start (std::string sessionId, std::string song, Doc doc, uint64_t seq, std::string snapshot, double now)
    {
        sessionInfo = {};
        sessionInfo.id = std::move (sessionId);
        sessionInfo.song = std::move (song);
        sessionInfo.snapshot = std::move (snapshot);
        sessionInfo.epoch = 1;
        sessionInfo.p1 = me.peer;
        auto self = me;
        self.online = true;
        sessionInfo.members = { self };
        approved = { me.peer };
        rep.reset (std::move (doc), seq);
        becomeHost (now);
        listener.sessionChanged();
    }

    void Session::join (const MemberInfo& host, double now)
    {
        currentRole = Role::joining;
        sessionInfo = {};
        sessionInfo.p1 = host.peer;
        lastHeard[host.peer] = now;
        listener.connectTo (host);
        if (links.count (host.peer) != 0)
            sendTo (host.peer, Json { { "type", "join" }, { "member", me.toJson() } });
        listener.sessionChanged();
    }

    void Session::leave (double now)
    {
        if (currentRole == Role::idle)
            return;

        if (isPlayer1())
        {
            // Hand the session to the next person in line who's here, so it carries on.
            for (const auto& m : sessionInfo.members)
                if (m.peer != me.peer && isOnline (m.peer, now))
                {
                    sendTo (m.peer, Json { { "type", "handoff" }, { "epoch", sessionInfo.epoch + 1 },
                                           { "seq", rep.seq() }, { "leaving", true } });
                    break;
                }
        }
        sendToMembers (Json { { "type", "leave" } });
        resetToIdle ("You left the session.");
    }

    // ------------------------------------------------------------------ Player 1

    void Session::becomeHost (double now)
    {
        currentRole = Role::player1;
        sessionInfo.base = rep.seq();
        hostDoc = rep.confirmed();
        sequencer = std::make_unique<Sequencer> (hostDoc, rep.seq(), timing.priorityWindow);
        sequencer->setPlayer1 (me.peer);
        lastCseq.clear();
        for (const auto& e : rep.log())
            lastCseq[e.author] = std::max (lastCseq[e.author], e.cseq);
        for (const auto& m : sessionInfo.members)
            approved.insert (m.peer);
        handingOff = false;
        askedForP1 = false;
        clockSamples.clear();
        clockOffset = 0;

        const auto pending = std::vector<Replica::Batch> (rep.pending().begin(), rep.pending().end());
        for (const auto& b : pending)
            sequenceBatch (me.peer, b.cseq, b.ops, now);
    }

    void Session::sequenceBatch (const std::string& author, uint64_t cseq, const Ops& ops, double now)
    {
        auto& last = lastCseq[author];
        if (cseq <= last)
        {
            // Already sequenced (a batch sent again after a Player 1 change).
            if (author == me.peer)
                deliver (rep.ack (cseq, 0, {}, {}));
            else
                sendTo (author, Json { { "type", "ack" }, { "epoch", sessionInfo.epoch }, { "base", sessionInfo.base },
                                       { "cseq", cseq }, { "seq", 0 }, { "ops", Json::array() }, { "rec", Json::array() } });
            return;
        }
        last = cseq;

        const auto result = sequencer->submit (author, ops, now);
        const LogEntry entry { result.seq, author, cseq, result.accepted };

        if (author == me.peer)
        {
            deliver (rep.ack (cseq, result.seq, result.accepted, result.reconcile));
        }
        else
        {
            Json rec = Json::array();
            for (const auto& r : result.reconcile)
                rec.push_back (Json { { "id", r.id }, { "deep", r.deep } });
            sendTo (author, Json { { "type", "ack" }, { "epoch", sessionInfo.epoch }, { "base", sessionInfo.base },
                                   { "cseq", cseq }, { "seq", result.seq }, { "ops", toJson (result.accepted) },
                                   { "rec", std::move (rec) } });
            if (result.seq != 0)
                deliver (rep.remote (entry));
        }

        if (result.seq != 0)
        {
            Json message = entry.toJson();
            message["type"] = "ops";
            message["epoch"] = sessionInfo.epoch;
            message["base"] = sessionInfo.base;
            sendToMembers (message, author);
        }
    }

    void Session::admit (const MemberInfo& member, double now)
    {
        approved.insert (member.peer);
        waitingToJoin.erase (member.peer);
        joinRequests.erase (member.peer);
        lastHeard[member.peer] = now;

        bool found = false;
        for (auto& m : sessionInfo.members)
            if (m.peer == member.peer)
            {
                m = member;
                m.online = true;
                found = true;
            }
        if (! found)
        {
            auto m = member;
            m.online = true;
            sessionInfo.members.push_back (m);
        }

        sendTo (member.peer, Json { { "type", "welcome" }, { "epoch", sessionInfo.epoch }, { "info", sessionInfo.toJson() },
                                    { "doc", rep.confirmed().toJson() }, { "seq", rep.seq() } });
        broadcastInfo();
    }

    void Session::approveJoin (const std::string& peer, bool allow, double now)
    {
        auto it = waitingToJoin.find (peer);
        if (! isPlayer1() || it == waitingToJoin.end())
            return;
        const auto member = it->second;
        if (allow)
            admit (member, now);
        else
        {
            waitingToJoin.erase (it);
            joinRequests.erase (peer);
            sendTo (peer, Json { { "type", "denied" }, { "reason", "Player 1 didn't let you in." } });
            listener.sessionChanged();
        }
    }

    void Session::answerPlayer1Request (const std::string& peer, bool allow, double now)
    {
        p1Requests.erase (peer);
        if (! isPlayer1())
            return;
        if (allow && handOver (peer, now))
            return;
        sendTo (peer, Json { { "type", "p1_denied" } });
        listener.sessionChanged();
    }

    bool Session::handOver (const std::string& peer, double now)
    {
        if (! isPlayer1() || handingOff || peer == me.peer || ! sessionInfo.isMember (peer) || ! isOnline (peer, now))
            return false;
        handingOff = true;
        handoffTo = peer;
        handoffDeadline = now + timing.handoffTimeout;
        sendTo (peer, Json { { "type", "handoff" }, { "epoch", sessionInfo.epoch + 1 }, { "seq", rep.seq() } });
        listener.sessionChanged();
        return true;
    }

    void Session::setSuccession (const std::vector<std::string>& order)
    {
        if (! isPlayer1())
            return;
        std::vector<MemberInfo> reordered;
        for (const auto& peer : order)
            if (auto* m = sessionInfo.member (peer))
                reordered.push_back (*m);
        for (const auto& m : sessionInfo.members)
            if (std::find (order.begin(), order.end(), m.peer) == order.end())
                reordered.push_back (m);
        sessionInfo.members = std::move (reordered);
        broadcastInfo();
    }

    void Session::setSnapshot (const std::string& snapshotId)
    {
        if (! isPlayer1())
            return;
        sessionInfo.snapshot = snapshotId;
        broadcastInfo();
    }

    void Session::setSong (const std::string& song)
    {
        if (! isPlayer1())
            return;
        sessionInfo.song = song;
        broadcastInfo();
    }

    void Session::removeMember (const std::string& peer)
    {
        auto& list = sessionInfo.members;
        list.erase (std::remove_if (list.begin(), list.end(), [&] (const MemberInfo& m) { return m.peer == peer; }), list.end());
        p1Requests.erase (peer);
    }

    // ------------------------------------------------------------------ guests

    void Session::requestPlayer1()
    {
        if (currentRole != Role::guest)
            return;
        askedForP1 = true;
        sendTo (sessionInfo.p1, Json { { "type", "req_p1" } });
        listener.sessionChanged();
    }

    bool Session::canTakeOver (double now) const
    {
        if (currentRole != Role::guest)
            return false;
        auto it = lastHeard.find (sessionInfo.p1);
        const bool p1Gone = links.count (sessionInfo.p1) == 0 || it == lastHeard.end() || now - it->second > timing.lostAfter;
        return p1Gone;
    }

    void Session::takeOver (double now)
    {
        if (! canTakeOver (now))
            return;
        finishTakeover (std::max (sessionInfo.epoch, votedEpoch) + 1, now);
    }

    // ------------------------------------------------------------------ edits

    void Session::submitLocal (Ops ops, double now)
    {
        if (ops.empty())
            return;
        const auto batch = rep.local (std::move (ops));
        if (isPlayer1() && ! handingOff)
            sequenceBatch (me.peer, batch.cseq, batch.ops, now);
        else if (currentRole == Role::guest && links.count (sessionInfo.p1) != 0 && ! deferResubmit)
            sendTo (sessionInfo.p1, Json { { "type", "submit" }, { "cseq", batch.cseq }, { "ops", toJson (batch.ops) } });
        // Otherwise it stays pending and goes out with resubmitPending().
    }

    void Session::resubmitPending (double now)
    {
        deferResubmit = false;
        const auto pending = std::vector<Replica::Batch> (rep.pending().begin(), rep.pending().end());
        if (isPlayer1())
        {
            for (const auto& b : pending)
                sequenceBatch (me.peer, b.cseq, b.ops, now);
        }
        else if (currentRole == Role::guest && links.count (sessionInfo.p1) != 0)
        {
            for (const auto& b : pending)
                sendTo (sessionInfo.p1, Json { { "type", "submit" }, { "cseq", b.cseq }, { "ops", toJson (b.ops) } });
        }
    }

    void Session::requestResync (bool full)
    {
        if (awaitingResync || currentRole != Role::guest)
            return;
        awaitingResync = true;
        resyncSentAt = clockNow;
        sendTo (sessionInfo.p1, Json { { "type", "resync" }, { "seq", rep.seq() }, { "full", full } });
    }

    // ------------------------------------------------------------------ transport

    void Session::connected (const std::string& peer, double now)
    {
        clockNow = now;
        links.insert (peer);
        lastHeard[peer] = now;

        if (currentRole == Role::joining && peer == sessionInfo.p1)
            sendTo (peer, Json { { "type", "join" }, { "member", me.toJson() } });
        else if (currentRole == Role::guest && peer == sessionInfo.p1)
        {
            // Back in touch with Player 1: make sure we didn't miss anything, then resend our edits.
            awaitingResync = false;
            requestResync (false);
            deferResubmit = true;
        }
        else if (isPlayer1() && sessionInfo.isMember (peer))
        {
            for (auto& m : sessionInfo.members)
                if (m.peer == peer)
                    m.online = true;
            broadcastInfo();
        }
    }

    void Session::disconnected (const std::string& peer, double)
    {
        links.erase (peer);
        if (isPlayer1() && sessionInfo.isMember (peer))
        {
            for (auto& m : sessionInfo.members)
                if (m.peer == peer)
                    m.online = false;
            broadcastInfo();
        }
        listener.sessionChanged();
    }

    bool Session::fromCurrentPlayer1 (const std::string& peer, const Json& message, double now)
    {
        const auto epoch = uinteger (message, "epoch");
        if (epoch < sessionInfo.epoch)
            return false; // an old Player 1
        if (epoch > sessionInfo.epoch)
        {
            // A Player 1 we haven't heard announced (we missed the message): follow it.
            adoptNewPlayer1 (peer, epoch, uinteger (message, "base"), now);
            return peer == sessionInfo.p1;
        }
        return peer == sessionInfo.p1;
    }

    void Session::receive (const std::string& peer, const Json& message, double now)
    {
        clockNow = now;
        lastHeard[peer] = now;
        const auto type = str (message, "type");

        if (type == "ping")
        {
            sendTo (peer, Json { { "type", "pong" }, { "t", number (message, "t") }, { "t1", now } });
            const auto epoch = uinteger (message, "epoch");
            const auto theirP1 = str (message, "p1");
            if (inSession() && epoch > sessionInfo.epoch && ! theirP1.empty() && sessionInfo.isMember (peer))
                adoptNewPlayer1 (theirP1, epoch, uinteger (message, "base"), now);
            return;
        }

        if (type == "pong")
        {
            if (peer == sessionInfo.p1 && ! isPlayer1())
            {
                const double t0 = number (message, "t");
                const double rtt = now - t0;
                if (rtt >= 0 && rtt < 5.0)
                {
                    clockSamples.emplace_back (rtt, number (message, "t1") - (t0 + now) / 2.0);
                    while (clockSamples.size() > 16)
                        clockSamples.pop_front();
                    auto best = std::min_element (clockSamples.begin(), clockSamples.end());
                    clockOffset = best->second;
                }
            }
            return;
        }

        if (type == "join")
        {
            if (! isPlayer1())
            {
                sendTo (peer, Json { { "type", "not_p1" }, { "p1", sessionInfo.p1 },
                                     { "p1_member", sessionInfo.member (sessionInfo.p1) != nullptr
                                                        ? sessionInfo.member (sessionInfo.p1)->toJson() : Json() } });
                return;
            }
            auto member = MemberInfo::fromJson (field (message, "member"));
            member.peer = peer;
            if (approved.count (peer) != 0)
                admit (member, now);
            else
            {
                waitingToJoin[peer] = member;
                joinRequests.insert (peer);
                listener.joinRequested (member);
                listener.sessionChanged();
            }
            return;
        }

        if (type == "not_p1")
        {
            if (currentRole == Role::joining)
            {
                auto host = MemberInfo::fromJson (field (message, "p1_member"));
                if (! host.peer.empty() && host.peer != me.peer)
                    join (host, now);
            }
            return;
        }

        if (type == "welcome")
        {
            if (currentRole != Role::joining)
                return;
            auto doc = Doc::fromJson (field (message, "doc"));
            if (! doc)
                return;
            sessionInfo = SessionInfo::fromJson (field (message, "info"));
            sessionInfo.p1 = peer;
            rep.reset (std::move (*doc), uinteger (message, "seq"));
            currentRole = Role::guest;
            awaitingResync = false;
            for (const auto& m : sessionInfo.members)
                if (m.peer != me.peer)
                    listener.connectTo (m);
            listener.joined();
            listener.sessionChanged();
            return;
        }

        if (type == "denied")
        {
            if (currentRole == Role::joining)
                resetToIdle (str (message, "reason", "Player 1 didn't let you in."));
            return;
        }

        if (type == "info")
        {
            if (! inSession() || ! fromCurrentPlayer1 (peer, message, now))
                return;
            auto updated = SessionInfo::fromJson (field (message, "info"));
            if (updated.id != sessionInfo.id)
                return;
            const bool stillMember = updated.isMember (me.peer);
            sessionInfo = updated;
            for (const auto& m : sessionInfo.members)
                if (m.peer != me.peer && links.count (m.peer) == 0)
                    listener.connectTo (m);
            if (! stillMember)
                resetToIdle ("You were removed from the session.");
            else
                listener.sessionChanged();
            return;
        }

        if (type == "submit")
        {
            if (! isPlayer1() || handingOff || ! sessionInfo.isMember (peer))
                return; // they'll send it again to whoever is Player 1 next
            auto ops = opsFromJson (field (message, "ops"));
            if (ops)
                sequenceBatch (peer, uinteger (message, "cseq"), *ops, now);
            return;
        }

        if (type == "ack")
        {
            if (currentRole != Role::guest || ! fromCurrentPlayer1 (peer, message, now))
                return;
            auto ops = opsFromJson (field (message, "ops"));
            std::vector<Reconcile> rec;
            if (const auto& list = field (message, "rec"); list.is_array())
                for (const auto& r : list)
                    rec.push_back ({ str (r, "id"), boolean (r, "deep") });
            if (awaitingResync)
            {
                // The catch-up will bring the accepted part; just settle the batch.
                deliver (rep.ack (uinteger (message, "cseq"), 0, {}, rec));
                return;
            }
            deliver (rep.ack (uinteger (message, "cseq"), uinteger (message, "seq"), ops ? *ops : Ops {}, rec));
            return;
        }

        if (type == "ops")
        {
            if (currentRole != Role::guest || ! fromCurrentPlayer1 (peer, message, now) || awaitingResync)
                return;
            auto entry = LogEntry::fromJson (message);
            if (entry)
                deliver (rep.remote (*entry));
            return;
        }

        if (type == "resync")
        {
            // Anyone can answer from their replica (Player 1, or a member a new Player 1 catches up from).
            const auto since = uinteger (message, "seq");
            const bool full = boolean (message, "full");
            auto entries = rep.logSince (since);
            const bool contiguous = entries.empty() ? since == rep.seq() : entries.front().seq == since + 1;
            if (! full && contiguous && since <= rep.seq())
            {
                Json list = Json::array();
                for (const auto& e : entries)
                    list.push_back (e.toJson());
                sendTo (peer, Json { { "type", "log" }, { "epoch", sessionInfo.epoch }, { "entries", std::move (list) },
                                     { "seq", rep.seq() } });
            }
            else
            {
                sendTo (peer, Json { { "type", "state" }, { "epoch", sessionInfo.epoch }, { "doc", rep.confirmed().toJson() },
                                     { "seq", rep.seq() } });
            }
            return;
        }

        if (type == "log")
        {
            handleLog (peer, message, now);
            return;
        }

        if (type == "state")
        {
            handleState (peer, message, now);
            return;
        }

        if (type == "req_p1")
        {
            if (isPlayer1() && sessionInfo.isMember (peer))
            {
                p1Requests.insert (peer);
                listener.player1Requested (*sessionInfo.member (peer));
                listener.sessionChanged();
            }
            return;
        }

        if (type == "p1_denied")
        {
            askedForP1 = false;
            listener.player1Denied();
            listener.sessionChanged();
            return;
        }

        if (type == "handoff")
        {
            if (currentRole != Role::guest || peer != sessionInfo.p1)
                return;
            const auto epoch = uinteger (message, "epoch");
            const auto seq = uinteger (message, "seq");
            if (boolean (message, "leaving"))
            {
                for (auto& m : sessionInfo.members)
                    if (m.peer == peer)
                        m.online = false;
            }
            if (rep.seq() < seq)
            {
                catchUpPeer = peer;
                catchUpEpoch = epoch;
                catchUpDeadline = now + timing.catchUpTimeout;
                sendTo (peer, Json { { "type", "resync" }, { "seq", rep.seq() }, { "full", false } });
            }
            else
                finishTakeover (epoch, now);
            if (boolean (message, "leaving"))
                removeMember (peer);
            return;
        }

        if (type == "p1")
        {
            const auto epoch = uinteger (message, "epoch");
            const auto newP1 = str (message, "p1", peer);
            if (! inSession() || epoch < sessionInfo.epoch)
                return;
            // Two takeovers in the same epoch: the smaller peer id wins, everywhere.
            if (epoch == sessionInfo.epoch && (newP1 == sessionInfo.p1 || newP1 > sessionInfo.p1))
                return;
            adoptNewPlayer1 (newP1, epoch, uinteger (message, "base"), now);
            return;
        }

        if (type == "elect")
        {
            const auto epoch = uinteger (message, "epoch");
            bool ok = false;
            if (inSession() && ! isPlayer1() && epoch > sessionInfo.epoch && votedEpoch < epoch && sessionInfo.isMember (peer))
            {
                auto it = lastHeard.find (sessionInfo.p1);
                const bool p1Silent = links.count (sessionInfo.p1) == 0 || it == lastHeard.end()
                                      || now - it->second > timing.voteGrace;
                if (p1Silent)
                {
                    ok = true;
                    votedEpoch = epoch;
                }
            }
            sendTo (peer, Json { { "type", "vote" }, { "epoch", epoch }, { "ok", ok }, { "seq", rep.seq() },
                                 { "p1", sessionInfo.p1 }, { "my_epoch", sessionInfo.epoch } });
            return;
        }

        if (type == "vote")
        {
            if (! electing || uinteger (message, "epoch") != electEpoch)
                return;
            if (boolean (message, "ok"))
                votes[peer] = uinteger (message, "seq");
            // Finish early once everyone reachable has answered.
            size_t reachable = 1;
            for (const auto& m : sessionInfo.members)
                if (m.peer != me.peer && m.peer != sessionInfo.p1 && links.count (m.peer) != 0)
                    ++reachable;
            if (votes.size() >= reachable)
                finishElection (now);
            return;
        }

        if (type == "leave")
        {
            if (isPlayer1())
            {
                removeMember (peer);
                approved.erase (peer);
                broadcastInfo();
            }
            else if (inSession() && peer == sessionInfo.p1)
            {
                // Player 1 left. It handed over first if anyone was here; otherwise the usual
                // timeout starts a takeover.
                for (auto& m : sessionInfo.members)
                    if (m.peer == peer)
                        m.online = false;
                listener.sessionChanged();
            }
            else if (inSession())
            {
                removeMember (peer);
                listener.sessionChanged();
            }
            return;
        }

        listener.appMessage (peer, message);
    }

    void Session::handleLog (const std::string& peer, const Json& message, double now)
    {
        const auto& list = field (message, "entries");
        bool gap = false;
        if (list.is_array())
            for (const auto& item : list)
            {
                auto entry = LogEntry::fromJson (item);
                if (! entry)
                    continue;
                auto u = rep.remote (*entry);
                if (u.needResync)
                {
                    gap = true;
                    break;
                }
                if (! u.apply.empty() || ! u.reconcile.empty())
                    listener.liveUpdate (u);
            }

        if (gap)
        {
            sendTo (peer, Json { { "type", "resync" }, { "seq", rep.seq() }, { "full", true } });
            return;
        }

        if (! catchUpPeer.empty() && peer == catchUpPeer)
        {
            caughtUp (now);
            return;
        }
        if (peer == sessionInfo.p1)
        {
            awaitingResync = false;
            resubmitPending (now);
        }
    }

    void Session::handleState (const std::string& peer, const Json& message, double now)
    {
        auto doc = Doc::fromJson (field (message, "doc"));
        if (! doc)
            return;
        const bool forCatchUp = ! catchUpPeer.empty() && peer == catchUpPeer;
        if (! forCatchUp && peer != sessionInfo.p1)
            return;

        rep.rebase (std::move (*doc), uinteger (message, "seq"));
        if (! extras.empty())
        {
            rep.local (extras);
            extras.clear();
        }
        listener.fullResync();

        if (forCatchUp)
        {
            caughtUp (now);
            return;
        }
        awaitingResync = false;
        resubmitPending (now);
    }

    void Session::caughtUp (double now)
    {
        const auto epoch = catchUpEpoch;
        catchUpPeer.clear();
        catchUpEpoch = 0;
        finishTakeover (epoch, now);
    }

    void Session::adoptNewPlayer1 (const std::string& p1, uint64_t epoch, uint64_t base, double now)
    {
        if (epoch <= sessionInfo.epoch && p1 == sessionInfo.p1)
            return;

        sessionInfo.epoch = epoch;
        sessionInfo.p1 = p1;
        electing = false;
        handingOff = false;
        catchUpPeer.clear();
        askedForP1 = false;
        clockSamples.clear();
        awaitingResync = false;

        if (p1 == me.peer)
        {
            becomeHost (now);
            listener.sessionChanged();
            return;
        }

        currentRole = Role::guest;
        sequencer.reset();
        p1Requests.clear();
        joinRequests.clear();
        waitingToJoin.clear();
        lastHeard[p1] = now;

        if (rep.seq() > base)
        {
            // We hold history the new Player 1 never saw (we were cut off from the others). Keep
            // our own edits from it, take their state, and send ours again.
            extras = rep.ownOpsSince (base);
            awaitingResync = true;
            resyncSentAt = now;
            sendTo (p1, Json { { "type", "resync" }, { "seq", rep.seq() }, { "full", true } });
        }
        else
        {
            requestResync (false);
        }
        deferResubmit = true;
        listener.sessionChanged();
    }

    // ------------------------------------------------------------------ elections

    std::string Session::expectedCandidate (double now) const
    {
        for (const auto& m : sessionInfo.members)
        {
            if (m.peer == sessionInfo.p1)
                continue;
            if (m.peer == me.peer || isOnline (m.peer, now))
                return m.peer;
        }
        return {};
    }

    void Session::startElection (double now)
    {
        electing = true;
        electEpoch = std::max (sessionInfo.epoch, votedEpoch) + 1;
        votedEpoch = electEpoch;
        votes.clear();
        votes[me.peer] = rep.seq();
        electDeadline = now + timing.electWait;
        listener.log ("Player 1 is unreachable: asking the others whether to take over");
        for (const auto& m : sessionInfo.members)
            if (m.peer != me.peer && m.peer != sessionInfo.p1)
                sendTo (m.peer, Json { { "type", "elect" }, { "epoch", electEpoch }, { "seq", rep.seq() } });
        listener.sessionChanged();
    }

    void Session::finishElection (double now)
    {
        if (! electing)
            return;
        electing = false;

        const size_t total = sessionInfo.members.size();
        if (votes.size() * 2 <= total)
        {
            nextElectionAt = now + timing.retryElection;
            listener.log ("No majority to take over yet; waiting for Player 1");
            listener.sessionChanged();
            return;
        }

        // Catch up from whoever got furthest before Player 1 vanished.
        std::string best = me.peer;
        uint64_t bestSeq = rep.seq();
        for (const auto& [peer, seq] : votes)
            if (seq > bestSeq)
            {
                best = peer;
                bestSeq = seq;
            }

        if (best != me.peer)
        {
            catchUpPeer = best;
            catchUpEpoch = electEpoch;
            catchUpDeadline = now + timing.catchUpTimeout;
            sendTo (best, Json { { "type", "resync" }, { "seq", rep.seq() }, { "full", false } });
            return;
        }
        finishTakeover (electEpoch, now);
    }

    void Session::finishTakeover (uint64_t epoch, double now)
    {
        const auto oldP1 = sessionInfo.p1;
        sessionInfo.epoch = epoch;
        sessionInfo.p1 = me.peer;
        electing = false;
        catchUpPeer.clear();
        awaitingResync = false;
        for (auto& m : sessionInfo.members)
            m.online = m.peer == me.peer || isOnline (m.peer, now);
        becomeHost (now);
        listener.log ("You're Player 1 now");
        sendToMembers (Json { { "type", "p1" }, { "epoch", epoch }, { "p1", me.peer }, { "base", sessionInfo.base } });
        broadcastInfo();
        (void) oldP1;
    }

    // ------------------------------------------------------------------ tick

    void Session::tick (double now)
    {
        clockNow = now;
        if (currentRole == Role::idle)
            return;

        if (now - lastPing >= timing.heartbeat)
        {
            lastPing = now;
            sendToMembers (Json { { "type", "ping" }, { "t", now }, { "epoch", sessionInfo.epoch }, { "p1", sessionInfo.p1 },
                                  { "base", sessionInfo.base } });
            if (currentRole == Role::joining && links.count (sessionInfo.p1) != 0)
                sendTo (sessionInfo.p1, Json { { "type", "ping" }, { "t", now }, { "epoch", 0 }, { "p1", "" } });
        }

        if (isPlayer1())
        {
            bool changed = false;
            std::vector<std::string> gone;
            for (auto& m : sessionInfo.members)
            {
                if (m.peer == me.peer)
                    continue;
                const bool online = isOnline (m.peer, now);
                if (m.online != online)
                {
                    m.online = online;
                    changed = true;
                }
                auto it = lastHeard.find (m.peer);
                if (it == lastHeard.end())
                    lastHeard[m.peer] = now;
                else if (now - it->second > timing.dropMemberAfter)
                    gone.push_back (m.peer);
            }
            for (const auto& g : gone)
            {
                removeMember (g);
                changed = true;
            }
            if (changed)
                broadcastInfo();

            if (handingOff && now > handoffDeadline)
            {
                handingOff = false; // the other side never took over: carry on as Player 1
                listener.log ("Handing over didn't complete; you're still Player 1");
                resubmitPending (now);
                listener.sessionChanged();
            }
            return;
        }

        if (currentRole == Role::joining)
            return;

        if (! catchUpPeer.empty() && now > catchUpDeadline)
        {
            catchUpPeer.clear();
            nextElectionAt = now + timing.retryElection;
        }

        if (awaitingResync && links.count (sessionInfo.p1) != 0 && now - resyncSentAt > timing.catchUpTimeout * 2)
        {
            awaitingResync = false;
            requestResync (true);
        }

        if (electing)
        {
            if (now >= electDeadline)
                finishElection (now);
            return;
        }

        if (catchUpPeer.empty() && canTakeOver (now) && sessionInfo.members.size() > 2 && now >= nextElectionAt
            && expectedCandidate (now) == me.peer)
        {
            startElection (now);
        }
    }
}
