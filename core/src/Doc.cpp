#include "tutti/Doc.h"

#include <algorithm>

namespace tutti
{
    Doc::Doc()
    {
        entities[rootId] = Entity { "song", {}, Json::object() };
    }

    const Entity* Doc::find (const std::string& id) const
    {
        auto it = entities.find (id);
        return it == entities.end() ? nullptr : &it->second;
    }

    bool Doc::create (const std::string& id, const std::string& type, const std::string& parent, const Json& props)
    {
        if (id.empty() || has (id) || ! has (parent))
            return false;

        Entity e { type, parent, Json::object() };
        if (props.is_object())
            for (auto it = props.begin(); it != props.end(); ++it)
                if (! it.value().is_null())
                    e.props[it.key()] = it.value();

        entities.emplace (id, std::move (e));
        kids[parent].push_back (id);
        return true;
    }

    std::vector<std::string> Doc::remove (const std::string& id)
    {
        if (id == rootId || ! has (id))
            return {};

        auto removed = subtree (id);
        const auto parent = entities[id].parent;

        for (const auto& r : removed)
        {
            entities.erase (r);
            kids.erase (r);
        }

        if (auto it = kids.find (parent); it != kids.end())
        {
            auto& list = it->second;
            list.erase (std::remove (list.begin(), list.end(), id), list.end());
        }
        return removed;
    }

    bool Doc::set (const std::string& id, const Json& props)
    {
        auto it = entities.find (id);
        if (it == entities.end() || ! props.is_object())
            return false;

        auto& target = it->second.props;
        for (auto p = props.begin(); p != props.end(); ++p)
        {
            if (p.value().is_null())
                target.erase (p.key());
            else
                target[p.key()] = p.value();
        }
        return true;
    }

    const std::vector<std::string>& Doc::children (const std::string& id) const
    {
        static const std::vector<std::string> none;
        auto it = kids.find (id);
        return it == kids.end() ? none : it->second;
    }

    std::vector<std::string> Doc::orderedChildren (const std::string& id, const std::string& type) const
    {
        std::vector<std::string> out;
        for (const auto& c : children (id))
            if (auto* e = find (c); e != nullptr && e->type == type)
                out.push_back (c);

        std::stable_sort (out.begin(), out.end(), [this] (const std::string& a, const std::string& b) {
            const double pa = number (find (a)->props, "pos");
            const double pb = number (find (b)->props, "pos");
            if (pa != pb)
                return pa < pb;
            return a < b;
        });
        return out;
    }

    std::vector<std::string> Doc::ancestors (const std::string& id) const
    {
        std::vector<std::string> out;
        auto* e = find (id);
        while (e != nullptr && ! e->parent.empty())
        {
            out.push_back (e->parent);
            e = find (e->parent);
        }
        return out;
    }

    std::vector<std::string> Doc::subtree (const std::string& id) const
    {
        std::vector<std::string> out;
        if (! has (id))
            return out;

        std::vector<std::string> stack { id };
        while (! stack.empty())
        {
            auto current = std::move (stack.back());
            stack.pop_back();
            const auto& c = children (current);
            for (auto it = c.rbegin(); it != c.rend(); ++it)
                stack.push_back (*it);
            out.push_back (std::move (current));
        }
        return out;
    }

    Json Doc::entityJson (const std::string& id) const
    {
        auto* e = find (id);
        if (e == nullptr)
            return nullptr;
        return Json { { "id", id }, { "t", e->type }, { "p", e->parent }, { "v", e->props } };
    }

    Json Doc::toJson() const
    {
        Json list = Json::array();
        for (const auto& id : subtree (rootId))
            list.push_back (entityJson (id));
        return Json { { "e", std::move (list) } };
    }

    std::optional<Doc> Doc::fromJson (const Json& j)
    {
        const auto& list = field (j, "e");
        if (! list.is_array())
            return std::nullopt;

        Doc doc;
        for (const auto& item : list)
        {
            const auto id = str (item, "id");
            if (id == rootId)
            {
                doc.entities[rootId].props = field (item, "v").is_object() ? field (item, "v") : Json::object();
                continue;
            }
            if (! doc.create (id, str (item, "t"), str (item, "p"), field (item, "v")))
                return std::nullopt;
        }
        return doc;
    }

    bool Doc::operator== (const Doc& other) const
    {
        if (entities.size() != other.entities.size())
            return false;
        for (const auto& [id, e] : entities)
        {
            auto* o = other.find (id);
            if (o == nullptr || o->type != e.type || o->parent != e.parent || o->props != e.props)
                return false;
        }
        return true;
    }
}
