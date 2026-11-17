#include "tutti/AlsMerge.h"

#include <map>
#include <set>

namespace tutti::als
{
    namespace
    {
        juce::XmlElement* liveSet (juce::XmlElement& root) { return root.getChildByName ("LiveSet"); }

        int indexOf (juce::XmlElement& parent, const juce::XmlElement* child)
        {
            for (int i = 0; i < parent.getNumChildElements(); ++i)
                if (parent.getChildElement (i) == child)
                    return i;
            return -1;
        }

        juce::XmlElement* masterOf (juce::XmlElement& set)
        {
            if (auto* m = set.getChildByName ("MasterTrack"))
                return m;
            return set.getChildByName ("MainTrack");
        }

        bool isTrackTag (const juce::XmlElement& e)
        {
            return e.hasTagName ("MidiTrack") || e.hasTagName ("AudioTrack") || e.hasTagName ("GroupTrack")
                || e.hasTagName ("ReturnTrack");
        }

        // Personal or volatile bits that shouldn't count as a change to a track.
        bool isNoise (const juce::String& tag)
        {
            static const char* parts[] = { "View", "Selected", "Folded", "Collapsed", "LomId", "Pointee", "SavedPlaying",
                                           "IsArmed", "Solo", "Monitoring", "Highlighted" };
            for (auto* p : parts)
                if (tag.contains (p))
                    return true;
            return false;
        }

        void strip (juce::XmlElement& e)
        {
            e.removeAttribute ("Id");
            for (int i = e.getNumChildElements(); --i >= 0;)
            {
                auto* c = e.getChildElement (i);
                if (isNoise (c->getTagName()))
                    e.removeChildElement (c, true);
                else
                    strip (*c);
            }
        }

        void forEach (juce::XmlElement& e, const std::function<void (juce::XmlElement&)>& fn)
        {
            fn (e);
            for (auto* c : e.getChildIterator())
                forEach (*c, fn);
        }

        int nextPointeeId (juce::XmlElement& set)
        {
            if (auto* n = set.getChildByName ("NextPointeeId"))
                return n->getIntAttribute ("Value");
            int maxId = 0;
            forEach (set, [&] (juce::XmlElement& e) {
                if (e.getTagName().endsWith ("Target") && e.hasAttribute ("Id"))
                    maxId = std::max (maxId, e.getIntAttribute ("Id"));
            });
            return maxId + 1;
        }

        void setNextPointeeId (juce::XmlElement& set, int value)
        {
            auto* n = set.getChildByName ("NextPointeeId");
            if (n == nullptr)
                n = set.createNewChildElement ("NextPointeeId");
            n->setAttribute ("Value", value);
        }

        // Paths in a structure map under a track ("T3/D0" for "T3"), re-rooted at a new path.
        void copySubMap (const Json& from, const std::string& oldPath, const std::string& newPath, Json& to)
        {
            if (! from.is_object())
                return;
            for (auto it = from.begin(); it != from.end(); ++it)
            {
                const auto& key = it.key();
                if (key == oldPath)
                    to[newPath] = it.value();
                else if (key.rfind (oldPath + "/", 0) == 0)
                    to[newPath + key.substr (oldPath.size())] = it.value();
            }
        }

        std::vector<std::string> clipIdsUnder (const Json& map, const std::string& path)
        {
            std::vector<std::string> out;
            if (map.is_object())
                for (auto it = map.begin(); it != map.end(); ++it)
                    if (it.key().rfind (path + "/", 0) == 0 && it.value().is_string()
                        && (it.key().find ("/A") != std::string::npos || it.key().find ("/L") != std::string::npos))
                        out.push_back (it.value().get<std::string>());
            return out;
        }
    }

    std::vector<TrackUnit> mappedTracks (juce::XmlElement& root, const Json& map)
    {
        std::vector<TrackUnit> out;
        auto* set = liveSet (root);
        if (set == nullptr)
            return out;
        int t = 0, r = 0;
        if (auto* tracks = set->getChildByName ("Tracks"))
            for (auto* e : tracks->getChildIterator())
            {
                if (! isTrackTag (*e))
                    continue;
                const bool isReturn = e->hasTagName ("ReturnTrack");
                const auto path = (isReturn ? "R" : "T") + std::to_string (isReturn ? r++ : t++);
                out.push_back ({ str (map, path.c_str()), path, e });
            }
        if (auto* m = masterOf (*set))
            out.push_back ({ str (map, "M"), "M", m });
        return out;
    }

    juce::String canonical (const juce::XmlElement& track)
    {
        juce::XmlElement copy (track);
        strip (copy);
        return copy.toString (juce::XmlElement::TextFormat().withoutHeader().singleLine());
    }

    std::unique_ptr<juce::XmlElement> importTrack (juce::XmlElement& root, const juce::XmlElement& track)
    {
        auto copy = std::make_unique<juce::XmlElement> (track);
        auto* set = liveSet (root);
        if (set == nullptr)
            return copy;

        // Fresh automation/modulation target ids, and the pointers to them inside the track.
        int next = nextPointeeId (*set);
        std::map<juce::String, int> renumbered;
        forEach (*copy, [&] (juce::XmlElement& e) {
            if (e.getTagName().endsWith ("Target") && e.hasAttribute ("Id"))
            {
                renumbered[e.getStringAttribute ("Id")] = next;
                e.setAttribute ("Id", next++);
            }
        });
        forEach (*copy, [&] (juce::XmlElement& e) {
            if (e.hasTagName ("PointeeId"))
                if (auto it = renumbered.find (e.getStringAttribute ("Value")); it != renumbered.end())
                    e.setAttribute ("Value", it->second);
        });
        setNextPointeeId (*set, next);

        // The track's own id must not clash with another track's.
        std::set<int> trackIds;
        if (auto* tracks = set->getChildByName ("Tracks"))
            for (auto* e : tracks->getChildIterator())
                trackIds.insert (e->getIntAttribute ("Id"));
        if (trackIds.count (copy->getIntAttribute ("Id")) != 0)
            copy->setAttribute ("Id", (trackIds.empty() ? 0 : *trackIds.rbegin()) + 1);
        return copy;
    }

    MergeResult merge (const juce::XmlElement& baseIn, const Json& baseMap,
                       const juce::XmlElement& oursIn, const Json& oursMap, const Json& oursNotes,
                       const juce::XmlElement& theirsIn, const Json& theirsMap, const Json& theirsNotes,
                       bool theirsWins)
    {
        MergeResult result;
        juce::XmlElement base (baseIn), theirs (theirsIn);
        result.set = std::make_unique<juce::XmlElement> (oursIn);

        auto* outSet = liveSet (*result.set);
        auto* outTracks = outSet != nullptr ? outSet->getChildByName ("Tracks") : nullptr;
        if (outTracks == nullptr || liveSet (theirs) == nullptr)
        {
            result.set.reset();
            result.error = "not a Live Set";
            return result;
        }

        std::map<std::string, juce::String> baseCanon;
        for (const auto& u : mappedTracks (base, baseMap))
            if (! u.id.empty())
                baseCanon[u.id] = canonical (*u.element);

        auto oursUnits = mappedTracks (*result.set, oursMap);
        std::map<std::string, juce::XmlElement*> oursById;
        for (const auto& u : oursUnits)
            if (! u.id.empty())
                oursById[u.id] = u.element;

        auto theirsUnits = mappedTracks (theirs, theirsMap);
        std::set<std::string> theirsIds;
        for (const auto& u : theirsUnits)
            if (! u.id.empty())
                theirsIds.insert (u.id);

        std::set<std::string> takenFromTheirs;

        // Tracks the new save changed or added.
        juce::XmlElement* previousInOutput = nullptr;
        for (const auto& u : theirsUnits)
        {
            if (u.id.empty())
                continue;
            const auto f = canonical (*u.element);
            auto b = baseCanon.find (u.id);
            auto s = oursById.find (u.id);
            const bool isMaster = u.path == "M";

            if (s == oursById.end())
            {
                if (b == baseCanon.end() || f != b->second)
                {
                    // New in theirs, or changed in theirs after ours deleted it.
                    if (b != baseCanon.end())
                    {
                        result.conflicts.push_back (u.id);
                        if (! theirsWins)
                            continue;
                    }
                    auto copy = importTrack (*result.set, *u.element);
                    auto* raw = copy.release();
                    if (previousInOutput != nullptr)
                        outTracks->insertChildElement (raw, indexOf (*outTracks, previousInOutput) + 1);
                    else
                        outTracks->insertChildElement (raw, 0);
                    previousInOutput = raw;
                    takenFromTheirs.insert (u.id);
                }
                continue;
            }

            const auto sc = canonical (*s->second);
            bool take = false;
            if (b == baseCanon.end() || f == b->second || f == sc)
                take = false;
            else if (sc == b->second)
                take = true;
            else
            {
                result.conflicts.push_back (u.id);
                take = theirsWins;
            }

            if (take)
            {
                auto copy = importTrack (*result.set, *u.element);
                auto* parent = isMaster ? outSet : outTracks;
                const int index = indexOf (*parent, s->second);
                parent->removeChildElement (s->second, true);
                auto* raw = copy.release();
                parent->insertChildElement (raw, index);
                s->second = raw;
                takenFromTheirs.insert (u.id);
            }
            if (! isMaster)
                previousInOutput = s->second;
        }

        // Tracks the new save deleted (and ours didn't change since the base).
        for (const auto& [id, canon] : baseCanon)
        {
            if (theirsIds.count (id) != 0)
                continue;
            auto s = oursById.find (id);
            if (s == oursById.end() || s->second == nullptr || s->second->hasTagName ("MasterTrack") || s->second->hasTagName ("MainTrack"))
                continue;
            if (canonical (*s->second) != canon)
            {
                result.conflicts.push_back (id);
                if (! theirsWins)
                    continue;
            }
            outTracks->removeChildElement (s->second, true);
            s->second = nullptr;
        }

        // The merged structure map and note orders, from whichever side each track came from.
        auto theirsPath = [&] (const std::string& id) {
            for (const auto& u : theirsUnits)
                if (u.id == id)
                    return u.path;
            return std::string();
        };
        auto oursPath = [&] (const std::string& id) {
            for (const auto& u : oursUnits)
                if (u.id == id)
                    return u.path;
            return std::string();
        };
        result.notes = oursNotes.is_object() ? oursNotes : Json::object();
        // Rebuild in output order: walk the output tracks and look each one up.
        std::map<juce::XmlElement*, std::pair<std::string, bool>> origin; // element -> (id, fromTheirs)
        for (const auto& [id, element] : oursById)
            if (element != nullptr)
                origin[element] = { id, takenFromTheirs.count (id) != 0 };
        for (auto* e : outTracks->getChildIterator())
            if (origin.count (e) == 0)
            {
                // Inserted from theirs: identify by canonical form.
                const auto c = canonical (*e);
                for (const auto& u : theirsUnits)
                    if (takenFromTheirs.count (u.id) != 0 && oursById.count (u.id) == 0 && canonical (*u.element) == c)
                        origin[e] = { u.id, true };
            }
        if (auto* m = masterOf (*outSet); m != nullptr && origin.count (m) == 0)
            origin[m] = { str (oursMap, "M"), false };

        for (const auto& u : mappedTracks (*result.set, Json::object()))
        {
            auto it = origin.find (u.element);
            if (it == origin.end() || it->second.first.empty())
                continue;
            const auto& [id, fromTheirs] = it->second;
            const auto source = fromTheirs ? theirsPath (id) : oursPath (id);
            copySubMap (fromTheirs ? theirsMap : oursMap, source, u.path, result.map);
            if (fromTheirs)
                for (const auto& clip : clipIdsUnder (theirsMap, source))
                    if (theirsNotes.is_object() && theirsNotes.contains (clip))
                        result.notes[clip] = theirsNotes[clip];
        }
        // Scenes and locators: from ours.
        if (oursMap.is_object())
            for (auto it = oursMap.begin(); it != oursMap.end(); ++it)
                if (it.key()[0] == 'S' || it.key()[0] == 'Q')
                    result.map[it.key()] = it.value();

        result.fromTheirs.assign (takenFromTheirs.begin(), takenFromTheirs.end());
        return result;
    }

    void keepPersonalState (juce::XmlElement& snapshot, const Json&, juce::XmlElement& mine, const Json&)
    {
        auto* to = liveSet (snapshot);
        auto* from = liveSet (mine);
        if (to == nullptr || from == nullptr)
            return;
        // Transport (loop region, playhead, punch, metronome) and the Set's view are personal.
        for (int i = 0; i < from->getNumChildElements(); ++i)
        {
            auto* e = from->getChildElement (i);
            const auto tag = e->getTagName();
            if (tag != "Transport" && ! tag.contains ("View"))
                continue;
            if (auto* existing = to->getChildByName (tag))
                to->replaceChildElement (existing, new juce::XmlElement (*e));
        }
    }
}
