// Engine: working side by side. The track lock, undoing your own changes, messages and pings,
// the plugin table, and keeping a complete copy after a session.
#include "Engine.h"

namespace tutti
{
    // The track an entity belongs to ("" for song-level things like tempo).
    std::string Engine::trackOf (const Doc& doc, const std::string& id) const
    {
        std::string cur = id;
        for (int depth = 0; depth < 64 && ! cur.empty(); ++depth)
        {
            const auto* e = doc.find (cur);
            if (e == nullptr)
                return {};
            if (e->type == "track")
                return cur;
            cur = e->parent;
        }
        return {};
    }

    // ================================================================== track lock

    void Engine::setLockTracks (bool on)
    {
        lockTracks = on;
        saveSettings();
        if (sessionPtr->inSession() && sessionPtr->isPlayer1())
        {
            sessionLock = on;
            sessionPtr->broadcast (Json { { "type", "lock", }, { "on", on } });
            addActivity (on ? "Track lock on: nobody edits a track someone else is on" : "Track lock off");
        }
    }

    bool Engine::blockedByLock (Ops& ops)
    {
        // Live can't stop a click, so with the lock on, an edit to a track someone else has
        // selected isn't shared: Live is put back the way it was and the person is told why.
        if (! sessionLock || ops.empty())
            return false;
        std::map<std::string, std::string> taken; // track -> who's on it
        for (const auto& m : sessionPtr->info().members)
            if (m.peer != self.peer && m.online)
                if (auto it = presence.find (m.peer); it != presence.end() && ! it->second.track.empty())
                    taken[it->second.track] = m.name;
        if (taken.empty())
            return false;

        const auto doc = sessionPtr->replica().optimistic();
        Ops keep;
        Json items = Json::array();
        std::string who, where;
        for (auto& op : ops)
        {
            const auto track = trackOf (doc, op.kind == Op::Kind::create ? op.parent : op.id);
            auto it = taken.find (op.kind == Op::Kind::create && op.type == "track" ? std::string() : track);
            if (track.empty() || it == taken.end())
            {
                keep.push_back (std::move (op));
                continue;
            }
            who = it->second;
            where = trackName (track);
            const bool deep = op.kind != Op::Kind::set;
            Json state = nullptr;
            if (doc.has (op.id))
            {
                state = Json::array();
                if (deep)
                    for (const auto& id : doc.subtree (op.id))
                        state.push_back (doc.entityJson (id));
                else
                    state.push_back (doc.entityJson (op.id));
            }
            items.push_back (Json { { "id", op.id }, { "deep", deep }, { "state", std::move (state) } });
        }
        if (items.empty())
            return false;
        ops = std::move (keep);
        sendToLive (Json { { "type", "reconcile" }, { "items", toLive (items) } });
        if (now() - lastLockNotice > 5.0)
        {
            lastLockNotice = now();
            notice (who + " is on " + (where.empty() ? std::string ("that track") : where) + ": your change there was undone.");
        }
        return true;
    }

    // ================================================================== undoing my own changes

    void Engine::recordUndo (const Ops& ops)
    {
        // The inverse of each edit, worked out before it's applied, newest last.
        const auto doc = sessionPtr->replica().optimistic();
        Ops inverse; // later edits first; within one edit, in order (parents before children)
        std::vector<std::pair<std::string, Json>> mine;
        for (const auto& op : ops)
        {
            Ops group;
            if (op.kind == Op::Kind::set)
            {
                const auto* e = doc.find (op.id);
                if (e == nullptr || ! op.props.is_object())
                    continue;
                Json before = Json::object();
                for (const auto& [key, value] : op.props.items())
                {
                    if (key == "path")
                        continue;
                    before[key] = e->props.contains (key) ? e->props[key] : Json (nullptr);
                }
                if (! before.empty())
                {
                    group.push_back (Op::set (op.id, before));
                    mine.emplace_back (op.id, op.props);
                }
            }
            else if (op.kind == Op::Kind::create)
                group.push_back (Op::remove (op.id));
            else if (doc.has (op.id))
            {
                // Removed: bring the whole thing back with the same ids, parents first.
                for (const auto& id : doc.subtree (op.id))
                    if (const auto* e = doc.find (id))
                        group.push_back (Op::create (id, e->type, e->parent, e->props));
            }
            inverse.insert (inverse.begin(), group.begin(), group.end());
        }
        if (inverse.empty())
            return;
        const double t = now();
        // Edits close together (a drag, a paste) undo together.
        if (! undoStack.empty() && t - undoStack.back().at < 1.5)
        {
            auto& step = undoStack.back();
            step.inverse.insert (step.inverse.begin(), inverse.begin(), inverse.end());
            step.mine.insert (step.mine.end(), mine.begin(), mine.end());
            step.at = t;
        }
        else
            undoStack.push_back ({ t, inverse, mine });
        while (undoStack.size() > 50)
            undoStack.pop_front();
    }

    void Engine::undoMine()
    {
        if (! sessionPtr->inSession() || ! liveBound || undoStack.empty())
            return;
        auto step = undoStack.back();
        undoStack.pop_back();
        const auto doc = sessionPtr->replica().optimistic();
        // Anything someone else changed since stays theirs.
        std::map<std::pair<std::string, std::string>, Json> lastSet; // what I set last, per property
        for (const auto& [id, props] : step.mine)
            for (const auto& [key, value] : props.items())
                if (key != "path")
                    lastSet[{ id, key }] = value;
        std::set<std::pair<std::string, std::string>> changedSince;
        for (const auto& [where, value] : lastSet)
            if (const auto* e = doc.find (where.first))
                if (! e->props.contains (where.second) || e->props[where.second] != value)
                    changedSince.insert (where);
        Ops run;
        int skipped = 0;
        for (auto op : step.inverse)
        {
            if (op.kind == Op::Kind::set)
            {
                if (! doc.has (op.id))
                    continue;
                Json keep = Json::object();
                for (const auto& [key, value] : op.props.items())
                    if (changedSince.count ({ op.id, key }) == 0)
                        keep[key] = value;
                    else
                        ++skipped;
                if (keep.empty())
                    continue;
                op.props = keep;
            }
            else if (op.kind == Op::Kind::remove && ! doc.has (op.id))
                continue;
            else if (op.kind == Op::Kind::create && (doc.has (op.id) || ! doc.has (op.parent)))
                continue;
            run.push_back (op);
        }
        if (run.empty())
        {
            notice (skipped > 0 ? "Nothing to undo: someone else has changed that since." : "Nothing to undo.");
            return;
        }
        log ("undoing my last change (" + std::to_string (run.size()) + " edits)");
        applyLocalAndSubmit (run);
        notice (skipped > 0 ? "Undid your last change (kept what others changed since)." : "Undid your last change.");
    }

    // ================================================================== messages and pings

    void Engine::addMessage (const std::string& who, const std::string& text, const std::string& track, double time)
    {
        messageLog.push_front ({ juce::Time::getCurrentTime(), who, text, track, time });
        while (messageLog.size() > 30)
            messageLog.pop_back();
        if (onChange)
            onChange();
    }

    void Engine::sendMessage (const std::string& text)
    {
        if (! sessionPtr->inSession() || text.empty())
            return;
        sessionPtr->broadcast (Json { { "type", "chat" }, { "text", text }, { "byName", self.name } });
        addMessage ("You", text, {}, 0);
    }

    void Engine::pingMySpot()
    {
        if (! sessionPtr->inSession())
            return;
        auto it = presence.find (self.peer);
        const std::string track = it != presence.end() ? it->second.track : std::string();
        const double time = it != presence.end() ? it->second.time : 0.0;
        const auto where = (trackName (track).empty() ? std::string ("here") : trackName (track))
                           + ", bar " + std::to_string (static_cast<int> (barOf (time)));
        sessionPtr->broadcast (Json { { "type", "ping" }, { "track", track }, { "time", time }, { "where", where },
                                      { "byName", self.name } });
        addMessage ("You", "Look at " + where, {}, 0);
    }

    void Engine::jumpToSpot (const std::string& track, double time)
    {
        if (! liveBound)
            return;
        sendToLive (Json { { "type", "goto" }, { "track", track }, { "time", time } });
    }

    // ================================================================== plugins in this song

    std::vector<std::string> Engine::buildPluginTable() const
    {
        std::vector<std::string> out;
        if (! sessionPtr->inSession() || ! currentSong.existsAsFile())
            return out;
        const auto stamp = currentSong.getLastModificationTime().toMilliseconds() ^ currentSong.getSize();
        if (stamp != pluginTableStamp)
        {
            pluginTableStamp = stamp;
            juce::String error;
            songPlugins.clear();
            if (auto set = als::load (currentSong, error))
                songPlugins = als::pluginRefs (*set);
        }
        // Who has what: this computer's Live, and everyone else's from their hello.
        std::vector<std::pair<std::string, Json>> people;
        people.emplace_back ("you", field (liveHello, "plugin_formats"));
        for (const auto& m : sessionPtr->info().members)
            if (m.peer != self.peer)
                if (auto it = peerHellos.find (m.peer); it != peerHellos.end())
                    people.emplace_back (m.name, field (it->second, "plugin_formats"));
        for (const auto& ref : songPlugins)
        {
            if (ref.name == "Tutti Stand-in")
                continue;
            const auto name = ref.name.toStdString();
            juce::StringArray have, lack;
            for (const auto& [who, formats] : people)
            {
                if (! formats.is_object() || formats.empty())
                    continue; // unknown (older Tutti)
                bool ok = false;
                if (formats.contains (name))
                    for (const auto& f : formats[name])
                        ok = ok || (f.is_string() && (f.get<std::string>() == ref.format.toStdString() || f.get<std::string>() == "?"));
                (ok ? have : lack).add (juce::String (who));
            }
            std::string line = name + " (" + ref.format.toStdString() + ")";
            if (! have.isEmpty())
                line += ": " + have.joinIntoString (", ").toStdString();
            if (! lack.isEmpty())
                line += (have.isEmpty() ? ": " : " | ") + std::string ("missing for ") + lack.joinIntoString (", ").toStdString();
            out.push_back (line);
        }
        return out;
    }

    // ================================================================== complete copy

    void Engine::saveCompleteCopy()
    {
        Json meta = copyOfferMeta;
        if (meta.is_null() && sessionPtr->inSession())
            if (auto m = loadSnapshot (latestSnapshot.empty() ? currentSnapshot : latestSnapshot))
                meta = *m;
        if (meta.is_null())
        {
            notice ("There's no saved version to copy yet.");
            return;
        }
        const auto song = juce::String (str (meta, "song", currentSongName.empty() ? "Song" : currentSongName));
        const auto stamp = juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H.%M");
        auto dir = options.songsDir.getChildFile ("Complete copies")
                       .getChildFile (juce::File::createLegalFileName (song + " " + stamp));
        writeCompleteCopy (meta, dir, juce::File::createLegalFileName (song), true);
        copyOfferMeta = nullptr;
    }
}
