#pragma once

#include "Json.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace tutti
{
    // One object in the shared song: a track, clip, note, device...
    struct Entity
    {
        std::string type;
        std::string parent;          // empty only for the root
        Json props = Json::object(); // synced properties (see docs/DESIGN.md for names)
    };

    // The shared document: a tree of entities rooted at "song".
    class Doc
    {
    public:
        static constexpr const char* rootId = "song";

        Doc();

        bool has (const std::string& id) const { return entities.count (id) != 0; }
        const Entity* find (const std::string& id) const;
        size_t size() const { return entities.size(); }

        // Fails (returns false) if the id exists or the parent doesn't.
        bool create (const std::string& id, const std::string& type, const std::string& parent, const Json& props = Json::object());

        // Removes the entity and everything inside it. Returns the removed ids, parents first
        // (empty if it didn't exist). The root can't be removed.
        std::vector<std::string> remove (const std::string& id);

        // Merges an object of props into the entity; a null value deletes that prop.
        bool set (const std::string& id, const Json& props);

        // Children in creation order.
        const std::vector<std::string>& children (const std::string& id) const;

        // Children of one type, ordered by their "pos" prop (ties by id).
        std::vector<std::string> orderedChildren (const std::string& id, const std::string& type) const;

        // Parent, grandparent... up to the root (nearest first).
        std::vector<std::string> ancestors (const std::string& id) const;

        // The entity and all its descendants, parents before children.
        std::vector<std::string> subtree (const std::string& id) const;

        // {"id","t","p","v"} for one entity, or null if missing.
        Json entityJson (const std::string& id) const;

        // Whole document: {"e": [entityJson, ...]} with parents before children.
        Json toJson() const;
        static std::optional<Doc> fromJson (const Json&);

        bool operator== (const Doc& other) const;

    private:
        std::unordered_map<std::string, Entity> entities;
        std::unordered_map<std::string, std::vector<std::string>> kids;
    };
}
