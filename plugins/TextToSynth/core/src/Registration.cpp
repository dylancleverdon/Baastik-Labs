#include "tts/Registration.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "tts/Store.h"

namespace tts
{
namespace fs = std::filesystem;

namespace
{
// Ordered, so a user's config keeps its key order when we touch it.
using OJson = nlohmann::ordered_json;

std::string u8(const fs::path& p)
{
    const auto s = p.u8string();
    return std::string(s.begin(), s.end());
}

OJson serverEntry(const fs::path& binary, bool claudeCode)
{
    OJson e = OJson::object();
    if (claudeCode)
        e["type"] = "stdio";
    e["command"] = u8(binary);
    e["args"] = OJson::array();
    if (claudeCode)
        e["env"] = OJson::object();
    return e;
}

void writeAtomically(const fs::path& file, const std::string& text)
{
    const auto tmp = fs::path(file).concat(".text-to-synth.tmp");
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out)
            throw std::runtime_error("can't write " + u8(tmp));
        out << text;
    }
    fs::rename(tmp, file);
}

RegistrationResult update(const std::string& client, const fs::path& file, const fs::path& binary, bool remove,
                          bool createIfMissing, bool claudeCode)
{
    RegistrationResult result { client, file, {} };
    try
    {
        std::error_code ec;
        const bool exists = fs::exists(file, ec);
        if (!exists && (remove || !createIfMissing))
        {
            result.status = remove ? "nothing to remove" : "not installed";
            return result;
        }

        OJson config = OJson::object();
        std::string original;
        if (exists)
        {
            std::ifstream in(file, std::ios::binary);
            std::stringstream buf;
            buf << in.rdbuf();
            original = buf.str();
            if (!original.empty())
                config = OJson::parse(original);
            if (!config.is_object())
                throw std::runtime_error("not a JSON object");
        }

        auto& servers = config["mcpServers"];
        if (!servers.is_object())
            servers = OJson::object();
        if (remove)
        {
            if (!servers.contains(kServerId))
            {
                result.status = "nothing to remove";
                return result;
            }
            servers.erase(kServerId);
            result.status = "removed";
        }
        else
        {
            auto entry = serverEntry(binary, claudeCode);
            if (servers.contains(kServerId))
            {
                // Keep anything the user added (env vars, args); refresh the command.
                auto merged = servers[kServerId];
                if (!merged.is_object())
                    merged = OJson::object();
                for (auto& [k, v] : entry.items())
                    if (k == "command" || !merged.contains(k))
                        merged[k] = v;
                if (merged == servers[kServerId])
                {
                    result.status = "already set";
                    return result;
                }
                servers[kServerId] = merged;
                result.status = "updated";
            }
            else
            {
                servers[kServerId] = entry;
                result.status = "added";
            }
        }

        fs::create_directories(file.parent_path());
        if (exists)
        {
            const auto backup = fs::path(file).concat(".before-text-to-synth");
            if (!fs::exists(backup, ec))
                fs::copy_file(file, backup, ec);
        }
        writeAtomically(file, config.dump(2) + "\n");
    }
    catch (const std::exception& e)
    {
        result.status = std::string("error: ") + e.what() + " (left unchanged)";
    }
    return result;
}
} // namespace

fs::path claudeDesktopConfigPath()
{
    return Paths::appData() / "Claude" / "claude_desktop_config.json";
}

fs::path claudeCodeConfigPath()
{
    if (const char* dir = std::getenv("CLAUDE_CONFIG_DIR"); dir && *dir)
        return fs::path(dir) / ".claude.json";
    return Paths::home() / ".claude.json";
}

std::vector<RegistrationResult> registerWithClaude(const fs::path& binary, bool remove)
{
    std::vector<RegistrationResult> results;
    // Claude Desktop: create the config if needed, so a later Claude install
    // finds Text To Synth ready.
    results.push_back(update("Claude Desktop", claudeDesktopConfigPath(), binary, remove, true, false));
    // Claude Code: only when it's installed (its config exists).
    results.push_back(update("Claude Code", claudeCodeConfigPath(), binary, remove, false, true));
    return results;
}
} // namespace tts
