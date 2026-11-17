#include "tutti/Update.h"

namespace tutti::update
{
    std::optional<Manifest> parse (const std::string& json, std::string& error)
    {
        auto j = parseJson (json);
        if (! j || ! j->is_object())
        {
            error = "the update manifest isn't valid JSON";
            return std::nullopt;
        }
        Manifest m;
        m.schema = static_cast<int> (integer (*j, "schema"));
        m.version = str (*j, "version");
        m.build = integer (*j, "build");
        m.commit = str (*j, "commit");
        m.notes = str (*j, "notes");
        m.published = str (*j, "published");
        m.protocol = static_cast<int> (integer (*j, "protocol"));
        m.suggestionsEndpoint = str (field (*j, "suggestions"), "endpoint");
        if (m.schema != 1 || m.version.empty() || m.build <= 0)
        {
            error = "the update manifest is missing its version";
            return std::nullopt;
        }
        if (const auto& platforms = field (*j, "platforms"); platforms.is_object())
            for (auto it = platforms.begin(); it != platforms.end(); ++it)
            {
                Asset a;
                a.name = str (it.value(), "name");
                a.url = str (it.value(), "url");
                a.sha256 = str (it.value(), "sha256");
                a.size = integer (it.value(), "size");
                if (! a.url.empty() && a.sha256.size() == 64)
                    m.platforms[it.key()] = a;
            }
        return m;
    }

    bool isAllowedUrl (const std::string& url, const std::vector<std::string>& extraPrefixes)
    {
        if (url.find ("..") != std::string::npos)
            return false;
        if (url.rfind (allowedPrefix, 0) == 0)
            return true;
        for (const auto& p : extraPrefixes)
            if (! p.empty() && url.rfind (p, 0) == 0)
                return true;
        return false;
    }

    bool isSafeEntry (const std::string& name)
    {
        if (name.empty() || name[0] == '/' || name[0] == '\\' || name.find (':') != std::string::npos)
            return false;
        size_t start = 0;
        while (start <= name.size())
        {
            auto end = name.find_first_of ("/\\", start);
            if (end == std::string::npos)
                end = name.size();
            if (name.compare (start, end - start, "..") == 0)
                return false;
            start = end + 1;
        }
        return true;
    }

    Decision decide (const Manifest& m, int64_t installedBuild, const Settings& s, bool manualCheck,
                     const std::string& platform, const std::vector<std::string>& extraPrefixes)
    {
        auto it = m.platforms.find (platform);
        if (it == m.platforms.end() || ! isAllowedUrl (it->second.url, extraPrefixes))
            return Decision::unavailable;
        if (m.build <= installedBuild)
            return Decision::upToDate;
        if (m.build == s.skipBuild && ! manualCheck)
            return Decision::skipped;
        if (! s.autoUpdate && ! manualCheck)
            return Decision::disabled;
        return Decision::install;
    }
}
