#include "tts/App.h"

#include <fstream>
#include <iostream>

#include "tts/Content.h"
#include "tts/Describe.h"
#include "tts/Edits.h"
#include "tts/Registration.h"

namespace tts
{
bool wantsServer(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]).rfind("--", 0) == 0 && std::string(argv[i]) != "--stdio")
            return false;
    return true;
}

int runApp(int argc, char** argv, const std::string& version, UpdateService* updates, const std::filesystem::path& selfPath)
{
    const std::string command = argc > 1 ? argv[1] : "";
    try
    {
        if (command == "--version")
        {
            std::cout << kProductName << " " << version << " by " << kCompanyName << "\n";
            return 0;
        }
        if (command == "--register" || command == "--unregister")
        {
            const bool remove = command == "--unregister";
            const auto binary = argc > 2 ? std::filesystem::path(argv[2]) : selfPath;
            int errors = 0;
            for (const auto& r : registerWithClaude(binary, remove))
            {
                std::cout << r.client << ": " << r.status << " (" << r.configFile.string() << ")\n";
                errors += r.status.rfind("error", 0) == 0;
            }
            if (!remove)
                std::cout << "Restart Claude to load Text To Synth.\n";
            return errors == 0 ? 0 : 1;
        }
        if (command == "--content-bundle" && argc > 2)
        {
            std::ofstream out(argv[2], std::ios::binary | std::ios::trunc);
            out << Content::builtin().dump(2) << "\n";
            return out ? 0 : 1;
        }
        if (command == "--describe" && argc > 2)
        {
            std::cout << describePreset(serum2::readPresetFile(argv[2]), PresetEditor::defaultResolver());
            return 0;
        }
        if (!wantsServer(argc, argv))
        {
            std::cerr << "usage: text-to-synth [--register | --unregister | --version | --describe <file> | "
                         "--content-bundle <out.json>]\n";
            return 2;
        }

        // The MCP server. stdout carries protocol messages only; logs go to stderr.
        std::ios::sync_with_stdio(false);
        PresetStore store(Paths::defaults());
        McpServer server(store, version, updates);
        server.run(std::cin, std::cout);
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "text-to-synth: " << e.what() << "\n";
        return 1;
    }
}
} // namespace tts
