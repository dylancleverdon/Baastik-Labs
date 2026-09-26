#pragma once

#include <string>
#include <vector>

#include "serum2/PresetFile.h"

namespace serumgen
{
using serum2::Json;

struct CategoryInfo
{
    std::string id;
    std::string name;
    std::string family; // "synth", "acoustic", "drums", "fx"
};

struct GenreInfo
{
    std::string id;
    std::string name;
    std::string family; // "electronic", "acoustic"
};

// Everything the generator knows about sound design, as data: the base knob
// ranges, categories, genres, recipes, macro themes and name word lists.
//
// The built-in content is compiled into the plugin. A content bundle (one
// JSON file with the same sections) can replace it at runtime, which is how
// new genres and tweaks ship without reinstalling.
class ContentLibrary
{
public:
    static const ContentLibrary& builtin();
    // Build from a bundle: {"version", "minEngine", "base", "categories",
    // "genres", "recipes", "macroThemes", "names"}.
    static ContentLibrary fromBundle(const Json& bundle);
    // The built-in content as a bundle (what the release workflow publishes).
    static Json builtinBundle();

    // Bump when the generator starts relying on new bundle features. Bundles
    // declaring a higher minEngine are ignored by older plugins.
    static constexpr int kEngineVersion = 1;

    int version() const { return version_; }
    const Json& base() const { return bundle_["base"]; }
    const Json& categoryData(const std::string& id) const;
    const Json& genreData(const std::string& id) const;
    const Json& recipe(const std::string& id) const;
    const Json& macroThemes() const { return bundle_["macroThemes"]; }
    const Json& names() const { return bundle_["names"]; }
    const Json& bundle() const { return bundle_; }

    const std::vector<CategoryInfo>& categories() const { return categories_; }
    const std::vector<GenreInfo>& genres() const { return genres_; }
    std::vector<std::string> recipeIds() const;

private:
    Json bundle_;
    int version_ = 0;
    std::vector<CategoryInfo> categories_;
    std::vector<GenreInfo> genres_;
};
} // namespace serumgen
