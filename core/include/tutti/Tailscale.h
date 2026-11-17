#pragma once

#include "Json.h"

#include <juce_core/juce_core.h>

#include <optional>
#include <vector>

// Reading `tailscale status --json`: who's on the private network and which address is ours.
namespace tutti::tailscale
{
    struct Peer
    {
        std::string hostName;
        std::string dnsName;
        std::string ip4;
        std::string user; // display name of the person the device belongs to
        std::string os;
        bool online = false;
    };

    struct Status
    {
        std::string backendState; // "Running" when connected
        std::string selfIp4;
        std::string selfHost;
        std::string selfUser;
        std::vector<Peer> peers;

        bool running() const { return backendState == "Running" && ! selfIp4.empty(); }
    };

    std::optional<Status> parse (const std::string& json);

    // The tailscale command-line tool on this computer, if installed.
    juce::File findCli();

    // Runs `tailscale status --json` (blocking, a second or so). nullopt if Tailscale isn't there.
    std::optional<Status> query();
}
