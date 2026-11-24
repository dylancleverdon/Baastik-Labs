// tutti-peer: a headless Tutti. Reads JSON commands on stdin, writes JSON events on stdout.
//
//   tutti-peer --data DIR --songs DIR --port 47380 --local-port 47381 --bind 127.0.0.1
//              --peer HOST:PORT [--peer ...] --name Alice [--no-tailscale]
//
// Commands: start {als}, join {peer}, approve {peer, allow}, request_p1, answer_p1 {peer, allow},
//           handover {peer}, takeover, leave, play, answer_play {join}, idea {track},
//           accept_idea {track}, reject_idea {track}, state, quit
// Events:   {"event":"open","path":...} when Tutti asks Live to open a song,
//           {"event":"state", ...} in reply to "state".

#include <csignal>
#include "Engine.h"
#include "Platform.h"

#include <iostream>
#include <thread>

using namespace tutti;

namespace
{
    Json viewJson (const EngineView& v, const Engine& engine)
    {
        Json members = Json::array();
        for (const auto& m : v.members)
            members.push_back (Json { { "peer", m.info.peer }, { "name", m.info.name }, { "p1", m.isPlayer1 },
                                      { "me", m.isMe }, { "online", m.info.online }, { "track", m.trackName },
                                      { "bar", m.bar }, { "playing", m.playing } });
        Json peers = Json::array();
        for (const auto& p : v.peers)
            peers.push_back (Json { { "peer", p.peer }, { "name", p.name }, { "hosting", p.hosting },
                                    { "session", p.sessionId }, { "song", p.song } });
        Json ideas = Json::array();
        for (const auto& i : v.ideas)
            ideas.push_back (Json { { "track", i.track }, { "orig", i.orig }, { "by", i.by }, { "name", i.name } });
        return Json { { "event", "state" }, { "peer", engine.selfPeer() }, { "role", toString (v.role) },
                      { "session", v.session.toJson() }, { "members", members }, { "peers", peers },
                      { "join_requests", v.joinRequests }, { "p1_requests", v.p1Requests },
                      { "live_connected", v.liveConnected }, { "live_bound", v.liveBound }, { "live_status", v.liveStatus },
                      { "stale", v.staleCount }, { "files_waiting", v.filesWaiting }, { "previews_waiting", v.previewsWaiting },
                      { "notices", v.notices }, { "ideas", ideas }, { "can_take_over", v.canTakeOver },
                      { "doc", engine.session().replica().confirmed().toJson() }, { "seq", engine.session().replica().seq() },
                      { "song_file", engine.songFile().getFullPathName().toStdString() },
                      { "update_available", v.updateAvailable }, { "versions", v.versions.size() },
                      { "save_to_share", v.saveToShare }, { "activity", v.activity } };
    }

    void emit (const Json& j)
    {
        std::cout << j.dump() << std::endl;
    }
}

int main (int argc, char* argv[])
{
   #if ! JUCE_WINDOWS
    std::signal (SIGPIPE, SIG_IGN); // a friend's closed connection must not kill us
   #endif
    juce::StringArray args;
    for (int i = 1; i < argc; ++i)
        args.add (argv[i]);
    auto value = [&] (const char* name, const juce::String& fallback = {}) {
        const int i = args.indexOf (name);
        return i >= 0 && i + 1 < args.size() ? args[i + 1] : fallback;
    };

    EngineOptions options;
    options.dataDir = juce::File (value ("--data", juce::File::getCurrentWorkingDirectory().getChildFile ("tutti-data").getFullPathName()));
    options.songsDir = juce::File (value ("--songs", options.dataDir.getChildFile ("Songs").getFullPathName()));
    options.peerPort = value ("--port", "47380").getIntValue();
    options.localPort = value ("--local-port", "47381").getIntValue();
    options.bindAddress = value ("--bind");
    options.useTailscale = ! args.contains ("--no-tailscale");
    options.name = value ("--name");
    for (int i = 0; i < args.size(); ++i)
        if (args[i] == "--peer" && i + 1 < args.size())
            options.extraPeers.add (args[i + 1]);
    options.openInLive = [] (const juce::File& f) {
        emit (Json { { "event", "open" }, { "path", f.getFullPathName().toStdString() } });
        return true;
    };

    // The end-to-end test plays Live: it saves the song when Tutti "presses" Save.
    options.pressSaveInLive = [] (bool bringToFront) {
        emit (Json { { "event", "press_save" }, { "front", bringToFront } });
        return static_cast<int> (saveKeyPressed);
    };

    Engine engine (options);
    engine.startup();

    std::mutex lock;
    std::deque<std::string> lines;
    std::atomic<bool> eof { false };
    std::thread reader ([&] {
        std::string line;
        while (std::getline (std::cin, line))
        {
            std::lock_guard<std::mutex> g (lock);
            lines.push_back (line);
        }
        eof = true;
    });
    reader.detach();

    emit (Json { { "event", "ready" }, { "peer", engine.selfPeer() } });
    for (;;)
    {
        std::deque<std::string> work;
        {
            std::lock_guard<std::mutex> g (lock);
            work.swap (lines);
        }
        for (const auto& line : work)
        {
            auto cmd = parseJson (line);
            if (! cmd)
                continue;
            const auto c = str (*cmd, "cmd");
            if (c == "quit")
            {
                engine.shutdown();
                return 0;
            }
            if (c == "state")
                emit (viewJson (engine.view(), engine));
            else if (c == "start")
            {
                juce::String error;
                if (! engine.startSession (juce::File (juce::String (str (*cmd, "als"))), error))
                    emit (Json { { "event", "error" }, { "text", error.toStdString() } });
            }
            else if (c == "trust")
                engine.trustAndLetIn (str (*cmd, "peer"));
            else if (c == "lock")
                engine.setLockTracks (boolean (*cmd, "on", true));
            else if (c == "undo")
                engine.undoMine();
            else if (c == "chat")
                engine.sendMessage (str (*cmd, "text"));
            else if (c == "auto_get")
                engine.setAutoGet (boolean (*cmd, "on", true));
            else if (c == "install_script")
                emit (Json { { "event", "installed" },
                             { "ok", platform::installRemoteScriptTo (juce::File (juce::String (str (*cmd, "from"))),
                                                                      juce::File (juce::String (str (*cmd, "to")))) } });
            else if (c == "join")
                engine.joinSession (str (*cmd, "peer"));
            else if (c == "approve")
                engine.approveJoin (str (*cmd, "peer"), boolean (*cmd, "allow", true));
            else if (c == "request_p1")
                engine.requestPlayer1();
            else if (c == "answer_p1")
                engine.answerPlayer1Request (str (*cmd, "peer"), boolean (*cmd, "allow", true));
            else if (c == "handover")
                engine.handOver (str (*cmd, "peer"));
            else if (c == "takeover")
                engine.takeOver();
            else if (c == "leave")
                engine.leaveSession();
            else if (c == "play")
                engine.playTogether();
            else if (c == "answer_play")
                engine.answerPlayTogether (boolean (*cmd, "join", true));
            else if (c == "play_mode")
                engine.setPlayTogetherMode (str (*cmd, "mode"));
            else if (c == "idea")
                engine.makeIdea (str (*cmd, "track"));
            else if (c == "accept_idea")
                engine.acceptIdea (str (*cmd, "track"));
            else if (c == "reject_idea")
                engine.rejectIdea (str (*cmd, "track"));
            else if (c == "update")
                engine.updateNow();
        }
        engine.pump();
        if (eof && work.empty())
        {
            engine.shutdown();
            return 0;
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (10));
    }
}
