#pragma once

#include "Replica.h"

#include <deque>
#include <map>
#include <memory>
#include <set>

namespace tutti
{
    struct MemberInfo
    {
        std::string peer;
        std::string name;
        std::string addr; // Tailscale address the app listens on
        std::string app;  // Tutti version
        std::string live; // Live version, e.g. "12.3.2"
        std::string os;   // "mac" / "windows"
        int color = 0;
        bool online = true;

        Json toJson() const;
        static MemberInfo fromJson (const Json&);
    };

    struct SessionInfo
    {
        std::string id;
        std::string song;
        std::string p1;
        std::string snapshot; // current snapshot (version) id
        uint64_t epoch = 0;   // bumped every time Player 1 changes
        uint64_t base = 0;    // sequence number this epoch's Player 1 started from
        std::vector<MemberInfo> members; // line of succession order

        Json toJson() const;
        static SessionInfo fromJson (const Json&);
        const MemberInfo* member (const std::string& peer) const;
        bool isMember (const std::string& peer) const { return member (peer) != nullptr; }
    };

    // How patient a session is. Defined outside Session so it can be a default argument.
    struct SessionTiming
    {
        double heartbeat = 1.0;       // ping every member this often
        double lostAfter = 10.0;      // Player 1 silent this long = gone
        double voteGrace = 5.0;       // a voter must also have missed Player 1 this long
        double electWait = 2.0;       // how long a candidate waits for votes
        double retryElection = 5.0;
        double catchUpTimeout = 5.0;
        double handoffTimeout = 5.0;
        double dropMemberAfter = 120.0; // Player 1 removes members gone this long
        double priorityWindow = 2.0;    // Player 1 wins clashes within this
    };

    // One computer's view of a session: joining, Player 1's sequencing, handing Player 1 over,
    // automatic takeover when Player 1 disappears, catch-up and clock sync. Pure logic: messages,
    // connections and time come in, messages and Live updates go out through the Listener.
    // Everything runs on one thread.
    class Session
    {
    public:
        enum class Role { idle, joining, guest, player1 };

        struct Listener
        {
            virtual ~Listener() = default;
            virtual void send (const std::string& peer, const Json& message) = 0;
            virtual void connectTo (const MemberInfo&) {}
            virtual void liveUpdate (const LiveUpdate&) {}
            virtual void fullResync() {}                         // Live must match replica().optimistic()
            virtual void sessionChanged() {}
            virtual void joinRequested (const MemberInfo&) {}    // Player 1: someone new wants in
            virtual void player1Requested (const MemberInfo&) {} // Player 1: someone asks for the role
            virtual void player1Denied() {}                      // our request was declined
            virtual void joined() {}                             // welcome arrived: fetch the snapshot
            virtual void left (const std::string& reason) { (void) reason; }
            virtual void appMessage (const std::string& peer, const Json& message) { (void) peer; (void) message; }
            virtual void log (const std::string&) {}
        };

        using Timing = SessionTiming;

        Session (MemberInfo self, Listener&, Timing = {}, uint64_t firstCseq = 1);

        // --- lifecycle
        void start (std::string sessionId, std::string song, Doc doc, uint64_t seq, std::string snapshot, double now);
        void join (const MemberInfo& host, double now);
        void leave (double now);

        // --- Player 1
        void approveJoin (const std::string& peer, bool allow, double now);
        void answerPlayer1Request (const std::string& peer, bool allow, double now);
        bool handOver (const std::string& peer, double now);
        void setSuccession (const std::vector<std::string>& order);
        void setSnapshot (const std::string& snapshotId);
        void setSong (const std::string& song);

        // --- guests
        void requestPlayer1();
        bool canTakeOver (double now) const;
        void takeOver (double now);

        // --- edits from our Live
        void submitLocal (Ops ops, double now);

        // --- transport
        void connected (const std::string& peer, double now);
        void disconnected (const std::string& peer, double now);
        void receive (const std::string& peer, const Json& message, double now);
        void tick (double now);

        // Sends an app-level message to every other member we're connected to.
        void broadcast (const Json& message);

        // --- state
        Role role() const { return currentRole; }
        bool isPlayer1() const { return currentRole == Role::player1; }
        bool inSession() const { return currentRole == Role::guest || currentRole == Role::player1; }
        const SessionInfo& info() const { return sessionInfo; }
        const MemberInfo& self() const { return me; }
        const Replica& replica() const { return rep; }
        bool isConnected (const std::string& peer) const { return links.count (peer) != 0; }
        bool isOnline (const std::string& peer, double now) const;
        const std::set<std::string>& pendingJoinRequests() const { return joinRequests; }
        const std::set<std::string>& pendingPlayer1Requests() const { return p1Requests; }
        bool player1Requested() const { return askedForP1; }

        // Player 1's clock = our clock + offset (0 for Player 1).
        double player1ClockOffset() const { return isPlayer1() ? 0.0 : clockOffset; }

        void updateSelf (const MemberInfo& updated);

    private:
        void sendTo (const std::string& peer, const Json& message);
        void sendToMembers (const Json& message, const std::string& except = {});
        void broadcastInfo();
        void becomeHost (double now);
        void sequenceBatch (const std::string& author, uint64_t cseq, const Ops& ops, double now);
        void resubmitPending (double now);
        void requestResync (bool full);
        void adoptNewPlayer1 (const std::string& p1, uint64_t epoch, uint64_t base, double now);
        void startElection (double now);
        void finishElection (double now);
        void finishTakeover (uint64_t epoch, double now);
        void admit (const MemberInfo& member, double now);
        void removeMember (const std::string& peer);
        void deliver (const LiveUpdate&);
        void handleLog (const std::string& peer, const Json& message, double now);
        void handleState (const std::string& peer, const Json& message, double now);
        void caughtUp (double now);
        bool fromCurrentPlayer1 (const std::string& peer, const Json& message, double now);
        std::string expectedCandidate (double now) const;
        void resetToIdle (const std::string& reason);

        MemberInfo me;
        Listener& listener;
        Timing timing;
        Role currentRole = Role::idle;
        SessionInfo sessionInfo;
        Replica rep;

        // Player 1 only.
        Doc hostDoc;
        std::unique_ptr<Sequencer> sequencer;
        std::map<std::string, uint64_t> lastCseq;
        std::set<std::string> approved;
        std::map<std::string, MemberInfo> waitingToJoin;
        std::set<std::string> joinRequests, p1Requests;

        std::set<std::string> links;
        std::map<std::string, double> lastHeard;
        double lastPing = -1e9;

        // Catch-up and recovery.
        bool awaitingResync = false;
        double resyncSentAt = 0;
        double clockNow = 0; // the latest `now` we were given
        bool deferResubmit = false;
        Ops extras; // our edits lost in a split, re-added once we have Player 1's state

        // Elections and handoffs.
        bool electing = false;
        uint64_t electEpoch = 0;
        double electDeadline = 0;
        std::map<std::string, uint64_t> votes;
        uint64_t votedEpoch = 0;
        double nextElectionAt = 0;
        std::string catchUpPeer;
        uint64_t catchUpEpoch = 0;
        double catchUpDeadline = 0;
        bool handingOff = false;
        std::string handoffTo;
        double handoffDeadline = 0;
        bool askedForP1 = false;

        // Clock sync with Player 1: (rtt, offset) samples.
        std::deque<std::pair<double, double>> clockSamples;
        double clockOffset = 0;
    };

    const char* toString (Session::Role);
}
