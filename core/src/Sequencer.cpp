#include "tutti/Sequencer.h"

#include <algorithm>

namespace tutti
{
    namespace
    {
        std::string propKey (const std::string& id, const std::string& key) { return id + '\x1f' + key; }

        void addReconcile (std::vector<Reconcile>& list, const std::string& id, bool deep)
        {
            for (auto& r : list)
                if (r.id == id)
                {
                    r.deep = r.deep || deep;
                    return;
                }
            list.push_back ({ id, deep });
        }
    }

    Sequencer::Sequencer (Doc& d, uint64_t lastSeq, double priorityWindowSeconds)
        : doc (d), seq (lastSeq), window (priorityWindowSeconds)
    {
    }

    bool Sequencer::protectedProp (const std::string& id, const std::string& key, double now) const
    {
        auto it = propMarks.find (propKey (id, key));
        return it != propMarks.end() && now - it->second < window;
    }

    bool Sequencer::protectedEntity (const std::string& id, double now) const
    {
        auto it = touchMarks.find (id);
        return it != touchMarks.end() && now - it->second < window;
    }

    void Sequencer::markByPlayer1 (const std::string& id, const Json& keys, double now)
    {
        if (keys.is_object())
            for (auto it = keys.begin(); it != keys.end(); ++it)
                propMarks[propKey (id, it.key())] = now;

        touchMarks[id] = now;
        for (const auto& a : doc.ancestors (id))
            touchMarks[a] = now;
    }

    SubmitResult Sequencer::submit (const std::string& author, const Ops& ops, double now)
    {
        SubmitResult result;
        const bool isP1 = ! p1.empty() && author == p1;

        for (const auto& op : ops)
        {
            switch (op.kind)
            {
                case Op::Kind::create:
                {
                    if (! doc.has (op.parent))
                    {
                        addReconcile (result.reconcile, op.id, false);
                        break;
                    }
                    if (doc.has (op.id))
                    {
                        // A resent create: treat it as setting those props.
                        Json keep = Json::object();
                        for (auto it = op.props.begin(); it != op.props.end(); ++it)
                        {
                            if (! isP1 && protectedProp (op.id, it.key(), now))
                                addReconcile (result.reconcile, op.id, false);
                            else
                                keep[it.key()] = it.value();
                        }
                        if (! keep.empty())
                        {
                            doc.set (op.id, keep);
                            if (isP1)
                                markByPlayer1 (op.id, keep, now);
                            result.accepted.push_back (Op::set (op.id, keep));
                        }
                        break;
                    }
                    doc.create (op.id, op.type, op.parent, op.props);
                    if (isP1)
                        markByPlayer1 (op.id, op.props, now);
                    result.accepted.push_back (op);
                    break;
                }

                case Op::Kind::remove:
                {
                    if (! doc.has (op.id))
                        break; // already gone: nothing to do
                    if (! isP1)
                    {
                        bool blocked = protectedEntity (op.id, now);
                        if (! blocked)
                            for (const auto& d : doc.subtree (op.id))
                                if (protectedEntity (d, now))
                                {
                                    blocked = true;
                                    break;
                                }
                        if (blocked)
                        {
                            addReconcile (result.reconcile, op.id, true);
                            break;
                        }
                    }
                    doc.remove (op.id);
                    result.accepted.push_back (op);
                    break;
                }

                case Op::Kind::set:
                {
                    if (! doc.has (op.id))
                    {
                        addReconcile (result.reconcile, op.id, false);
                        break;
                    }
                    Json keep = Json::object();
                    for (auto it = op.props.begin(); it != op.props.end(); ++it)
                    {
                        if (! isP1 && protectedProp (op.id, it.key(), now))
                            addReconcile (result.reconcile, op.id, false);
                        else
                            keep[it.key()] = it.value();
                    }
                    if (keep.empty())
                        break;
                    doc.set (op.id, keep);
                    if (isP1)
                        markByPlayer1 (op.id, keep, now);
                    result.accepted.push_back (Op::set (op.id, std::move (keep)));
                    break;
                }
            }
        }

        if (! result.accepted.empty())
            result.seq = ++seq;
        return result;
    }
}
