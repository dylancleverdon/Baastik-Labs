// serumtool: inspect, convert and generate Serum 2 presets from the command line.

#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "serum2/Patch.h"
#include "serum2/PresetFile.h"
#include "serumgen/Content.h"
#include "serumgen/Generator.h"

namespace fs = std::filesystem;
using serum2::Json;

namespace
{
const char* kUsage = R"(serumtool - Serum 2 preset utilities

usage:
  serumtool dump <preset.SerumPreset> [out.json]   decode a preset to JSON
  serumtool pack <preset.json> <out.SerumPreset>   encode JSON back to a preset
  serumtool diff <a.SerumPreset> <b.SerumPreset>   list parameters that differ
  serumtool scan <dir>                             list wavetables used by presets
  serumtool list                                   categories, genres, recipes, groups
  serumtool gen [options]                          generate presets
  serumtool content-bundle <out.json>              write the built-in content bundle

gen options:
  --mode random|guided|mutate   (default guided)
  --category <id|any>           (default any)
  --genre <id|any|none>         (default none)
  --chaos <0..1>                (default 0.2)
  --seed <n>                    first seed (default: random)
  --count <n>                   presets to write (default 1)
  --lock <group,group,...>      keep these groups from --base
  --base <preset.SerumPreset>   base for locks and mutate mode
  --content <bundle.json>       use a content bundle instead of the built-in one
  --out <dir>                   output folder (default .)
  --quiet                       don't print summaries
)";

struct Args
{
    std::vector<std::string> positional;
    std::map<std::string, std::string> options;
    std::set<std::string> flags;
};

Args parse(int argc, char** argv, int first)
{
    Args a;
    for (int i = first; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg.rfind("--", 0) == 0)
        {
            const auto name = arg.substr(2);
            if (name == "quiet" || name == "help")
                a.flags.insert(name);
            else if (i + 1 < argc)
                a.options[name] = argv[++i];
            else
                throw std::invalid_argument("missing value for " + arg);
        }
        else
            a.positional.push_back(arg);
    }
    return a;
}

void writeText(const fs::path& path, const std::string& text)
{
    std::ofstream out(path);
    if (!out)
        throw std::runtime_error("cannot write " + path.string());
    out << text;
}

int dump(const Args& a)
{
    if (a.positional.empty())
        throw std::invalid_argument("dump needs a preset file");
    const auto preset = serum2::readPresetFile(a.positional[0]);
    const Json out { { "metadata", preset.metadata }, { "data", preset.data } };
    if (a.positional.size() > 1)
        writeText(a.positional[1], out.dump(1) + "\n");
    else
        std::cout << out.dump(1) << "\n";
    return 0;
}

int pack(const Args& a)
{
    if (a.positional.size() < 2)
        throw std::invalid_argument("pack needs <preset.json> <out.SerumPreset>");
    std::ifstream in(a.positional[0]);
    const auto j = Json::parse(in);
    serum2::writePresetFile(a.positional[1], { j.at("metadata"), j.at("data") });
    std::cout << "wrote " << a.positional[1] << "\n";
    return 0;
}

void flatten(const Json& j, const std::string& path, std::map<std::string, Json>& out)
{
    if (j.is_object())
    {
        for (const auto& [k, v] : j.items())
            flatten(v, path.empty() ? k : path + "/" + k, out);
    }
    else if (j.is_array() && j.size() > 32)
    {
        out[path] = Json("<array of " + std::to_string(j.size()) + ">");
    }
    else
        out[path] = j;
}

int diff(const Args& a)
{
    if (a.positional.size() < 2)
        throw std::invalid_argument("diff needs two presets");
    std::map<std::string, Json> left, right;
    flatten(serum2::readPresetFile(a.positional[0]).data, "", left);
    flatten(serum2::readPresetFile(a.positional[1]).data, "", right);
    std::set<std::string> keys;
    for (const auto& [k, v] : left)
        keys.insert(k);
    for (const auto& [k, v] : right)
        keys.insert(k);
    for (const auto& k : keys)
    {
        const auto l = left.count(k) ? left[k].dump() : "(absent)";
        const auto r = right.count(k) ? right[k].dump() : "(absent)";
        if (l != r)
            std::cout << k << ": " << l << " -> " << r << "\n";
    }
    return 0;
}

int scan(const Args& a)
{
    if (a.positional.empty())
        throw std::invalid_argument("scan needs a folder");
    std::map<std::string, Json> tables;
    for (const auto& entry : fs::recursive_directory_iterator(a.positional[0]))
    {
        if (entry.path().extension() != ".SerumPreset")
            continue;
        try
        {
            const auto preset = serum2::readPresetFile(entry.path());
            for (int i = 0; i < 3; ++i)
            {
                const auto key = "Oscillator" + std::to_string(i);
                const auto wt = "WTOsc" + std::to_string(i);
                if (!preset.data.contains(key) || !preset.data[key].contains(wt))
                    continue;
                const auto& w = preset.data[key][wt];
                if (!w.contains("relativePathToWT") || !w.contains("numFrames"))
                    continue;
                tables[w["relativePathToWT"].get<std::string>()] = Json {
                    { "path", w["relativePathToWT"] },   { "numFrames", w["numFrames"] },
                    { "sampleRate", w["sampleRate"] }, { "numChannels", w["numChannels"] },
                };
            }
        }
        catch (const std::exception& e)
        {
            std::cerr << entry.path().string() << ": " << e.what() << "\n";
        }
    }
    Json out = Json::array();
    for (const auto& [path, t] : tables)
        out.push_back(t);
    std::cout << out.dump(1) << "\n";
    return 0;
}

int list(const serumgen::ContentLibrary& content)
{
    std::cout << "categories:\n";
    for (const auto& c : content.categories())
        std::cout << "  " << c.id << "  (" << c.name << ", " << c.family << ")\n";
    std::cout << "genres:\n";
    for (const auto& g : content.genres())
        std::cout << "  " << g.id << "  (" << g.name << ", " << g.family << ")\n";
    std::cout << "recipes:\n";
    for (const auto& r : content.recipeIds())
        std::cout << "  " << r << "  " << content.recipe(r).value("name", "") << "\n";
    std::cout << "groups:\n";
    for (const auto g : serumgen::kAllGroups)
        std::cout << "  " << serumgen::groupId(g) << "  (" << serumgen::groupLabel(g) << ")\n";
    return 0;
}

int gen(const Args& a, const serumgen::ContentLibrary& content)
{
    serumgen::Settings s;
    const auto opt = [&](const std::string& name, const std::string& fallback) {
        const auto it = a.options.find(name);
        return it == a.options.end() ? fallback : it->second;
    };
    const auto mode = serumgen::modeFromId(opt("mode", "guided"));
    if (!mode)
        throw std::invalid_argument("unknown mode " + opt("mode", ""));
    s.mode = *mode;
    s.category = opt("category", "any");
    s.genre = opt("genre", "none");
    s.chaos = std::stod(opt("chaos", "0.2"));
    s.seed = a.options.count("seed") ? std::stoull(a.options.at("seed")) : std::random_device {}();
    const int count = std::stoi(opt("count", "1"));
    const fs::path outDir = opt("out", ".");
    const bool quiet = a.flags.count("quiet") > 0;

    if (a.options.count("lock"))
    {
        std::stringstream groups(a.options.at("lock"));
        std::string id;
        while (std::getline(groups, id, ','))
        {
            const auto g = serumgen::groupFromId(id);
            if (!g)
                throw std::invalid_argument("unknown group " + id);
            s.setLocked(*g, true);
        }
    }
    std::optional<serum2::Preset> base;
    if (a.options.count("base"))
        base = serum2::readPresetFile(a.options.at("base"));
    if (s.mode == serumgen::Mode::Mutate && !base)
        throw std::invalid_argument("mutate mode needs --base");

    const serumgen::Generator generator(content);
    fs::create_directories(outDir);
    for (int i = 0; i < count; ++i)
    {
        const auto result = generator.generate(s, base ? &*base : nullptr);
        auto path = outDir / serumgen::presetFileName(result.name);
        for (int n = 2; fs::exists(path); ++n)
            path = outDir / serumgen::presetFileName(result.name + " " + std::to_string(n));
        serum2::writePresetFile(path, result.preset);
        std::cout << path.filename().string() << "  [seed " << result.seed << ", " << result.category
                  << (result.genre.empty() ? "" : " / " + result.genre)
                  << (result.recipe.empty() ? "" : ", recipe " + result.recipe) << "]\n";
        if (!quiet)
            for (const auto& line : result.summary)
                std::cout << "    " << line << "\n";
        ++s.seed;
    }
    return 0;
}
} // namespace

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::cout << kUsage;
        return 1;
    }
    const std::string command = argv[1];
    try
    {
        const auto args = parse(argc, argv, 2);
        std::optional<serumgen::ContentLibrary> custom;
        if (args.options.count("content"))
        {
            std::ifstream in(args.options.at("content"));
            custom = serumgen::ContentLibrary::fromBundle(Json::parse(in));
        }
        const auto& content = custom ? *custom : serumgen::ContentLibrary::builtin();

        if (command == "dump")
            return dump(args);
        if (command == "pack")
            return pack(args);
        if (command == "diff")
            return diff(args);
        if (command == "scan")
            return scan(args);
        if (command == "list")
            return list(content);
        if (command == "gen")
            return gen(args, content);
        if (command == "content-bundle")
        {
            if (args.positional.empty())
                throw std::invalid_argument("content-bundle needs an output file");
            writeText(args.positional[0], serumgen::ContentLibrary::builtinBundle().dump(1) + "\n");
            return 0;
        }
        std::cout << kUsage;
        return command == "help" || command == "--help" ? 0 : 1;
    }
    catch (const std::exception& e)
    {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
