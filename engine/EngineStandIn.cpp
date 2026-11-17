// Engine: the Tutti Stand-in plugin.
//
// Each Stand-in on this computer connects to us with its Link id. We find its device in the shared
// song (a plugin device named "Tutti Stand-in" whose second parameter is that id), look at the
// plugin right before it, and tell it to record (we own that plugin) or play (we don't). Recordings
// are shared as segments in the device's "bounce" prop: {file, start, len, tempo, fp, by, byName}.
// `fp` fingerprints the track's clips, notes and the devices before the Stand-in when it was
// recorded; when the track changes, the segment is played anyway but shown as out of date.
#include "Engine.h"

#include <juce_cryptography/juce_cryptography.h>

namespace tutti
{
    namespace
    {
        bool isStandIn (const Entity& e)
        {
            return e.type == "device" && juce::String (str (e.props, "plugin", str (e.props, "name"))).contains ("Tutti Stand-in");
        }
    }

    const std::string* Engine::standInDevice (const Doc& doc, double link) const
    {
        static std::string found;
        for (const auto& id : doc.subtree (Doc::rootId))
            if (auto* e = doc.find (id); e != nullptr && isStandIn (*e) && std::abs (number (e->props, "p1", -1.0) - link) < 2e-6)
            {
                found = id;
                return &found;
            }
        return nullptr;
    }

    std::string Engine::trackFingerprint (const Doc& doc, const std::string& trackId, double beforePos) const
    {
        std::vector<std::string> parts;
        for (const auto& id : doc.subtree (trackId))
        {
            auto* e = doc.find (id);
            if (e == nullptr || id == trackId)
                continue;
            if (e->type == "device" && (number (e->props, "pos") >= beforePos || isStandIn (*e)))
                continue;
            if (e->type == "device" || e->type == "clip" || e->type == "note" || e->type == "chain")
            {
                auto props = e->props;
                props.erase ("bounce");
                parts.push_back (e->type + ":" + props.dump());
            }
        }
        std::sort (parts.begin(), parts.end());
        juce::String all;
        for (const auto& p : parts)
            all << juce::String (p) << "\n";
        return juce::SHA256 (all.toUTF8()).toHexString().substring (0, 16).toStdString();
    }

    void Engine::handleStandIn (uint64_t linkId, const Json& m)
    {
        const auto type = str (m, "type");
        if (type == "hello")
        {
            standIns[linkId].link = number (m, "link", -1.0);
            standIns[linkId].sent.clear();
            updateStandIns();
            return;
        }
        if (type != "segment" || ! sessionPtr->inSession())
            return;

        const auto doc = sessionPtr->replica().optimistic();
        const auto* device = standInDevice (doc, number (m, "link", -1.0));
        if (device == nullptr)
            return;
        const std::string deviceId = *device;
        const juce::File file (juce::String (str (m, "path")));
        const double start = number (m, "start"), len = number (m, "len"), tempo = number (m, "tempo", 120.0);
        juce::Thread::launch ([this, file, deviceId, start, len, tempo] {
            juce::String error;
            auto hash = contentStore.add (file, error);
            if (hash)
                audio::ensurePreview (contentStore, *hash, error);
            file.deleteFile();
            const auto h = hash.value_or (std::string());
            post ([this, h, deviceId, start, len, tempo] {
                if (h.empty() || ! sessionPtr->inSession())
                    return;
                const auto doc = sessionPtr->replica().optimistic();
                auto* device = doc.find (deviceId);
                if (device == nullptr)
                    return;
                // Replace whatever this take overlaps; keep the rest, sorted by position.
                Json bounce = Json::array();
                if (const auto& old = field (device->props, "bounce"); old.is_array())
                    for (const auto& seg : old)
                    {
                        const double s = number (seg, "start"), e = s + number (seg, "len");
                        if (e <= start || s >= start + len)
                            bounce.push_back (seg);
                    }
                bounce.push_back (Json { { "file", h }, { "start", start }, { "len", len }, { "tempo", tempo },
                                         { "fp", trackFingerprint (doc, device->parent, number (device->props, "pos")) },
                                         { "by", self.peer }, { "byName", self.name } });
                std::vector<Json> sorted (bounce.begin(), bounce.end());
                std::sort (sorted.begin(), sorted.end(), [] (const Json& a, const Json& b) { return number (a, "start") < number (b, "start"); });
                sessionPtr->submitLocal ({ Op::set (deviceId, { { "bounce", Json (sorted) } }) }, now());
                updateStandIns();
            });
        });
    }

    void Engine::updateStandIns()
    {
        if (standIns.empty())
            return;
        const auto doc = sessionPtr->replica().optimistic();
        std::set<std::string> owned;
        if (const auto& plugins = field (liveHello, "plugins"); plugins.is_array())
            for (const auto& p : plugins)
                if (p.is_string())
                    owned.insert (p.get<std::string>());

        for (auto& [linkId, standIn] : standIns)
        {
            Json message { { "type", "standin" }, { "mode", "idle" }, { "segments", Json::array() }, { "who", "" } };
            if (sessionPtr->inSession())
                if (const auto* device = standInDevice (doc, standIn.link))
                {
                    const auto* entity = doc.find (*device);
                    const auto devices = doc.orderedChildren (entity->parent, "device");
                    const Entity* before = nullptr;
                    for (const auto& d : devices)
                    {
                        if (d == *device)
                            break;
                        before = doc.find (d);
                    }
                    const auto pluginName = before != nullptr ? str (before->props, "plugin") : std::string();
                    if (! pluginName.empty())
                    {
                        const bool mine = owned.count (pluginName) != 0;
                        message["mode"] = mine ? "capture" : "play";
                        if (! mine)
                        {
                            const auto fp = trackFingerprint (doc, entity->parent, number (entity->props, "pos"));
                            std::string who;
                            if (const auto& bounce = field (entity->props, "bounce"); bounce.is_array())
                                for (const auto& seg : bounce)
                                {
                                    const auto hash = str (seg, "file");
                                    wantFile (hash, true);
                                    const auto local = contentStore.bestFor (hash);
                                    if (local == juce::File())
                                        continue;
                                    message["segments"].push_back (Json { { "path", local.getFullPathName().toStdString() },
                                                                          { "start", number (seg, "start") }, { "len", number (seg, "len") },
                                                                          { "tempo", number (seg, "tempo", 120.0) },
                                                                          { "stale", str (seg, "fp") != fp } });
                                    who = str (seg, "byName");
                                }
                            message["who"] = who;
                        }
                    }
                }
            const auto text = message.dump();
            if (text != standIn.sent)
            {
                standIn.sent = text;
                sendTo (linkId, message);
            }
        }
    }
}
