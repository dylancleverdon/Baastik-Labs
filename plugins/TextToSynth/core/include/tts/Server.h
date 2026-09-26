#pragma once

#include <iosfwd>
#include <optional>
#include <string>

#include "serum2/PresetFile.h"
#include "tts/Store.h"

namespace tts
{
using serum2::Json;

inline constexpr const char* kProductName = "Text To Synth";
inline constexpr const char* kCompanyName = "Baastik Labs";

// The app's updater, seen from the server. The shipped build implements this
// with the Baastik updater; the dev build has none.
class UpdateService
{
public:
    virtual ~UpdateService() = default;
    // One line for the end of tool results while an update waits ("" if none).
    virtual std::string notice() = 0;
    // Checks now and says what it found.
    virtual std::string checkNow() = 0;
    // Downloads and opens the installer; says what happened.
    virtual std::string installUpdate() = 0;
};

// A Model Context Protocol server over stdio (newline-delimited JSON-RPC 2.0)
// exposing the Text To Synth tools. Claude turns words into edits; this turns
// edits into Serum 2 presets.
class McpServer
{
public:
    McpServer(PresetStore& store, std::string version, UpdateService* updates = nullptr);

    // One JSON-RPC message in, the response out (nothing for notifications).
    std::optional<Json> handle(const Json& message);
    // Reads messages line by line until EOF.
    void run(std::istream& in, std::ostream& out);

    // Runs a tool: {"content": [{"type": "text", ...}], "isError": bool}.
    Json callTool(const std::string& name, const Json& args);
    static Json tools();
    static std::string instructions();

private:
    std::string guide(const Json& args);
    std::string startingPoints();
    std::string createPreset(const Json& args);
    std::string describe(const Json& args);
    std::string editPreset(const Json& args);
    std::string listVersions(const Json& args);
    std::string restoreVersion(const Json& args);
    std::string listPresets();

    PresetStore& store_;
    std::string version_;
    UpdateService* updates_;
};
} // namespace tts
