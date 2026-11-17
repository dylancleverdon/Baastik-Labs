#include "tutti/Session.h"

#include <catch2/catch_test_macros.hpp>

#include <deque>
#include <map>
#include <random>
#include <set>

using namespace tutti;

namespace
{
    void applyToLive (Doc& live, const LiveUpdate& u)
    {
        for (const auto& op : u.apply)
            apply (live, op);
        for (const auto& r : u.reconcile)
        {
            const auto id = r["id"].get<std::string>();
            if (r["state"].is_null())
            {
                live.remove (id);
                continue;
            }
            for (const auto& e : r["state"])
            {
                const auto eid = e["id"].get<std::string>();
                if (const auto* existing = live.find (eid))
                {
                    Json patch = e["v"];
                    for (auto it = existing->props.begin(); it != existing->props.end(); ++it)
                        if (! patch.contains (it.key()))
                            patch[it.key()] = nullptr;
                    live.set (eid, patch);
                }
                else
                    live.create (eid, e["t"].get<std::string>(), e["p"].get<std::string>(), e["v"]);
            }
        }
    }

    struct Network;

    // One computer: its Session plus a Doc standing in for its Live Set.
    struct Node : Session::Listener
    {
        Network& net;
        std::string id;
        std::unique_ptr<Session> session;
        Doc live;
        bool up = true;
        std::vector<std::string> joinAsks, p1Asks;
        int denied = 0;

        Node (Network& n, const std::string& peer);
        void send (const std::string& peer, const Json& message) override;
        void connectTo (const MemberInfo& m) override;
        void liveUpdate (const LiveUpdate& u) override { applyToLive (live, u); }
        void fullResync() override { live = session->replica().optimistic(); }
        void joinRequested (const MemberInfo& m) override { joinAsks.push_back (m.peer); }
        void player1Requested (const MemberInfo& m) override { p1Asks.push_back (m.peer); }
        void player1Denied() override { ++denied; }
    };

    struct Network
    {
        std::map<std::string, std::unique_ptr<Node>> nodes;
        std::set<std::pair<std::string, std::string>> links;      // both directions stored
        std::set<std::pair<std::string, std::string>> blocked;    // partitioned pairs
        std::map<std::pair<std::string, std::string>, std::deque<std::pair<double, Json>>> queues;
        std::deque<std::pair<std::string, std::string>> connectRequests;
        std::mt19937 rng;
        double now = 0;
        double latency = 0.03;
        int counter = 0;

        explicit Network (unsigned seed = 1) : rng (seed) {}

        Node& add (const std::string& id)
        {
            auto node = std::make_unique<Node> (*this, id);
            auto& ref = *node;
            nodes[id] = std::move (node);
            return ref;
        }

        Node& operator[] (const std::string& id) { return *nodes.at (id); }

        bool canLink (const std::string& a, const std::string& b) const
        {
            return a != b && nodes.at (a)->up && nodes.at (b)->up && blocked.count ({ a, b }) == 0;
        }

        void connect (const std::string& a, const std::string& b)
        {
            if (! canLink (a, b) || links.count ({ a, b }) != 0)
                return;
            links.insert ({ a, b });
            links.insert ({ b, a });
            nodes[a]->session->connected (b, now);
            nodes[b]->session->connected (a, now);
        }

        void cut (const std::string& a, const std::string& b)
        {
            if (links.count ({ a, b }) == 0)
                return;
            links.erase ({ a, b });
            links.erase ({ b, a });
            queues.erase ({ a, b });
            queues.erase ({ b, a });
            nodes[a]->session->disconnected (b, now);
            nodes[b]->session->disconnected (a, now);
        }

        void partition (const std::set<std::string>& side)
        {
            for (auto& [a, na] : nodes)
                for (auto& [b, nb] : nodes)
                    if (a != b && (side.count (a) != 0) != (side.count (b) != 0))
                    {
                        blocked.insert ({ a, b });
                        cut (a, b);
                    }
        }

        void heal()
        {
            blocked.clear();
            for (auto& [a, na] : nodes)
                for (auto& [b, nb] : nodes)
                    if (a < b && na->session->inSession() && nb->session->inSession())
                        connect (a, b);
        }

        void send (const std::string& from, const std::string& to, const Json& message)
        {
            if (links.count ({ from, to }) == 0)
                return;
            queues[{ from, to }].push_back ({ now + latency, message });
        }

        void step (double dt = 0.05)
        {
            now += dt;
            while (! connectRequests.empty())
            {
                auto [a, b] = connectRequests.front();
                connectRequests.pop_front();
                connect (a, b);
            }
            // Deliver due messages, one link at a time in FIFO order.
            for (bool delivered = true; delivered;)
            {
                delivered = false;
                for (auto& [key, q] : queues)
                {
                    if (q.empty() || q.front().first > now)
                        continue;
                    auto message = std::move (q.front().second);
                    q.pop_front();
                    auto& to = *nodes[key.second];
                    if (to.up)
                        to.session->receive (key.first, message, now);
                    delivered = true;
                    break; // the map may have changed
                }
            }
            for (auto& [id, n] : nodes)
                if (n->up)
                    n->session->tick (now);
        }

        void run (double seconds)
        {
            const double end = now + seconds;
            while (now < end)
                step();
        }

        int roll (int n) { return std::uniform_int_distribution<int> (0, n - 1) (rng); }

        void randomEdit (Node& n)
        {
            if (! n.session->inSession())
                return;
            const auto& doc = n.live;
            std::vector<std::string> ids;
            for (const auto& e : doc.subtree ("song"))
                if (e != "song")
                    ids.push_back (e);
            const auto fresh = n.id + "-" + std::to_string (++counter);
            Ops ops;
            const int what = roll (10);
            if (what < 4 || ids.empty())
                ops.push_back (Op::create (fresh, "track", "song", { { "pos", roll (100) }, { "vol", roll (10) } }));
            else if (what < 8)
                ops.push_back (Op::set (ids[(size_t) roll ((int) ids.size())], { { "vol", roll (50) } }));
            else
                ops.push_back (Op::remove (ids[(size_t) roll ((int) ids.size())]));
            for (const auto& op : ops)
                apply (n.live, op);
            n.session->submitLocal (ops, now);
        }

        std::vector<Node*> members()
        {
            std::vector<Node*> out;
            for (auto& [id, n] : nodes)
                if (n->up && n->session->inSession())
                    out.push_back (n.get());
            return out;
        }

        // Everyone in the session agrees with Player 1, and their Live matches.
        void checkConverged()
        {
            auto list = members();
            REQUIRE_FALSE (list.empty());
            int p1Count = 0;
            const Node* p1 = nullptr;
            for (auto* n : list)
                if (n->session->isPlayer1())
                {
                    ++p1Count;
                    p1 = n;
                }
            REQUIRE (p1Count == 1);
            const auto& truth = p1->session->replica().confirmed();
            for (auto* n : list)
            {
                INFO ("node " << n->id);
                CHECK (n->session->info().p1 == p1->id);
                CHECK (n->session->info().epoch == p1->session->info().epoch);
                CHECK (n->session->replica().pending().empty());
                CHECK (n->session->replica().confirmed() == truth);
                CHECK (n->live == truth);
            }
        }
    };

    Node::Node (Network& n, const std::string& peer) : net (n), id (peer)
    {
        MemberInfo me;
        me.peer = peer;
        me.name = "Name " + peer;
        session = std::make_unique<Session> (me, *this);
    }

    void Node::send (const std::string& peer, const Json& message) { net.send (id, peer, message); }
    void Node::connectTo (const MemberInfo& m) { net.connectRequests.push_back ({ id, m.peer }); }

    MemberInfo hostInfo (Network& net, const std::string& id) { return net[id].session->self(); }

    // A starts a session, the others join and A lets them in.
    void startWith (Network& net, const std::vector<std::string>& ids)
    {
        for (const auto& id : ids)
            net.add (id);
        Doc base;
        base.create ("t0", "track", "song", { { "pos", 0 }, { "name", "Drums" } });
        net[ids[0]].live = base;
        net[ids[0]].session->start ("s1", "Night Drive", base, 0, "v0", net.now);

        for (size_t i = 1; i < ids.size(); ++i)
        {
            net[ids[i]].session->join (hostInfo (net, ids[0]), net.now);
            net.run (0.5);
            REQUIRE (net[ids[0]].joinAsks.size() == i);
            net[ids[0]].session->approveJoin (ids[i], true, net.now);
            net.run (0.5);
            REQUIRE (net[ids[i]].session->role() == Session::Role::guest);
            net[ids[i]].live = net[ids[i]].session->replica().optimistic(); // "opened the snapshot"
        }
        net.run (1.0);
    }

    void editFor (Network& net, double seconds, int perStep = 1)
    {
        const double end = net.now + seconds;
        while (net.now < end)
        {
            auto list = net.members();
            for (int i = 0; i < perStep && ! list.empty(); ++i)
                net.randomEdit (*list[(size_t) net.roll ((int) list.size())]);
            net.step();
        }
    }
}

TEST_CASE ("Joining needs Player 1's OK, then everyone edits and converges", "[session]")
{
    Network net (7);
    startWith (net, { "a", "b", "c" });
    CHECK (net["a"].session->isPlayer1());
    CHECK (net["b"].session->info().members.size() == 3);

    editFor (net, 20.0, 2);
    net.run (3.0);
    net.checkConverged();
}

TEST_CASE ("Player 1 can say no to a joiner", "[session]")
{
    Network net;
    net.add ("a");
    net.add ("b");
    net["a"].session->start ("s1", "Song", Doc(), 0, "v0", 0);
    net["b"].session->join (hostInfo (net, "a"), 0);
    net.run (0.5);
    net["a"].session->approveJoin ("b", false, net.now);
    net.run (0.5);
    CHECK (net["b"].session->role() == Session::Role::idle);
}

TEST_CASE ("Ask to be Player 1: approve hands over, deny keeps it", "[session]")
{
    Network net (3);
    startWith (net, { "a", "b", "c" });

    net["c"].session->requestPlayer1();
    net.run (0.5);
    REQUIRE (net["a"].p1Asks == std::vector<std::string> { "c" });
    net["a"].session->answerPlayer1Request ("c", false, net.now);
    net.run (0.5);
    CHECK (net["c"].denied == 1);
    CHECK (net["a"].session->isPlayer1());

    net["b"].session->requestPlayer1();
    net.run (0.5);
    net["a"].session->answerPlayer1Request ("b", true, net.now);
    editFor (net, 5.0);
    net.run (2.0);
    CHECK (net["b"].session->isPlayer1());
    CHECK (net["b"].session->info().epoch == 2);
    net.checkConverged();
}

TEST_CASE ("Edits made during a handover aren't lost", "[session]")
{
    Network net (11);
    startWith (net, { "a", "b", "c" });
    editFor (net, 2.0, 3);
    net["a"].session->handOver ("c", net.now);
    editFor (net, 5.0, 3); // keep editing straight through the switch
    net.run (3.0);
    CHECK (net["c"].session->isPlayer1());
    net.checkConverged();
}

TEST_CASE ("Next in line takes over when Player 1 vanishes", "[session]")
{
    Network net (5);
    startWith (net, { "a", "b", "c", "d" });
    editFor (net, 3.0, 2);

    net["a"].up = false; // crash
    for (const auto& other : { "b", "c", "d" })
        net.cut ("a", other);

    editFor (net, 15.0); // others keep working; their edits wait for the new Player 1
    net.run (3.0);
    CHECK (net["b"].session->isPlayer1());
    CHECK (net["b"].session->info().epoch == 2);
    net.checkConverged();

    // The old Player 1 comes back as a guest.
    net["a"].up = true;
    net.heal();
    net.run (5.0);
    CHECK_FALSE (net["a"].session->isPlayer1());
    net.checkConverged();
}

TEST_CASE ("A cut-off Player 1 rejoins as a guest and keeps its own edits", "[session]")
{
    Network net (9);
    startWith (net, { "a", "b", "c" });
    editFor (net, 2.0);

    net.partition ({ "a" });
    // a keeps editing on its own (it still thinks it's Player 1)...
    for (int i = 0; i < 20; ++i)
    {
        net.randomEdit (net["a"]);
        net.step();
    }
    const auto aCreated = net["a"].session->replica().confirmed();
    net.run (15.0);
    CHECK (net["b"].session->isPlayer1());

    net.heal();
    net.run (6.0);
    CHECK_FALSE (net["a"].session->isPlayer1());
    net.checkConverged();

    // Tracks a created while cut off made it into the shared song (unless someone removed them).
    const auto& truth = net["b"].session->replica().confirmed();
    int survived = 0;
    for (const auto& id : aCreated.subtree ("song"))
        if (id.rfind ("a-", 0) == 0 && truth.has (id))
            ++survived;
    CHECK (survived > 0);
}

TEST_CASE ("A minority can't take over", "[session]")
{
    Network net (4);
    startWith (net, { "a", "b", "c", "d" });
    net.partition ({ "a", "b" });
    net.run (30.0);
    CHECK (net["a"].session->isPlayer1());
    CHECK_FALSE (net["c"].session->isPlayer1());
    CHECK_FALSE (net["d"].session->isPlayer1());

    net.heal();
    net.run (5.0);
    net.checkConverged();
}

TEST_CASE ("Two people: no automatic takeover, but Take over works", "[session]")
{
    Network net (2);
    startWith (net, { "a", "b" });
    net.partition ({ "a" });
    net.run (30.0);
    CHECK_FALSE (net["b"].session->isPlayer1());
    CHECK (net["b"].session->canTakeOver (net.now));
    net["b"].session->takeOver (net.now);
    CHECK (net["b"].session->isPlayer1());
    CHECK (net["b"].session->info().epoch == 2);

    net.heal();
    net.run (5.0);
    net.checkConverged();
}

TEST_CASE ("Player 1 leaving hands the session to the next person", "[session]")
{
    Network net (6);
    startWith (net, { "a", "b", "c" });
    editFor (net, 2.0);
    net["a"].session->leave (net.now);
    editFor (net, 3.0);
    net.run (3.0);
    CHECK (net["a"].session->role() == Session::Role::idle);
    CHECK (net["b"].session->isPlayer1());
    net.checkConverged();
}

TEST_CASE ("Clock offset to Player 1 is estimated from heartbeats", "[session]")
{
    Network net (8);
    startWith (net, { "a", "b" });
    net.run (5.0);
    // Same simulated clock everywhere, so the offset should be ~0.
    CHECK (std::abs (net["b"].session->player1ClockOffset()) < 0.01);
}

TEST_CASE ("Chaos: edits, splits, crashes and handovers still converge", "[session][simulation]")
{
    uint64_t playerOneChanges = 0;
    size_t totalOps = 0;
    for (unsigned seed = 1; seed <= 12; ++seed)
    {
        INFO ("seed " << seed);
        Network net (seed);
        startWith (net, { "a", "b", "c", "d" });
        for (int round = 0; round < 8; ++round)
        {
            editFor (net, 2.0 + net.roll (4), 2);
            const int event = net.roll (4);
            if (event == 0)
            {
                // Hand Player 1 to someone random.
                for (auto* n : net.members())
                    if (n->session->isPlayer1())
                    {
                        auto list = net.members();
                        auto* target = list[(size_t) net.roll ((int) list.size())];
                        n->session->handOver (target->id, net.now);
                        break;
                    }
            }
            else if (event == 1)
            {
                std::set<std::string> side;
                for (auto& [id, n] : net.nodes)
                    if (net.roll (3) == 0)
                        side.insert (id);
                net.partition (side);
                editFor (net, 14.0);
                net.heal();
            }
        }
        net.heal();
        net.run (12.0);
        net.checkConverged();
        for (auto* n : net.members())
            if (n->session->isPlayer1())
            {
                playerOneChanges += n->session->info().epoch - 1;
                totalOps += n->session->replica().confirmed().size();
            }
    }
    // Make sure the chaos really happened: several Player 1 changes and plenty of edits.
    CHECK (playerOneChanges >= 12);
    CHECK (totalOps > 200);
    UNSCOPED_INFO ("Player 1 changes: " << playerOneChanges << ", entities: " << totalOps);
}
