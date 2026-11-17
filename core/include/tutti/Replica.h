#pragma once

#include "Sequencer.h"

#include <deque>

namespace tutti
{
    // One accepted batch in the global order.
    struct LogEntry
    {
        uint64_t seq = 0;
        std::string author;
        uint64_t cseq = 0; // the author's own batch number
        Ops ops;

        Json toJson() const;
        static std::optional<LogEntry> fromJson (const Json&);
    };

    // What this computer's Live has to do after a message from Player 1.
    struct LiveUpdate
    {
        Ops apply;                      // others' edits, minus props we have newer pending edits for
        Json reconcile = Json::array(); // [{"id", "deep", "state": [entity...] | null}]
        bool needResync = false;        // we missed something: ask Player 1 for the log since seq()

        bool empty() const { return apply.empty() && reconcile.empty() && ! needResync; }
    };

    // This computer's copy of the document. Our own edits are applied to Live immediately and kept
    // as pending until Player 1 accepts or rejects them; everyone's accepted edits form the
    // confirmed document. Every member keeps the log since the last snapshot, so anyone can take
    // over as Player 1.
    class Replica
    {
    public:
        struct Batch
        {
            uint64_t cseq = 0;
            Ops ops;
        };

        explicit Replica (std::string selfPeerId);

        // Start from a snapshot (joining, an update, a restore). Clears pending edits and the log.
        void reset (Doc confirmedDoc, uint64_t seq);

        // Our own edit, already visible in our Live. Returns the batch to send to Player 1.
        Batch local (Ops ops);

        // Player 1's answer to one of our batches.
        LiveUpdate ack (uint64_t cseq, uint64_t seq, const Ops& accepted, const std::vector<Reconcile>& reconcile);

        // An accepted batch from the global order (someone else's, or ours seen in a catch-up).
        LiveUpdate remote (const LogEntry& entry);

        const Doc& confirmed() const { return confirmedDoc; }
        uint64_t seq() const { return lastSeq; }
        Doc optimistic() const;
        const std::deque<Batch>& pending() const { return pendingList; }
        const std::vector<LogEntry>& log() const { return logList; }
        std::vector<LogEntry> logSince (uint64_t seq) const;
        const std::string& self() const { return selfId; }

        // Reconcile payload for entities, read from the optimistic document.
        Json reconcileFor (const std::vector<Reconcile>&) const;

    private:
        bool pendingKey (const std::string& id, const std::string& key) const;
        bool pendingRemove (const std::string& id) const;
        void applyConfirmed (const Ops& ops);

        std::string selfId;
        Doc confirmedDoc;
        uint64_t lastSeq = 0;
        uint64_t nextCseq = 1;
        std::deque<Batch> pendingList;
        std::vector<LogEntry> logList;
    };
}
