#pragma once

#include <filesystem>
#include <string>

#include "tts/Server.h"

namespace tts
{
// Command line shared by the shipped app and the dev build:
//   text-to-synth                  run the MCP server on stdin/stdout (what Claude launches)
//   text-to-synth --register       add Text To Synth to Claude Desktop / Claude Code
//   text-to-synth --unregister     remove it again
//   text-to-synth --version
//   text-to-synth --content-bundle <out.json>   write the vocabulary bundle (release workflow)
//   text-to-synth --describe <file.SerumPreset>
// Returns the process exit code.
int runApp(int argc, char** argv, const std::string& version, UpdateService* updates,
           const std::filesystem::path& selfPath);

// True when argv asks for the MCP server (no command-line action).
bool wantsServer(int argc, char** argv);
} // namespace tts
