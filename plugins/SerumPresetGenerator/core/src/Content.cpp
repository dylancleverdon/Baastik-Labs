#include "serumgen/Content.h"

#include <algorithm>
#include <stdexcept>

#include "serumgen/embedded/Resources.h"

namespace serumgen
{
namespace
{
const Json& emptyObject()
{
    static const Json empty = Json::object();
    return empty;
}

const Json& section(const Json& bundle, const char* name, const std::string& id)
{
    if (bundle.contains(name) && bundle[name].contains(id))
        return bundle[name][id];
    return emptyObject();
}

// "profiles/genres/jazz.json" -> ("genres", "jazz")
std::pair<std::string, std::string> classify(std::string_view path)
{
    const auto slash = path.rfind('/');
    const auto dot = path.rfind('.');
    const auto stem = std::string(path.substr(slash + 1, dot - slash - 1));
    const auto dir = slash == std::string_view::npos ? std::string {} : std::string(path.substr(0, slash));
    const auto parentSlash = dir.rfind('/');
    const auto folder = parentSlash == std::string::npos ? dir : dir.substr(parentSlash + 1);
    return { folder, stem };
}
} // namespace

Json ContentLibrary::builtinBundle()
{
    Json bundle = Json::object();
    bundle["categories"] = Json::object();
    bundle["genres"] = Json::object();
    bundle["recipes"] = Json::object();
    for (const auto& resource : embedded::all())
    {
        const auto json = Json::parse(resource.data);
        const auto [folder, stem] = classify(resource.name);
        if (folder == "categories" || folder == "genres" || folder == "recipes")
            bundle[folder][stem] = json;
        else if (stem == "base")
            bundle["base"] = json;
        else if (stem == "macros")
            bundle["macroThemes"] = json;
        else if (stem == "names")
            bundle["names"] = json;
        else if (stem == "content")
        {
            bundle["version"] = json.value("version", 1);
            bundle["minEngine"] = json.value("minEngine", 1);
        }
    }
#ifdef SERUMGEN_CONTENT_VERSION
    // Release builds stamp the content version so the plugin and the content
    // bundle published alongside it agree.
    bundle["version"] = SERUMGEN_CONTENT_VERSION;
#endif
    return bundle;
}

const ContentLibrary& ContentLibrary::builtin()
{
    static const ContentLibrary library = fromBundle(builtinBundle());
    return library;
}

ContentLibrary ContentLibrary::fromBundle(const Json& bundle)
{
    for (const auto* key : { "base", "categories", "genres", "recipes", "macroThemes", "names" })
        if (!bundle.contains(key) || !bundle[key].is_object())
            throw std::invalid_argument(std::string("content bundle is missing ") + key);
    if (bundle.value("minEngine", 1) > kEngineVersion)
        throw std::invalid_argument("content bundle needs a newer plugin");

    ContentLibrary lib;
    lib.bundle_ = bundle;
    lib.version_ = bundle.value("version", 0);
    for (const auto& [id, c] : bundle["categories"].items())
        lib.categories_.push_back({ id, c.value("name", id), c.value("family", "synth") });
    for (const auto& [id, g] : bundle["genres"].items())
        lib.genres_.push_back({ id, g.value("name", id), g.value("family", "electronic") });
    // Present in a stable, friendly order: by family, then name.
    const auto byFamily = [](const auto& a, const auto& b) {
        return a.family != b.family ? a.family > b.family : a.name < b.name;
    };
    std::sort(lib.categories_.begin(), lib.categories_.end(), byFamily);
    std::sort(lib.genres_.begin(), lib.genres_.end(), byFamily);
    return lib;
}

const Json& ContentLibrary::categoryData(const std::string& id) const
{
    return section(bundle_, "categories", id);
}

const Json& ContentLibrary::genreData(const std::string& id) const
{
    return section(bundle_, "genres", id);
}

const Json& ContentLibrary::recipe(const std::string& id) const
{
    return section(bundle_, "recipes", id);
}

std::vector<std::string> ContentLibrary::recipeIds() const
{
    std::vector<std::string> ids;
    for (const auto& [id, r] : bundle_["recipes"].items())
        ids.push_back(id);
    return ids;
}
} // namespace serumgen
