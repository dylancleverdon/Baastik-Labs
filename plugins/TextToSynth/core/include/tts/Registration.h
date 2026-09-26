#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace tts
{
// The key Text To Synth uses in Claude's MCP server configs.
inline constexpr const char* kServerId = "text-to-synth";

struct RegistrationResult
{
    std::string client; // "Claude Desktop", "Claude Code"
    std::filesystem::path configFile;
    std::string status; // "added", "updated", "already set", "removed", "not installed", "error: ..."
};

// Adds (or with remove, deletes) the Text To Synth entry in Claude Desktop's
// claude_desktop_config.json and, when Claude Code is installed, its user
// config (~/.claude.json). Other entries and key order are kept, and the file
// is backed up before the first change. The installers run this through
// `text-to-synth --register`.
std::vector<RegistrationResult> registerWithClaude(const std::filesystem::path& binary, bool remove = false);

std::filesystem::path claudeDesktopConfigPath();
std::filesystem::path claudeCodeConfigPath();
} // namespace tts
