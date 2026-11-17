#pragma once

#include "Doc.h"

#include <optional>
#include <string>
#include <vector>

namespace tutti
{
    // One change to the document.
    struct Op
    {
        enum class Kind { create, remove, set };

        Kind kind = Kind::set;
        std::string id;
        std::string type;            // create only
        std::string parent;          // create only
        Json props = Json::object(); // create: initial props; set: changed props (null = delete)

        static Op create (std::string id, std::string type, std::string parent, Json props = Json::object());
        static Op remove (std::string id);
        static Op set (std::string id, Json props);

        // Wire form: {"k":"c","id","t","p","v"} / {"k":"r","id"} / {"k":"s","id","v"}.
        Json toJson() const;
        static std::optional<Op> fromJson (const Json&);

        bool operator== (const Op& o) const
        {
            return kind == o.kind && id == o.id && type == o.type && parent == o.parent && props == o.props;
        }
    };

    using Ops = std::vector<Op>;

    Json toJson (const Ops&);
    std::optional<Ops> opsFromJson (const Json&);

    // Applies one op. Replays are harmless: creating an existing id merges its props, removing a
    // missing id does nothing. Returns false only when the op can't apply (create under a missing
    // parent, set on a missing entity).
    bool apply (Doc&, const Op&);
}
