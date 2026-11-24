// Engine: things that make a session smoother. Getting others' saves by itself, trusted friends
// and rejoining, continuing the last session, the plugin check, jumping to a friend, and the
// activity feed.
#include "Engine.h"

#include "tutti/Als.h"

namespace tutti
{
    // ================================================================== activity feed

    void Engine::addActivity (const std::string& text)
    {
        activityLog.emplace_front (juce::Time::getCurrentTime(), text);
        while (activityLog.size() > 20)
            activityLog.pop_back();
        log ("activity: " + text);
    }

    // Who joins, leaves, drops out and comes back (checked once a second).
    void Engine::trackMembers()
    {
        if (! sessionPtr->inSession())
        {
            knownMembers.clear();
            return;
        }
        std::map<std::string, std::string> now;
        for (const auto& m : sessionPtr->info().members)
            if (m.peer != self.peer)
                now[m.peer] = m.name;
        if (! knownMembers.empty() || sessionPtr->isPlayer1())
        {
            for (const auto& [peer, name] : now)
                if (knownMembers.count (peer) == 0)
                    addActivity (name + " joined");
            for (const auto& [peer, name] : knownMembers)
                if (now.count (peer) == 0)
                    addActivity (name + " left");
        }
        knownMembers = now;
    }

    // ================================================================== getting others' saves

    void Engine::setAutoGet (bool on)
    {
        autoGet = on;
        saveSettings();
    }

    void Engine::autoGetIfDue()
    {
        // Someone saved: load the new version by itself once that's safe. If you have plugin
        // changes that aren't shared yet, Tutti saves those first (autoSaveIfDue), which brings
        // in their changes too, so nothing is lost. Without anything of yours waiting, it just
        // loads the new version. (Only saving when there's something of yours to share keeps
        // two computers from triggering each other's saves forever.)
        if (! autoGet || ! sessionPtr->inSession() || ! liveBound || reloading || ! pendingReload.is_null()
            || latestSnapshot.empty() || latestSnapshot == currentSnapshot || ! pendingSave.is_null())
            return;
        if (! mySaveNeeded.empty() || ! myPluginChanges.empty())
            return; // the automatic save (or the person) shares theirs first
        const double t = now();
        if (t - updateSeenAt < 3.0 || liveIsBusy())
            return;
        if (editedSinceSave)
        {
            // You've changed things since your last save (maybe automation, which Tutti can't
            // see): save yours first, which combines both, rather than replacing your work.
            if (! options.pressSaveInLive || ! autoSave || t - lastAutoSaveTry < 5.0)
                return; // without that, the person chooses (the update card)
            lastAutoSaveTry = t;
            if (options.pressSaveInLive (false) == saveKeyPressed)
            {
                log ("saved to combine my changes with the new version");
                updateSeenAt = t + 30.0;
            }
            return;
        }
        updateSeenAt = t + 30.0; // don't retry this every second if loading fails
        log ("getting the latest version by itself");
        updateNow();
    }

    // ================================================================== trusted friends, rejoining

    void Engine::trustAndLetIn (const std::string& peer)
    {
        trustedPeers[peer] = memberName (peer);
        saveSettings();
        approveJoin (peer, true);
        notice (trustedPeers[peer] + " can now join your sessions without asking.");
    }

    void Engine::forgetTrusted()
    {
        trustedPeers.clear();
        saveSettings();
    }

    void Engine::autoRejoinIfDue()
    {
        // Dropped out (Tutti or the computer restarted, the network went away): join again as
        // soon as the session shows up, the same session or the same host sharing the same song.
        if (! rejoinWanted || sessionPtr->inSession() || sessionPtr->role() == Session::Role::joining)
            return;
        const double t = now();
        if (t - lastRejoinTry < 10.0)
            return;
        for (const auto& [peer, hello] : peerHellos)
        {
            const auto& summary = field (hello, "session");
            if (! summary.is_object() || str (summary, "p1") != peer)
                continue;
            const bool same = str (summary, "id") == lastSessionId
                              || (peer == lastHost && ! lastSong.empty() && str (summary, "song") == lastSong);
            if (! same)
                continue;
            lastRejoinTry = t;
            log ("rejoining " + str (summary, "song") + " (" + str (hello, "name") + ")");
            notice ("Rejoining " + str (summary, "song") + "...");
            joinSession (peer);
            return;
        }
    }

    void Engine::continueLastSession()
    {
        if (sessionPtr->inSession() || ! lastSongFile.existsAsFile())
            return;
        juce::String error;
        if (! startSession (lastSongFile, error))
            notice (error.toStdString());
    }

    // ================================================================== plugin check

    void Engine::checkPlugins (const juce::XmlElement& set)
    {
        const auto& installed = field (liveHello, "plugin_formats");
        if (! installed.is_object() || installed.empty())
            return; // this Live didn't say what it has
        pluginProblems.clear();
        for (const auto& ref : als::pluginRefs (set))
        {
            const auto name = ref.name.toStdString();
            if (name == "Tutti Stand-in")
                continue;
            if (! installed.contains (name))
                pluginProblems.push_back (name + " isn't installed here");
            else
            {
                bool hasFormat = false;
                juce::StringArray have;
                for (const auto& f : installed[name])
                    if (f.is_string())
                    {
                        have.add (juce::String (f.get<std::string>()));
                        hasFormat = hasFormat || f.get<std::string>() == ref.format.toStdString();
                    }
                if (! hasFormat && ! have.contains ("?"))
                    pluginProblems.push_back (name + ": the song uses " + ref.format.toStdString() + ", you have "
                                              + have.joinIntoString (", ").toStdString()
                                              + (ref.format == "VST3" ? " (install the VST3 version and turn on VST3 in Live's Plug-Ins settings)" : ""));
            }
        }
        if (! pluginProblems.empty())
        {
            log ("plugin check: " + juce::StringArray ([this] {
                     juce::StringArray a;
                     for (const auto& p : pluginProblems)
                         a.add (juce::String (p));
                     return a;
                 }()).joinIntoString ("; ").toStdString());
            notice ("Some plugins in this song won't load here: see the Plugins card.");
        }
    }

    // ================================================================== jumping to a friend

    void Engine::jumpTo (const std::string& peer)
    {
        auto it = presence.find (peer);
        if (it == presence.end() || it->second.track.empty() || ! liveBound)
            return;
        sendToLive (Json { { "type", "goto" }, { "track", it->second.track }, { "time", it->second.time } });
        notice ("Jumped to " + memberName (peer) + "'s spot.");
    }
}
