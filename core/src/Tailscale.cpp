#include "tutti/Tailscale.h"

#include <memory>
#include <thread>

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

    std::optional<Status> query (std::string* problem)
    {
        return queryWith (findCli(), problem);
    }

    std::optional<Status> queryWith (const juce::File& cli, std::string* problem, int timeoutMs)
    {
        auto say = [problem] (const std::string& text) {
            if (problem != nullptr)
                *problem = text;
        };
        if (cli == juce::File())
        {
            say ("the tailscale command isn't installed");
            return std::nullopt;
        }
        juce::StringArray args;
       #if ! JUCE_WINDOWS
        // The Mac app's executable is both the app and the command-line tool, and it only acts as
        // the tool when started from a terminal. Started by Tutti it could open the app instead and
        // never answer. Tailscale's documented escape hatch: TAILSCALE_BE_CLI=1.
        args.addArray ({ "/usr/bin/env", "TAILSCALE_BE_CLI=1" });
       #endif
        args.addArray ({ cli.getFullPathName(), "status", "--json" });
        // Read on a thread of its own and wait for it with a deadline. Waiting for the output to
        // end isn't enough: a process the tool starts can keep the pipe open long after it quits.
        struct Run
        {
            juce::ChildProcess process;
            juce::String out;
            juce::WaitableEvent done;
        };
        auto run = std::make_shared<Run>();
        if (! run->process.start (args, juce::ChildProcess::wantStdOut))
        {
            say ("couldn't run " + cli.getFullPathName().toStdString());
            return std::nullopt;
        }
        std::thread ([run] {
            run->out = run->process.readAllProcessOutput();
            run->done.signal();
        }).detach();
        if (! run->done.wait (timeoutMs))
        {
            run->process.kill();
            say ("tailscale status didn't answer within " + std::to_string (timeoutMs / 1000) + " seconds");
            return std::nullopt;
        }
        const auto out = run->out;
        auto status = parse (out.toStdString());
        if (! status)
            say ("tailscale status said: " + out.substring (0, 200).trim().replaceCharacters ("\r\n", "  ").toStdString());
        return status;
    }

    bool isTailscaleAddress (const std::string& ip4)
    {
        // 100.64.0.0/10: 100.64.x.x to 100.127.x.x
        juce::StringArray parts;
        parts.addTokens (juce::String (ip4), ".", {});
        if (parts.size() != 4 || parts[0] != "100")
            return false;
        const int second = parts[1].getIntValue();
        return second >= 64 && second <= 127;
    }

    std::string localAddress()
    {
        for (const auto& a : juce::IPAddress::getAllAddresses (false))
            if (const auto ip = a.toString().toStdString(); isTailscaleAddress (ip))
                return ip;
        return {};
    }
}
