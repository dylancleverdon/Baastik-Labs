#include "tutti/Tailscale.h"

namespace tutti::tailscale
{
    namespace
    {
        std::string firstIp4 (const Json& ips)
        {
            if (ips.is_array())
                for (const auto& ip : ips)
                    if (ip.is_string() && ip.get<std::string>().find ('.') != std::string::npos)
                        return ip.get<std::string>();
            return {};
        }

        std::string userName (const Json& users, const Json& node)
        {
            const auto id = std::to_string (integer (node, "UserID"));
            const auto& u = field (users, id.c_str());
            auto display = str (u, "DisplayName");
            return display.empty() ? str (u, "LoginName") : display;
        }
    }

    std::optional<Status> parse (const std::string& json)
    {
        auto j = parseJson (json);
        if (! j || ! j->is_object())
            return std::nullopt;
        Status s;
        s.backendState = str (*j, "BackendState");
        const auto& users = field (*j, "User");
        const auto& self = field (*j, "Self");
        s.selfIp4 = firstIp4 (field (self, "TailscaleIPs"));
        s.selfHost = str (self, "HostName");
        s.selfUser = userName (users, self);
        if (const auto& peers = field (*j, "Peer"); peers.is_object())
            for (auto it = peers.begin(); it != peers.end(); ++it)
            {
                const auto& p = it.value();
                Peer peer;
                peer.hostName = str (p, "HostName");
                peer.dnsName = str (p, "DNSName");
                peer.ip4 = firstIp4 (field (p, "TailscaleIPs"));
                peer.user = userName (users, p);
                peer.os = str (p, "OS");
                peer.online = boolean (p, "Online");
                if (! peer.ip4.empty())
                    s.peers.push_back (peer);
            }
        return s;
    }

    juce::File findCli()
    {
        juce::StringArray candidates;
       #if JUCE_MAC
        candidates.add ("/Applications/Tailscale.app/Contents/MacOS/Tailscale");
        candidates.add ("/opt/homebrew/bin/tailscale");
        candidates.add ("/usr/local/bin/tailscale");
       #elif JUCE_WINDOWS
        candidates.add ("C:\\Program Files\\Tailscale\\tailscale.exe");
        candidates.add ("C:\\Program Files (x86)\\Tailscale\\tailscale.exe");
       #else
        candidates.add ("/usr/bin/tailscale");
        candidates.add ("/usr/local/bin/tailscale");
       #endif
        for (const auto& c : candidates)
            if (juce::File (c).existsAsFile())
                return juce::File (c);
        return {};
    }

    std::optional<Status> query()
    {
        auto cli = findCli();
        if (cli == juce::File())
            return std::nullopt;
        juce::ChildProcess process;
        if (! process.start (juce::StringArray { cli.getFullPathName(), "status", "--json" }, juce::ChildProcess::wantStdOut))
            return std::nullopt;
        const auto out = process.readAllProcessOutput();
        process.waitForProcessToFinish (5000);
        return parse (out.toStdString());
    }
}
