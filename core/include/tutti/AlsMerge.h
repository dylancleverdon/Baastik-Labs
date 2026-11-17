#pragma once

#include "Als.h"
#include "Json.h"

// The save lane's merge: combines Live Sets at the level of whole tracks.
//
// Every snapshot comes with a structure map (from the Remote Script) that names each track:
// "T<i>" is the i-th regular track, "R<i>" the i-th return track, "M" the master; deeper paths
// ("T3/D0", "T3/A2") name devices and clips inside them. That lets us line up the same track
// across files saved on different computers.
namespace tutti::als
{
    struct TrackUnit
    {
        std::string id;
        std::string path;           // "T3", "R0", "M"
        juce::XmlElement* element = nullptr;
    };

    // The Set's tracks paired with the ids from its structure map.
    std::vector<TrackUnit> mappedTracks (juce::XmlElement& set, const Json& map);

    // A track's XML without the numbering Live is free to change between saves (element Ids,
    // pointee ids), so "the same track" compares equal across computers.
    juce::String canonical (const juce::XmlElement& track);

    struct MergeResult
    {
        std::unique_ptr<juce::XmlElement> set;
        Json map = Json::object();
        Json notes = Json::object();
        std::vector<std::string> fromTheirs;  // track ids taken from the new save
        std::vector<std::string> conflicts;   // changed on both sides
        std::string error;
        bool ok() const { return set != nullptr; }
    };

    // Three-way merge: `base` is what the saver started from, `ours` the current snapshot,
    // `theirs` the new save. Tracks changed only in theirs are taken from theirs; tracks changed on
    // both sides go to theirs if `theirsWins` (the saver is Player 1, or the current side's change
    // wasn't Player 1's), otherwise ours is kept. Everything outside the tracks comes from ours.
    MergeResult merge (const juce::XmlElement& base, const Json& baseMap,
                       const juce::XmlElement& ours, const Json& oursMap, const Json& oursNotes,
                       const juce::XmlElement& theirs, const Json& theirsMap, const Json& theirsNotes,
                       bool theirsWins);

    // Copies a track from another Set into this one, renumbering its automation/modulation
    // target ids (and the pointers to them) so they don't collide with ids already in `set`.
    std::unique_ptr<juce::XmlElement> importTrack (juce::XmlElement& set, const juce::XmlElement& track);

    // Carries this computer's personal state (solo, arm, monitoring, view...) from `mine` into
    // a snapshot before it's opened here, for tracks with the same id. Best effort.
    void keepPersonalState (juce::XmlElement& snapshot, const Json& snapshotMap, juce::XmlElement& mine, const Json& mineMap);
}
