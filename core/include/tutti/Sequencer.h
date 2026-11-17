#pragma once

#include "Ops.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace tutti
{
    // An entity the author must reset to the document's state. `deep` means its whole subtree
    // (a rejected removal: the author has to bring back everything inside it too).
    struct Reconcile
    {
        std::string id;
        bool deep = false;
        bool operator== (const Reconcile& o) const { return id == o.id && deep == o.deep; }
    };

    struct SubmitResult
    {
        uint64_t seq = 0; // global sequence number given to `accepted` (0 if nothing was accepted)
        Ops accepted;     // what was applied, in order (possibly a subset of what was sent)
        std::vector<Reconcile> reconcile;
    };

    // Runs on Player 1's computer. Validates everyone's edits against the document, applies them
    // in arrival order and gives each accepted batch the next sequence number.
    //
    // Player 1 wins: within `priorityWindow` seconds of Player 1 changing a property, a guest's
    // change to that property is dropped; and a guest can't remove something Player 1 changed
    // (itself or anything inside it) in that window. Otherwise the last writer wins.
    class Sequencer
    {
    public:
        explicit Sequencer (Doc& doc, uint64_t lastSeq = 0, double priorityWindowSeconds = 2.0);

        void setPlayer1 (std::string peerId) { p1 = std::move (peerId); }
        const std::string& player1() const { return p1; }

        SubmitResult submit (const std::string& author, const Ops& ops, double now);

        uint64_t lastSeq() const { return seq; }

    private:
        bool protectedProp (const std::string& id, const std::string& key, double now) const;
        bool protectedEntity (const std::string& id, double now) const;
        void markByPlayer1 (const std::string& id, const Json& keys, double now);

        Doc& doc;
        std::string p1;
        uint64_t seq;
        double window;
        std::unordered_map<std::string, double> propMarks;  // "id\x1fkey" -> when Player 1 last set it
        std::unordered_map<std::string, double> touchMarks; // id -> when Player 1 last changed it or a descendant
    };
}
