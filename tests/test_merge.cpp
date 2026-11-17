#include "tutti/AlsMerge.h"

#include <catch2/catch_test_macros.hpp>

using namespace tutti;

namespace
{
    juce::String track (const char* tag, int id, const char* name, int target, const char* events)
    {
        return juce::String ("<") + tag + " Id=\"" + juce::String (id) + "\"><Name><EffectiveName Value=\"" + name + "\"/></Name>"
             + "<LomId Value=\"" + juce::String (id * 7) + "\"/>"
             + "<AutomationEnvelopes><Envelopes><AutomationEnvelope Id=\"0\"><EnvelopeTarget><PointeeId Value=\""
             + juce::String (target) + "\"/></EnvelopeTarget><Automation><Events>" + events
             + "</Events></Automation></AutomationEnvelope></Envelopes></AutomationEnvelopes>"
             + "<DeviceChain><Mixer><Volume><AutomationTarget Id=\"" + juce::String (target) + "\"/></Volume></Mixer></DeviceChain>"
             + "</" + tag + ">";
    }

    std::unique_ptr<juce::XmlElement> set (const juce::String& tracks, int next = 300, const char* transport = "<Transport><LoopStart Value=\"0\"/></Transport>")
    {
        auto xml = juce::parseXML ("<Ableton Creator=\"Ableton Live 12.3.2\"><LiveSet><NextPointeeId Value=\"" + juce::String (next)
                                   + "\"/>" + transport + "<Tracks>" + tracks + "</Tracks>"
                                   + track ("MasterTrack", 1, "Main", 90, "") + "</LiveSet></Ableton>");
        REQUIRE (xml);
        return xml;
    }

    const char* ev (int n)
    {
        static juce::String s;
        s = "<FloatEvent Time=\"" + juce::String (n) + "\" Value=\"0.5\"/>";
        return s.toRawUTF8();
    }

    Json mapOf (std::initializer_list<std::pair<std::string, std::string>> items)
    {
        Json j = Json::object();
        for (const auto& [k, v] : items)
            j[k] = v;
        return j;
    }

    std::vector<std::string> names (juce::XmlElement& root)
    {
        std::vector<std::string> out;
        for (const auto& u : als::mappedTracks (root, Json::object()))
            out.push_back (u.element->getChildByName ("Name")->getChildByName ("EffectiveName")->getStringAttribute ("Value").toStdString());
        return out;
    }
}

TEST_CASE ("The same track compares equal whatever Live numbered it", "[merge]")
{
    auto a = juce::parseXML (track ("MidiTrack", 8, "Drums", 101, ev (1)));
    auto b = juce::parseXML (track ("MidiTrack", 23, "Drums", 555, ev (1)));
    auto c = juce::parseXML (track ("MidiTrack", 8, "Drums", 101, ev (2)));
    CHECK (als::canonical (*a) == als::canonical (*b));
    CHECK (als::canonical (*a) != als::canonical (*c));
}

TEST_CASE ("Imported tracks get fresh automation ids", "[merge]")
{
    auto target = set (track ("MidiTrack", 8, "Drums", 101, ""), 300);
    auto incoming = juce::parseXML (track ("MidiTrack", 8, "Keys", 101, ev (4)));
    auto imported = als::importTrack (*target, *incoming);
    CHECK (imported->getIntAttribute ("Id") != 8); // track id clash resolved
    auto* at = imported->getChildByName ("DeviceChain")->getChildByName ("Mixer")->getChildByName ("Volume")->getChildByName ("AutomationTarget");
    CHECK (at->getIntAttribute ("Id") == 300);
    auto* pointee = imported->getChildByName ("AutomationEnvelopes")->getChildByName ("Envelopes")->getChildByName ("AutomationEnvelope")
                        ->getChildByName ("EnvelopeTarget")->getChildByName ("PointeeId");
    CHECK (pointee->getIntAttribute ("Value") == 300);
    CHECK (target->getChildByName ("LiveSet")->getChildByName ("NextPointeeId")->getIntAttribute ("Value") == 301);
}

TEST_CASE ("Track-level three-way merge", "[merge]")
{
    const auto baseMap = mapOf ({ { "T0", "ea" }, { "T1", "eb" }, { "R0", "er" }, { "M", "em" } });
    auto base = set (track ("MidiTrack", 10, "Drums", 101, "") + track ("AudioTrack", 11, "Vox", 102, "")
                     + track ("ReturnTrack", 12, "Reverb", 103, ""));

    // Ours (the current snapshot) changed Vox's automation since the base.
    auto ours = set (track ("MidiTrack", 10, "Drums", 101, "") + track ("AudioTrack", 11, "Vox", 102, ev (8))
                     + track ("ReturnTrack", 12, "Reverb", 103, ""));
    // The new save changed Drums, added Keys after it, and was made on another computer (other ids).
    auto theirs = set (track ("MidiTrack", 40, "Drums", 201, ev (3)) + track ("MidiTrack", 41, "Keys", 202, ev (5))
                       + track ("AudioTrack", 42, "Vox", 203, "") + track ("ReturnTrack", 43, "Reverb", 204, ""),
                       500, "<Transport><LoopStart Value=\"16\"/></Transport>");
    const auto theirsMap = mapOf ({ { "T0", "ea" }, { "T1", "ek" }, { "T2", "eb" }, { "R0", "er" }, { "M", "em" },
                                    { "T1/A0", "clip-k" } });
    const Json theirsNotes { { "clip-k", Json::array ({ "n1", "n2" }) } };

    SECTION ("changes on different tracks combine")
    {
        auto r = als::merge (*base, baseMap, *ours, baseMap, Json::object(), *theirs, theirsMap, theirsNotes, false);
        REQUIRE (r.ok());
        CHECK (names (*r.set) == std::vector<std::string> { "Drums", "Keys", "Vox", "Reverb", "Main" });
        CHECK (r.conflicts.empty());
        CHECK (r.map["T0"] == "ea");
        CHECK (r.map["T1"] == "ek");
        CHECK (r.map["T2"] == "eb");
        CHECK (r.map["R0"] == "er");
        CHECK (r.map["T1/A0"] == "clip-k");
        CHECK (r.notes["clip-k"].size() == 2);
        // Drums came from theirs (its event is there), Vox kept ours (its event too).
        const auto merged = r.set->toString();
        CHECK (merged.contains ("Time=\"3\""));
        CHECK (merged.contains ("Time=\"8\""));
        // Settings outside the tracks (the transport here) come from ours.
        CHECK (r.set->getChildByName ("LiveSet")->getChildByName ("Transport")->getChildByName ("LoopStart")->getIntAttribute ("Value") == 0);
    }

    SECTION ("a clash keeps ours unless theirs wins")
    {
        auto clash = set (track ("MidiTrack", 10, "Drums", 101, "") + track ("AudioTrack", 11, "Vox", 102, ev (9))
                          + track ("ReturnTrack", 12, "Reverb", 103, ""));
        auto keep = als::merge (*base, baseMap, *ours, baseMap, Json::object(), *clash, baseMap, Json::object(), false);
        CHECK (keep.conflicts == std::vector<std::string> { "eb" });
        CHECK (keep.set->toString().contains ("Time=\"8\""));
        auto win = als::merge (*base, baseMap, *ours, baseMap, Json::object(), *clash, baseMap, Json::object(), true);
        CHECK (win.set->toString().contains ("Time=\"9\""));
        CHECK_FALSE (win.set->toString().contains ("Time=\"8\""));
    }

    SECTION ("a track deleted in the save is deleted, unless ours changed it")
    {
        auto deleted = set (track ("MidiTrack", 10, "Drums", 101, "") + track ("ReturnTrack", 12, "Reverb", 103, ""));
        const auto deletedMap = mapOf ({ { "T0", "ea" }, { "R0", "er" }, { "M", "em" } });
        auto r = als::merge (*base, baseMap, *base, baseMap, Json::object(), *deleted, deletedMap, Json::object(), false);
        CHECK (names (*r.set) == std::vector<std::string> { "Drums", "Reverb", "Main" });
        auto kept = als::merge (*base, baseMap, *ours, baseMap, Json::object(), *deleted, deletedMap, Json::object(), false);
        CHECK (names (*kept.set) == std::vector<std::string> { "Drums", "Vox", "Reverb", "Main" });
    }
}

TEST_CASE ("Personal state comes from this computer's own file", "[merge]")
{
    auto snapshot = set (track ("MidiTrack", 10, "Drums", 101, ""), 300, "<Transport><LoopStart Value=\"0\"/></Transport>");
    auto mine = set (track ("MidiTrack", 10, "Drums", 101, ""), 300, "<Transport><LoopStart Value=\"32\"/></Transport>");
    als::keepPersonalState (*snapshot, Json::object(), *mine, Json::object());
    CHECK (snapshot->getChildByName ("LiveSet")->getChildByName ("Transport")->getChildByName ("LoopStart")->getIntAttribute ("Value") == 32);
}
