#pragma once

#include "Net.h"
#include "tutti/AudioFiles.h"
#include "tutti/Session.h"
#include "tutti/Store.h"
#include "tutti/Tailscale.h"

#include <functional>
#include <map>
#include <mutex>
#include <set>

namespace tutti
{
    struct EngineOptions
    {
        juce::File dataDir;
        juce::File songsDir;
        int peerPort = protocol::kPeerPort;
        int localPort = protocol::kLocalPort;
        juce::String bindAddress;      // empty: Tailscale's address (or 0.0.0.0 without Tailscale)
        juce::StringArray extraPeers;  // "host:port" to probe besides Tailscale peers
        bool useTailscale = true;
        std::function<bool (const juce::File&)> openInLive; // defaults to platform::openInLive
        juce::String appVersion;
        juce::String os;
        juce::String name;             // default display name (else Tailscale's or the computer's)
    };

    // What the UI shows. Rebuilt on request; cheap enough for a few people.
    struct EngineView
    {
        struct Peer
        {
            std::string peer, name, addr, os, app;
            int color = 0;
            bool hosting = false;      // Player 1 of a session we could join
            std::string sessionId, song;
            int members = 0;
        };
        struct Member
        {
            MemberInfo info;
            bool isPlayer1 = false, isMe = false;
            std::string trackName;     // where they are
            double bar = 0;
            bool playing = false, recording = false;
        };
        struct Idea
        {
            std::string track, orig, by, byName, name, origName;
        };

        bool tailscaleInstalled = false, tailscaleRunning = false;
        std::string selfAddress;
        bool liveConnected = false, liveBound = false;
        std::string liveVersion, liveStatus;
        std::vector<Peer> peers;
        Session::Role role = Session::Role::idle;
        SessionInfo session;
        std::vector<Member> members;
        std::vector<std::string> joinRequests, p1Requests; // peer ids
        bool canTakeOver = false, askedForP1 = false, handingOver = false;
        std::vector<Idea> ideas;
        int staleCount = 0;
        int filesWaiting = 0, previewsWaiting = 0;
        std::vector<std::string> notices;
        std::string playRequestBy;     // someone asked everyone to play together (ask mode)
        double playRequestPos = 0;
        std::string myName;
        int myColor = 0;
        std::string playTogetherMode;  // "ask", "auto", "never"
        std::string startStatus;       // progress while starting a session
    };

    class Engine : private Session::Listener
    {
    public:
        explicit Engine (EngineOptions);
        ~Engine() override;

        void startup();
        void shutdown();

        // Runs queued network/Live events and timers. Call often (every ~20 ms) on one thread.
        void pump();

        // Called after anything the UI shows has changed.
        std::function<void()> onChange;

        // --- actions (all on the pump thread)
        bool startSession (const juce::File& als, juce::String& error);
        void joinSession (const std::string& hostPeer);
        void leaveSession();
        void approveJoin (const std::string& peer, bool allow);
        void answerPlayer1Request (const std::string& peer, bool allow);
        void requestPlayer1();
        void handOver (const std::string& peer);
        void takeOver();
        void openSongInLive();
        void playTogether();
        void answerPlayTogether (bool join);
        void stopTogether();
        void setPlayTogetherMode (const std::string& mode);
        void makeIdea (const std::string& trackId);
        void acceptIdea (const std::string& ideaTrack);
        void rejectIdea (const std::string& ideaTrack);
        void setIdentity (const std::string& name, int color);

        EngineView view() const;
        const Session& session() const { return *sessionPtr; }
        Store& store() { return contentStore; }
        const juce::File& songFile() const { return currentSong; }
        const std::string& selfPeer() const { return self.peer; }
        juce::File dataDir() const { return options.dataDir; }

        // Hooks for the save lane / versions (EngineVersions.cpp) and tests.
        void notice (const std::string& text);
        void sendToLive (const Json& message);
        Json liveDoc (const Doc& doc) const;  // document with local audio paths, for the Remote Script
        double now() const;

    private:
        friend struct EngineFiles;

        // --- Session::Listener
        void send (const std::string& peer, const Json& message) override;
        void connectTo (const MemberInfo&) override;
        void liveUpdate (const LiveUpdate&) override;
        void fullResync() override;
        void sessionChanged() override;
        void joinRequested (const MemberInfo&) override;
        void player1Requested (const MemberInfo&) override;
        void player1Denied() override;
        void joined() override;
        void left (const std::string& reason) override;
        void appMessage (const std::string& peer, const Json& message) override;
        void log (const std::string&) override;

        // --- threads -> pump
        void post (std::function<void()> fn);

        // --- peers
        struct LinkState
        {
            std::shared_ptr<net::Link> link;
            std::string peer;          // after hello
            std::string addr;
            bool local = false;        // the Remote Script / Stand-in side
            std::string role;          // "peer", "live", "standin"
        };
        void startServers();
        void discover();
        void connectPeer (const juce::String& host, int port);
        void adoptSocket (std::unique_ptr<juce::StreamingSocket>, bool outgoing, bool local, const juce::String& addr);
        void onFrame (uint64_t linkId, protocol::Frame frame);
        void onClosed (uint64_t linkId);
        void sendHello (LinkState&);
        void handlePeerHello (uint64_t linkId, const Json& hello);
        void broadcastStatus();
        Json helloMessage() const;
        void sendTo (uint64_t linkId, const Json& message);

        // --- Live (Remote Script)
        void handleLive (const Json& message);
        void bindLive();
        void submitFromLive (Ops ops);
        void applyLocalAndSubmit (const Ops& ops);
        Ops toLive (const Ops& ops) const;
        Json toLive (const Json& reconcileItems) const;
        bool songOpenInLive() const;

        // --- starting and joining
        void finishStart (const Json& captured);
        void requestSnapshot();
        void handleSnapshotRequest (const std::string& peer, const Json& message);
        void handleSnapshot (const std::string& peer, const Json& message);
        void maybeOpenJoinedSong();
        juce::File workspaceFor (const std::string& song) const;
        bool writeSongCopy (const juce::XmlElement& set, const juce::File& target, juce::String& error);

        // --- files (EngineFiles.cpp)
        void wantFile (const std::string& hash, bool previewFirst);
        void pumpTransfers();
        void handleFileMessage (const std::string& peer, const Json& message, const protocol::Frame* binary);
        void fileArrived (const std::string& hash, const std::string& kind);
        void serveChunks();
        void prepareLocalFile (const std::string& hash);

        // --- presence, play together, ideas
        void handlePresence (const std::string& peer, const Json& message);
        void handlePlay (const Json& message);
        std::string trackName (const std::string& trackId) const;
        double barOf (double beats) const;

        // --- settings
        void loadSettings();
        void saveSettings() const;

        EngineOptions options;
        MemberInfo self;
        std::unique_ptr<Session> sessionPtr;
        Store contentStore;

        std::mutex queueLock;
        std::deque<std::function<void()>> queue;

        net::Server peerServer, localServer;
        std::map<uint64_t, LinkState> links;
        std::map<std::string, uint64_t> peerLinks; // peer id -> link
        uint64_t nextLinkId = 1;
        uint64_t liveLink = 0;
        std::map<juce::String, double> dialing;    // host:port -> when we last tried
        std::map<std::string, Json> peerHellos;    // peer id -> last hello (for discovery)
        std::optional<tailscale::Status> tailscaleStatus;
        bool tailscaleInstalled = false;
        double lastDiscovery = -1e9;
        bool discovering = false;

        // Live
        Json liveHello;
        bool liveBound = false;
        std::string liveStatus = "Live isn't connected";
        double lastBindAttempt = -1e9;
        std::set<std::string> stale;
        std::deque<Json> pendingLocalBatches; // waiting for audio files to be hashed

        // Session song
        juce::File currentSong;
        std::string currentSongName;
        Json currentMap, currentNotes;
        std::string currentSnapshot;
        std::string startStatus;
        juce::File pendingStartSource;
        juce::File pendingStartCopy;
        bool waitingForCapture = false;
        bool joinedWaitingForSong = false;
        Json joinSnapshot;

        // Files
        struct Transfer
        {
            std::string hash, kind;   // kind: "preview" or "full"
            std::vector<std::string> tried;
            std::string from;
            int64_t received = 0, total = -1;
            double started = 0, retryAt = 0;
            int retries = 0;
            bool active = false;
        };
        std::map<std::string, Transfer> transfers; // key: hash + ":" + kind
        struct Outgoing
        {
            uint64_t link = 0;
            std::string hash, kind;
            juce::File file;
            int64_t offset = 0, total = 0;
            Json meta;
        };
        std::deque<Outgoing> serving;
        std::set<std::string> preparing; // hashes or paths being hashed/previewed in the background
        std::map<std::string, std::string> pathHashes; // local file path -> hash, once hashed

        // Presence and play together
        struct Presence
        {
            std::string track, clip;
            double time = 0;
            bool playing = false, recording = false;
        };
        std::map<std::string, Presence> presence;
        double lastPresenceSent = 0;
        Json pendingPresence;
        std::string playMode = "ask";
        Json playRequest;
        double playRequestAt = 0;
        bool joinedPlay = false;

        std::deque<std::string> notices;
        double lastTick = 0;
    };
}
