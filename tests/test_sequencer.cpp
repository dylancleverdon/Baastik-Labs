#include "tutti/Sequencer.h"

#include <catch2/catch_test_macros.hpp>

using namespace tutti;

namespace
{
    struct Fixture
    {
        Doc doc;
        Sequencer seq { doc };

        Fixture()
        {
            seq.setPlayer1 ("p1");
            seq.submit ("p1", { Op::create ("t1", "track", "song", { { "vol", 0.5 } }),
                                Op::create ("c1", "clip", "t1"),
                                Op::create ("n1", "note", "c1", { { "p", 60 } }) },
                        0.0);
        }
    };
}

TEST_CASE ("Sequencer numbers accepted batches", "[sequencer]")
{
    Fixture f;
    CHECK (f.seq.lastSeq() == 1);
    auto r = f.seq.submit ("g", { Op::set ("t1", { { "vol", 0.7 } }) }, 10.0);
    CHECK (r.seq == 2);
    CHECK (r.reconcile.empty());
    CHECK (f.doc.find ("t1")->props["vol"] == 0.7);

    // Nothing applicable: no sequence number used.
    auto none = f.seq.submit ("g", { Op::set ("ghost", { { "vol", 1.0 } }) }, 11.0);
    CHECK (none.seq == 0);
    CHECK (none.accepted.empty());
    CHECK (none.reconcile == std::vector<Reconcile> { { "ghost", false } });
    CHECK (f.seq.lastSeq() == 2);
}

TEST_CASE ("Player 1 wins a property for the priority window", "[sequencer]")
{
    Fixture f;
    f.seq.submit ("p1", { Op::set ("t1", { { "vol", 0.9 } }) }, 100.0);

    // A guest's change to the same property right after is dropped and reconciled...
    auto r = f.seq.submit ("g", { Op::set ("t1", { { "vol", 0.1 }, { "name", "Mine" } }) }, 101.0);
    CHECK (r.reconcile == std::vector<Reconcile> { { "t1", false } });
    REQUIRE (r.accepted.size() == 1);
    CHECK (r.accepted[0].props == Json { { "name", "Mine" } }); // ...but other props still land
    CHECK (f.doc.find ("t1")->props["vol"] == 0.9);

    // After the window the guest's edit is fine.
    auto later = f.seq.submit ("g", { Op::set ("t1", { { "vol", 0.2 } }) }, 103.0);
    CHECK (later.reconcile.empty());
    CHECK (f.doc.find ("t1")->props["vol"] == 0.2);
}

TEST_CASE ("Player 1 always overrides a guest", "[sequencer]")
{
    Fixture f;
    f.seq.submit ("g", { Op::set ("t1", { { "vol", 0.3 } }) }, 50.0);
    auto r = f.seq.submit ("p1", { Op::set ("t1", { { "vol", 0.6 } }) }, 50.1);
    CHECK (r.reconcile.empty());
    CHECK (f.doc.find ("t1")->props["vol"] == 0.6);
}

TEST_CASE ("A guest can't delete what Player 1 is working inside", "[sequencer]")
{
    Fixture f;
    f.seq.submit ("p1", { Op::set ("n1", { { "p", 62 } }) }, 200.0);

    auto r = f.seq.submit ("g", { Op::remove ("c1") }, 200.5);
    CHECK (r.accepted.empty());
    CHECK (r.reconcile == std::vector<Reconcile> { { "c1", true } });
    CHECK (f.doc.has ("n1"));

    auto later = f.seq.submit ("g", { Op::remove ("c1") }, 205.0);
    CHECK (later.accepted.size() == 1);
    CHECK_FALSE (f.doc.has ("c1"));
    CHECK_FALSE (f.doc.has ("n1"));
}

TEST_CASE ("Creates under missing parents are rejected; resent creates become sets", "[sequencer]")
{
    Fixture f;
    auto r = f.seq.submit ("g", { Op::create ("n9", "note", "gone") }, 1.0);
    CHECK (r.reconcile == std::vector<Reconcile> { { "n9", false } });

    auto again = f.seq.submit ("g", { Op::create ("t1", "track", "song", { { "name", "X" } }) }, 10.0);
    REQUIRE (again.accepted.size() == 1);
    CHECK (again.accepted[0].kind == Op::Kind::set);
    CHECK (f.doc.find ("t1")->props["name"] == "X");

    // Removing something already gone is quietly ignored.
    auto gone = f.seq.submit ("g", { Op::remove ("nothing") }, 11.0);
    CHECK (gone.accepted.empty());
    CHECK (gone.reconcile.empty());
}
