#include "tutti/Replica.h"

#include <catch2/catch_test_macros.hpp>

#include <deque>
#include <random>

using namespace tutti;

namespace
{
    // Applies a LiveUpdate to a Doc standing in for one computer's Live Set, the way the Remote
    // Script does: ops are best effort, reconcile makes entities match the given state exactly.
    void applyToLive (Doc& live, const LiveUpdate& u)
    {
        for (const auto& op : u.apply)
            apply (live, op);

        for (const auto& r : u.reconcile)
        {
            const auto& state = r["state"];
            const auto id = r["id"].get<std::string>();
            if (state.is_null())
            {
                live.remove (id);
                continue;
            }
            for (const auto& e : state)
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
                {
                    live.create (eid, e["t"].get<std::string>(), e["p"].get<std::string>(), e["v"]);
                }
            }
        }
    }

    struct Message
    {
        enum class Kind { submit, ack, remote } kind;
        std::string author;
        uint64_t cseq = 0;
        uint64_t seq = 0;
        Ops ops;
        std::vector<Reconcile> reconcile;
    };

    struct Client
    {
        std::string id;
        Replica replica;
        Doc live;
        std::deque<Message> toHost, fromHost;

        explicit Client (std::string peer) : id (peer), replica (peer) {}
    };

    struct Simulation
    {
        std::mt19937 rng;
        Doc hostDoc;
        Sequencer sequencer { hostDoc };
        std::vector<std::unique_ptr<Client>> clients;
        double now = 0;
        int counter = 0;

        explicit Simulation (unsigned seed, int numClients) : rng (seed)
        {
            for (int i = 0; i < numClients; ++i)
                clients.push_back (std::make_unique<Client> ("c" + std::to_string (i)));
            sequencer.setPlayer1 ("c0");
        }

        int roll (int n) { return std::uniform_int_distribution<int> (0, n - 1) (rng); }

        std::string pick (const Doc& doc, const std::string& type)
        {
            std::vector<std::string> ids;
            for (const auto& id : doc.subtree ("song"))
                if (id != "song" && (type.empty() || doc.find (id)->type == type))
                    ids.push_back (id);
            return ids.empty() ? std::string() : ids[(size_t) roll ((int) ids.size())];
        }

        void localEdit (Client& c)
        {
            Ops ops;
            const int what = roll (10);
            const auto fresh = c.id + "-" + std::to_string (++counter);
            if (what < 3)
                ops.push_back (Op::create (fresh, "track", "song", { { "pos", roll (100) }, { "vol", roll (10) } }));
            else if (what < 5)
            {
                if (auto t = pick (c.live, "track"); ! t.empty())
                    ops.push_back (Op::create (fresh, "clip", t, { { "start", roll (64) } }));
            }
            else if (what < 7)
            {
                if (auto cl = pick (c.live, "clip"); ! cl.empty())
                    ops.push_back (Op::create (fresh, "note", cl, { { "p", roll (128) } }));
            }
            else if (what < 9)
            {
                if (auto e = pick (c.live, ""); ! e.empty())
                {
                    static const char* keys[] = { "vol", "name", "p", "start" };
                    ops.push_back (Op::set (e, { { keys[roll (4)], roll (50) } }));
                }
            }
            else if (auto e = pick (c.live, ""); ! e.empty())
                ops.push_back (Op::remove (e));

            if (ops.empty())
                return;
            for (const auto& op : ops)
                apply (c.live, op);
            auto batch = c.replica.local (ops);
            c.toHost.push_back ({ Message::Kind::submit, c.id, batch.cseq, 0, batch.ops, {} });
        }

        void hostStep (Client& c)
        {
            if (c.toHost.empty())
                return;
            auto m = std::move (c.toHost.front());
            c.toHost.pop_front();
            now += 0.05 * roll (20);

            auto r = sequencer.submit (m.author, m.ops, now);
            c.fromHost.push_back ({ Message::Kind::ack, m.author, m.cseq, r.seq, r.accepted, r.reconcile });
            if (r.seq != 0)
                for (auto& other : clients)
                    if (other->id != m.author)
                        other->fromHost.push_back ({ Message::Kind::remote, m.author, m.cseq, r.seq, r.accepted, {} });
        }

        void clientStep (Client& c)
        {
            if (c.fromHost.empty())
                return;
            auto m = std::move (c.fromHost.front());
            c.fromHost.pop_front();
            LiveUpdate u;
            if (m.kind == Message::Kind::ack)
                u = c.replica.ack (m.cseq, m.seq, m.ops, m.reconcile);
            else
                u = c.replica.remote ({ m.seq, m.author, m.cseq, m.ops });
            REQUIRE_FALSE (u.needResync);
            applyToLive (c.live, u);
        }

        void run (int steps)
        {
            for (int i = 0; i < steps; ++i)
            {
                auto& c = *clients[(size_t) roll ((int) clients.size())];
                switch (roll (3))
                {
                    case 0: localEdit (c); break;
                    case 1: hostStep (c); break;
                    default: clientStep (c); break;
                }
            }
            // Quiesce: deliver everything.
            for (bool busy = true; busy;)
            {
                busy = false;
                for (auto& c : clients)
                {
                    while (! c->toHost.empty()) { hostStep (*c); busy = true; }
                    while (! c->fromHost.empty()) { clientStep (*c); busy = true; }
                }
            }
        }
    };
}

TEST_CASE ("Replica forwards others' edits but keeps our pending ones", "[replica]")
{
    Replica r ("me");
    Doc base;
    base.create ("t1", "track", "song", { { "vol", 0.5 }, { "name", "Bass" } });
    r.reset (base, 10);

    r.local ({ Op::set ("t1", { { "vol", 0.9 } }) });
    auto u = r.remote ({ 11, "other", 1, { Op::set ("t1", { { "vol", 0.1 }, { "name", "Sub" } }) } });
    REQUIRE (u.apply.size() == 1);
    CHECK (u.apply[0].props == Json { { "name", "Sub" } }); // vol is ours (pending)
    CHECK (r.confirmed().find ("t1")->props["vol"] == 0.1);
    CHECK (r.optimistic().find ("t1")->props["vol"] == 0.9);
    CHECK (r.seq() == 11);
}

TEST_CASE ("Replica reconciles rejected edits to the confirmed state", "[replica]")
{
    Replica r ("me");
    Doc base;
    base.create ("t1", "track", "song", { { "vol", 0.5 } });
    base.create ("c1", "clip", "t1");
    base.create ("n1", "note", "c1", { { "p", 60 } });
    r.reset (base, 1);

    const auto batch = r.local ({ Op::remove ("c1") });
    auto u = r.ack (batch.cseq, 0, {}, { { "c1", true } });
    CHECK (r.pending().empty());
    REQUIRE (u.reconcile.size() == 1);
    const auto& state = u.reconcile[0]["state"];
    REQUIRE (state.size() == 2); // clip and its note, parents first
    CHECK (state[0]["id"] == "c1");
    CHECK (state[1]["id"] == "n1");
}

TEST_CASE ("Replica detects gaps and duplicates", "[replica]")
{
    Replica r ("me");
    r.reset (Doc(), 5);
    CHECK (r.remote ({ 5, "x", 1, {} }).empty());       // duplicate
    CHECK (r.remote ({ 7, "x", 2, {} }).needResync);    // gap
    CHECK_FALSE (r.remote ({ 6, "x", 3, {} }).needResync);
    CHECK (r.logSince (5).size() == 1);
}

TEST_CASE ("Our own batch seen in a catch-up counts as accepted", "[replica]")
{
    Replica r ("me");
    r.reset (Doc(), 0);
    const auto b = r.local ({ Op::create ("t1", "track", "song") });
    auto u = r.remote ({ 1, "me", b.cseq, b.ops });
    CHECK (u.apply.empty());
    CHECK (r.pending().empty());
    CHECK (r.confirmed().has ("t1"));
}

TEST_CASE ("Everyone converges with random concurrent edits", "[replica][simulation]")
{
    for (unsigned seed = 1; seed <= 40; ++seed)
    {
        Simulation sim (seed, seed % 3 == 0 ? 4 : 3);
        sim.run (1500);
        for (const auto& c : sim.clients)
        {
            INFO ("seed " << seed << " client " << c->id);
            CHECK (c->replica.pending().empty());
            CHECK (c->replica.confirmed() == sim.hostDoc);
            CHECK (c->live == sim.hostDoc);
            CHECK (c->replica.seq() == sim.sequencer.lastSeq());
        }
    }
}
