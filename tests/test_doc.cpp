#include "tutti/Ops.h"

#include <catch2/catch_test_macros.hpp>

using namespace tutti;

TEST_CASE ("Doc builds a tree and removes subtrees", "[doc]")
{
    Doc doc;
    REQUIRE (doc.has ("song"));
    REQUIRE (doc.create ("t1", "track", "song", { { "name", "Drums" }, { "pos", 1.0 } }));
    REQUIRE (doc.create ("c1", "clip", "t1", { { "start", 0.0 } }));
    REQUIRE (doc.create ("n1", "note", "c1", { { "p", 36 } }));
    REQUIRE (doc.create ("n2", "note", "c1", { { "p", 38 } }));

    CHECK_FALSE (doc.create ("t1", "track", "song", {}));   // duplicate id
    CHECK_FALSE (doc.create ("x", "note", "missing", {}));  // missing parent
    CHECK (doc.ancestors ("n1") == std::vector<std::string> { "c1", "t1", "song" });
    CHECK (doc.subtree ("t1") == std::vector<std::string> { "t1", "c1", "n1", "n2" });

    const auto removed = doc.remove ("c1");
    CHECK (removed == std::vector<std::string> { "c1", "n1", "n2" });
    CHECK_FALSE (doc.has ("n1"));
    CHECK (doc.children ("t1").empty());
    CHECK (doc.remove ("song").empty());
}

TEST_CASE ("Doc set merges props and null deletes", "[doc]")
{
    Doc doc;
    doc.create ("t1", "track", "song", { { "name", "Bass" }, { "vol", 0.8 } });
    REQUIRE (doc.set ("t1", { { "vol", 0.5 }, { "name", nullptr }, { "mute", true } }));
    const auto* t = doc.find ("t1");
    CHECK (t->props == Json { { "vol", 0.5 }, { "mute", true } });
    CHECK_FALSE (doc.set ("nope", { { "a", 1 } }));
}

TEST_CASE ("Doc orders children by pos, ties by id", "[doc]")
{
    Doc doc;
    doc.create ("b", "track", "song", { { "pos", 2.0 } });
    doc.create ("a", "track", "song", { { "pos", 2.0 } });
    doc.create ("c", "track", "song", { { "pos", 1.0 } });
    doc.create ("s", "scene", "song", { { "pos", 0.0 } });
    CHECK (doc.orderedChildren ("song", "track") == std::vector<std::string> { "c", "a", "b" });
}

TEST_CASE ("Doc round-trips through JSON", "[doc]")
{
    Doc doc;
    doc.set ("song", { { "tempo", 124.0 } });
    doc.create ("t1", "track", "song", { { "name", "Keys" } });
    doc.create ("c1", "clip", "t1", { { "len", 4.0 } });
    doc.create ("n1", "note", "c1", { { "p", 60 }, { "t", 0.5 } });

    auto copy = Doc::fromJson (doc.toJson());
    REQUIRE (copy);
    CHECK (*copy == doc);

    auto bad = Doc::fromJson (Json { { "e", Json::array ({ Json { { "id", "x" }, { "t", "note" }, { "p", "missing" } } }) } });
    CHECK_FALSE (bad);
}

TEST_CASE ("Ops serialize and apply idempotently", "[ops]")
{
    Ops ops { Op::create ("t1", "track", "song", { { "name", "A" } }),
              Op::set ("t1", { { "name", "B" } }),
              Op::create ("c1", "clip", "t1"),
              Op::remove ("c1") };

    auto parsed = opsFromJson (toJson (ops));
    REQUIRE (parsed);
    CHECK (*parsed == ops);

    Doc doc;
    for (const auto& op : ops)
        CHECK (apply (doc, op));
    // Replaying everything leaves the same result.
    Doc again = doc;
    for (const auto& op : ops)
        apply (again, op);
    CHECK (again == doc);
    CHECK (doc.find ("t1")->props == Json { { "name", "B" } });

    CHECK_FALSE (apply (doc, Op::set ("missing", { { "a", 1 } })));
    CHECK_FALSE (Op::fromJson (Json { { "k", "c" }, { "id", "x" } })); // create without parent/type
    CHECK_FALSE (opsFromJson (Json::object()));
}
