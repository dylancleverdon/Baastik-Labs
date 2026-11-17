#include "tutti/Ops.h"

namespace tutti
{
    Op Op::create (std::string id, std::string type, std::string parent, Json props)
    {
        Op op;
        op.kind = Kind::create;
        op.id = std::move (id);
        op.type = std::move (type);
        op.parent = std::move (parent);
        op.props = props.is_object() ? std::move (props) : Json::object();
        return op;
    }

    Op Op::remove (std::string id)
    {
        Op op;
        op.kind = Kind::remove;
        op.id = std::move (id);
        return op;
    }

    Op Op::set (std::string id, Json props)
    {
        Op op;
        op.kind = Kind::set;
        op.id = std::move (id);
        op.props = props.is_object() ? std::move (props) : Json::object();
        return op;
    }

    Json Op::toJson() const
    {
        switch (kind)
        {
            case Kind::create: return Json { { "k", "c" }, { "id", id }, { "t", type }, { "p", parent }, { "v", props } };
            case Kind::remove: return Json { { "k", "r" }, { "id", id } };
            case Kind::set:    return Json { { "k", "s" }, { "id", id }, { "v", props } };
        }
        return nullptr;
    }

    std::optional<Op> Op::fromJson (const Json& j)
    {
        const auto k = str (j, "k");
        const auto id = str (j, "id");
        if (id.empty())
            return std::nullopt;

        if (k == "c")
        {
            const auto parent = str (j, "p");
            const auto type = str (j, "t");
            if (parent.empty() || type.empty())
                return std::nullopt;
            return create (id, type, parent, field (j, "v"));
        }
        if (k == "r")
            return remove (id);
        if (k == "s" && field (j, "v").is_object())
            return set (id, field (j, "v"));
        return std::nullopt;
    }

    Json toJson (const Ops& ops)
    {
        Json list = Json::array();
        for (const auto& op : ops)
            list.push_back (op.toJson());
        return list;
    }

    std::optional<Ops> opsFromJson (const Json& j)
    {
        if (! j.is_array())
            return std::nullopt;
        Ops ops;
        ops.reserve (j.size());
        for (const auto& item : j)
        {
            auto op = Op::fromJson (item);
            if (! op)
                return std::nullopt;
            ops.push_back (std::move (*op));
        }
        return ops;
    }

    bool apply (Doc& doc, const Op& op)
    {
        switch (op.kind)
        {
            case Op::Kind::create:
                if (doc.has (op.id))
                    return doc.set (op.id, op.props);
                return doc.create (op.id, op.type, op.parent, op.props);

            case Op::Kind::remove:
                doc.remove (op.id);
                return true;

            case Op::Kind::set:
                return doc.set (op.id, op.props);
        }
        return false;
    }
}
