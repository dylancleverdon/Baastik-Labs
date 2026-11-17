#include "tutti/Replica.h"

#include <algorithm>

namespace tutti
{
    Json LogEntry::toJson() const
    {
        return Json { { "seq", seq }, { "by", author }, { "cseq", cseq }, { "ops", tutti::toJson (ops) } };
    }

    std::optional<LogEntry> LogEntry::fromJson (const Json& j)
    {
        auto ops = opsFromJson (field (j, "ops"));
        const auto seq = uinteger (j, "seq");
        if (! ops || seq == 0)
            return std::nullopt;
        return LogEntry { seq, str (j, "by"), uinteger (j, "cseq"), std::move (*ops) };
    }

    Replica::Replica (std::string selfPeerId) : selfId (std::move (selfPeerId)) {}

    void Replica::reset (Doc doc, uint64_t seq)
    {
        confirmedDoc = std::move (doc);
        lastSeq = seq;
        pendingList.clear();
        logList.clear();
    }

    Replica::Batch Replica::local (Ops ops)
    {
        Batch b { nextCseq++, std::move (ops) };
        pendingList.push_back (b);
        return b;
    }

    void Replica::applyConfirmed (const Ops& ops)
    {
        for (const auto& op : ops)
            apply (confirmedDoc, op);
    }

    bool Replica::pendingKey (const std::string& id, const std::string& key) const
    {
        for (const auto& b : pendingList)
            for (const auto& op : b.ops)
                if (op.id == id && op.kind != Op::Kind::remove && op.props.contains (key))
                    return true;
        return false;
    }

    bool Replica::pendingRemove (const std::string& id) const
    {
        for (const auto& b : pendingList)
            for (const auto& op : b.ops)
                if (op.kind == Op::Kind::remove && op.id == id)
                    return true;
        return false;
    }

    Doc Replica::optimistic() const
    {
        Doc doc = confirmedDoc;
        for (const auto& b : pendingList)
            for (const auto& op : b.ops)
                apply (doc, op);
        return doc;
    }

    Json Replica::reconcileFor (const std::vector<Reconcile>& items) const
    {
        Json out = Json::array();
        if (items.empty())
            return out;

        const Doc doc = optimistic();
        for (const auto& r : items)
        {
            Json state = nullptr;
            if (doc.has (r.id))
            {
                state = Json::array();
                if (r.deep)
                    for (const auto& id : doc.subtree (r.id))
                        state.push_back (doc.entityJson (id));
                else
                    state.push_back (doc.entityJson (r.id));
            }
            out.push_back (Json { { "id", r.id }, { "deep", r.deep }, { "state", std::move (state) } });
        }
        return out;
    }

    LiveUpdate Replica::ack (uint64_t cseq, uint64_t seq, const Ops& accepted, const std::vector<Reconcile>& reconcile)
    {
        LiveUpdate update;

        pendingList.erase (std::remove_if (pendingList.begin(), pendingList.end(),
                                           [cseq] (const Batch& b) { return b.cseq == cseq; }),
                           pendingList.end());

        if (seq != 0)
        {
            if (seq <= lastSeq)
            {
                // Already seen through a catch-up: nothing to apply.
            }
            else if (seq != lastSeq + 1)
            {
                update.needResync = true;
            }
            else
            {
                applyConfirmed (accepted);
                lastSeq = seq;
                logList.push_back ({ seq, selfId, cseq, accepted });
            }
        }

        update.reconcile = reconcileFor (reconcile);
        return update;
    }

    LiveUpdate Replica::remote (const LogEntry& entry)
    {
        LiveUpdate update;
        if (entry.seq <= lastSeq)
            return update; // duplicate
        if (entry.seq != lastSeq + 1)
        {
            update.needResync = true;
            return update;
        }

        if (entry.author == selfId)
        {
            // One of our own batches, learned from a catch-up rather than an ack: Live has it already.
            pendingList.erase (std::remove_if (pendingList.begin(), pendingList.end(),
                                               [&] (const Batch& b) { return b.cseq == entry.cseq; }),
                               pendingList.end());
            applyConfirmed (entry.ops);
        }
        else
        {
            for (const auto& op : entry.ops)
            {
                apply (confirmedDoc, op);

                if (op.kind == Op::Kind::remove)
                {
                    update.apply.push_back (op);
                    continue;
                }
                if (pendingRemove (op.id))
                    continue; // we deleted it locally; our removal is still on its way

                if (op.kind == Op::Kind::create)
                {
                    update.apply.push_back (op);
                    continue;
                }

                Json keep = Json::object();
                for (auto it = op.props.begin(); it != op.props.end(); ++it)
                    if (! pendingKey (op.id, it.key()))
                        keep[it.key()] = it.value();
                if (! keep.empty())
                    update.apply.push_back (Op::set (op.id, std::move (keep)));
            }
        }

        lastSeq = entry.seq;
        logList.push_back (entry);
        return update;
    }

    std::vector<LogEntry> Replica::logSince (uint64_t seq) const
    {
        std::vector<LogEntry> out;
        for (const auto& e : logList)
            if (e.seq > seq)
                out.push_back (e);
        return out;
    }
}
