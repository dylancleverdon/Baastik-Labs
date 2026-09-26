#include "tts/Store.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <stdexcept>

#include "serumgen/Generator.h"

namespace tts
{
using serum2::Json;

namespace
{
std::string env(const char* name)
{
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string();
}

std::string nowIso()
{
    const auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm {};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string u8(const fs::path& p)
{
    const auto s = p.u8string();
    return std::string(s.begin(), s.end());
}

fs::path fromU8(const std::string& s)
{
    return fs::path(std::u8string(s.begin(), s.end()));
}

Json readLog(const fs::path& dir)
{
    std::ifstream in(dir / "log.json");
    if (!in)
        return Json::array();
    try
    {
        auto j = Json::parse(in);
        return j.is_array() ? j : Json::array();
    }
    catch (const std::exception&)
    {
        return Json::array();
    }
}

void writeLog(const fs::path& dir, const Json& log)
{
    std::ofstream out(dir / "log.json", std::ios::binary | std::ios::trunc);
    out << log.dump(2);
}

constexpr const char* kExtension = ".SerumPreset";
} // namespace

// ------------------------------------------------------------------- paths

fs::path Paths::home()
{
#if defined(_WIN32)
    if (auto p = env("USERPROFILE"); !p.empty())
        return fromU8(p);
#endif
    if (auto p = env("HOME"); !p.empty())
        return fromU8(p);
    return fs::current_path();
}

namespace
{
fs::path& documentsOverride()
{
    static fs::path folder;
    return folder;
}
} // namespace

void Paths::setDocumentsFolder(fs::path folder)
{
    documentsOverride() = std::move(folder);
}

fs::path Paths::documents()
{
    if (!documentsOverride().empty())
        return documentsOverride();
    return home() / "Documents";
}

fs::path Paths::appData()
{
#if defined(_WIN32)
    if (auto p = env("APPDATA"); !p.empty())
        return fromU8(p);
    return home() / "AppData" / "Roaming";
#elif defined(__APPLE__)
    return home() / "Library" / "Application Support";
#else
    if (auto p = env("XDG_CONFIG_HOME"); !p.empty())
        return fromU8(p);
    return home() / ".config";
#endif
}

Paths Paths::defaults()
{
    Paths p;
    p.presets = documents() / "Xfer" / "Serum 2 Presets" / "Presets" / "User" / "Text To Synth";
    p.data = appData() / "Baastik Labs" / "Text To Synth";
    if (auto o = env("TEXT_TO_SYNTH_PRESETS_DIR"); !o.empty())
        p.presets = fromU8(o);
    if (auto o = env("TEXT_TO_SYNTH_DATA_DIR"); !o.empty())
        p.data = fromU8(o);
    return p;
}

// ------------------------------------------------------------------ names

std::string presetNameOf(const serum2::Preset& preset)
{
    return preset.metadata.value("presetName", std::string("Untitled"));
}

void setPresetName(serum2::Preset& preset, const std::string& name)
{
    preset.metadata["presetName"] = name;
    preset.data["presetName"] = name;
}

// ------------------------------------------------------------------ store

PresetStore::PresetStore(Paths paths)
    : paths_(std::move(paths))
{
}

bool PresetStore::owns(const fs::path& file) const
{
    std::error_code ec;
    const auto dir = fs::weakly_canonical(paths_.presets, ec);
    const auto parent = fs::weakly_canonical(file, ec).parent_path();
    return parent == dir;
}

std::vector<fs::path> PresetStore::list() const
{
    std::vector<fs::path> files;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(paths_.presets, ec))
        if (e.is_regular_file() && e.path().extension() == kExtension)
            files.push_back(e.path());
    std::sort(files.begin(), files.end());
    return files;
}

fs::path PresetStore::resolve(const std::string& nameOrPath) const
{
    if (nameOrPath.empty())
        throw std::invalid_argument("which preset? Give its name or path");
    const auto asPath = fromU8(nameOrPath);
    std::error_code ec;
    if (fs::is_regular_file(asPath, ec))
        return fs::absolute(asPath);

    // Our folder: exact file name, name without extension, or preset name.
    auto wanted = lower(nameOrPath);
    if (wanted.size() > 12 && wanted.ends_with(lower(kExtension)))
        wanted.resize(wanted.size() - 12);
    for (const auto& f : list())
        if (lower(u8(f.stem())) == wanted)
            return f;
    for (const auto& f : list())
    {
        try
        {
            if (lower(presetNameOf(serum2::readPresetFile(f))) == wanted)
                return f;
        }
        catch (const std::exception&)
        {
        }
    }

    std::string known;
    int n = 0;
    for (const auto& f : list())
        if (n++ < 25)
            known += (known.empty() ? "" : ", ") + u8(f.stem());
    throw std::invalid_argument("no preset called \"" + nameOrPath + "\"" + (known.empty() ? "" : ". Mine: " + known)
                                + ". Other presets need a full file path.");
}

fs::path PresetStore::historyDir(const fs::path& file) const
{
    return paths_.data / "History" / file.stem();
}

fs::path PresetStore::versionFile(const fs::path& file, int version) const
{
    char name[32];
    std::snprintf(name, sizeof name, "v%03d.SerumPreset", version);
    return historyDir(file) / name;
}

std::vector<VersionInfo> PresetStore::versions(const fs::path& file) const
{
    std::vector<VersionInfo> out;
    for (const auto& e : readLog(historyDir(file)))
        out.push_back({ e.value("version", 0), e.value("note", std::string()), e.value("time", std::string()) });
    return out;
}

int PresetStore::record(const fs::path& file, const serum2::Bytes& bytes, const std::string& note)
{
    const auto dir = historyDir(file);
    fs::create_directories(dir);
    auto log = readLog(dir);
    const int version = log.empty() ? 1 : log.back().value("version", 0) + 1;
    serum2::writeFile(versionFile(file, version), bytes);
    log.push_back({ { "version", version }, { "note", note }, { "time", nowIso() } });
    writeLog(dir, log);
    return version;
}

serum2::Preset PresetStore::load(const fs::path& file) const
{
    return serum2::readPresetFile(file);
}

fs::path PresetStore::create(serum2::Preset preset, const std::string& note)
{
    fs::create_directories(paths_.presets);
    const auto base = presetNameOf(preset);
    auto fileName = serumgen::presetFileName(base); // "Name.SerumPreset", filesystem-safe
    auto stem = fileName.substr(0, fileName.size() - 12);
    fs::path file = paths_.presets / fromU8(fileName);
    std::string name = base;
    // Never overwrite: a new description gets a new preset, and the history
    // folder of an old one with the same name must not be reused.
    for (int i = 2; fs::exists(file) || fs::exists(historyDir(file)); ++i)
    {
        name = base + " " + std::to_string(i);
        file = paths_.presets / fromU8(stem + " " + std::to_string(i) + kExtension);
    }
    setPresetName(preset, name);
    const auto bytes = serum2::encodePreset(preset);
    serum2::writeFile(file, bytes);
    record(file, bytes, note);
    return file;
}

int PresetStore::commit(const fs::path& file, const serum2::Preset& preset, const std::string& note)
{
    if (!owns(file))
        throw std::logic_error("commit outside the Text To Synth folder");
    // Keep changes made in Serum since our last write as their own version.
    const auto log = versions(file);
    std::error_code ec;
    if (fs::exists(file, ec))
    {
        const auto onDisk = serum2::readFile(file);
        const bool known = !log.empty() && fs::exists(versionFile(file, log.back().version))
                        && serum2::readFile(versionFile(file, log.back().version)) == onDisk;
        if (!known)
            record(file, onDisk, log.empty() ? "original" : "changes saved in Serum");
    }
    const auto bytes = serum2::encodePreset(preset);
    serum2::writeFile(file, bytes);
    return record(file, bytes, note);
}

int PresetStore::restore(const fs::path& file, int version)
{
    const auto from = versionFile(file, version);
    if (!fs::exists(from))
        throw std::invalid_argument("there's no version " + std::to_string(version));
    return commit(file, serum2::readPresetFile(from), "restored version " + std::to_string(version));
}
} // namespace tts
