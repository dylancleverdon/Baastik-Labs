// norg-sign: Ed25519 keys and signatures for Norg updates (used by CI, no JUCE needed).
//
//   norg-sign keygen                      print a new seed (secret!) and its public key
//   norg-sign public                      print the public key for $NORG_UPDATE_SIGNING_KEY
//   norg-sign sign   --version V --build B --zip-name N --url U --sha256 H
//                                         print the signature, using $NORG_UPDATE_SIGNING_KEY
//   norg-sign verify --public P --signature S --version V --build B --zip-name N --url U --sha256 H

#include "UpdateCanonical.h"

#include <monocypher-ed25519.h>
#include <monocypher.h>

#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <string>

namespace
{
    std::string toHex (const uint8_t* data, size_t size)
    {
        static const char* digits = "0123456789abcdef";
        std::string out;
        for (size_t i = 0; i < size; ++i)
        {
            out += digits[data[i] >> 4];
            out += digits[data[i] & 15];
        }
        return out;
    }

    template <size_t N>
    std::optional<std::array<uint8_t, N>> fromHex (const std::string& hex)
    {
        if (hex.size() != N * 2)
            return std::nullopt;

        std::array<uint8_t, N> out {};
        for (size_t i = 0; i < N; ++i)
        {
            char* end = nullptr;
            const auto byte = hex.substr (i * 2, 2);
            out[i] = static_cast<uint8_t> (std::strtoul (byte.c_str(), &end, 16));
            if (end != byte.c_str() + 2)
                return std::nullopt;
        }
        return out;
    }

    std::optional<std::array<uint8_t, 32>> seedFromEnv()
    {
        const char* env = std::getenv ("NORG_UPDATE_SIGNING_KEY");
        if (env == nullptr)
            return std::nullopt;

        std::string value (env);
        while (! value.empty() && std::isspace (static_cast<unsigned char> (value.back())))
            value.pop_back();
        return fromHex<32> (value);
    }

    void keyPair (std::array<uint8_t, 32> seed, uint8_t secret[64], uint8_t publicKey[32])
    {
        crypto_ed25519_key_pair (secret, publicKey, seed.data()); // wipes its seed argument (a copy)
    }

    std::map<std::string, std::string> parseArgs (int argc, char* argv[])
    {
        std::map<std::string, std::string> args;
        for (int i = 2; i + 1 < argc; i += 2)
            args[argv[i]] = argv[i + 1];
        return args;
    }

    std::string message (std::map<std::string, std::string>& a)
    {
        return norg::update::canonicalMessage (a["--version"], std::stoll (a["--build"]), a["--zip-name"],
                                               a["--url"], a["--sha256"]);
    }

    int usage()
    {
        std::cerr << "usage: norg-sign keygen | public | sign ... | verify ...\n";
        return 64;
    }
}

int main (int argc, char* argv[])
{
    if (argc < 2)
        return usage();

    const std::string command = argv[1];

    if (command == "keygen")
    {
        std::array<uint8_t, 32> seed {};
        std::ifstream random ("/dev/urandom", std::ios::binary);
        if (! random.read (reinterpret_cast<char*> (seed.data()), seed.size()))
        {
            std::cerr << "could not read /dev/urandom\n";
            return 1;
        }

        uint8_t secret[64], publicKey[32];
        keyPair (seed, secret, publicKey);
        std::cout << "seed=" << toHex (seed.data(), seed.size()) << "\n"
                  << "public=" << toHex (publicKey, 32) << "\n";
        crypto_wipe (secret, sizeof (secret));
        crypto_wipe (seed.data(), seed.size());
        return 0;
    }

    if (command == "public" || command == "sign")
    {
        const auto seed = seedFromEnv();
        if (! seed)
        {
            std::cerr << "NORG_UPDATE_SIGNING_KEY is missing or not 64 hex characters\n";
            return 2;
        }

        uint8_t secret[64], publicKey[32];
        keyPair (*seed, secret, publicKey);

        if (command == "public")
        {
            std::cout << toHex (publicKey, 32) << "\n";
        }
        else
        {
            auto args = parseArgs (argc, argv);
            for (const char* required : { "--version", "--build", "--zip-name", "--url", "--sha256" })
                if (args[required].empty())
                    return usage();

            const auto msg = message (args);
            uint8_t signature[64];
            crypto_ed25519_sign (signature, secret, reinterpret_cast<const uint8_t*> (msg.data()), msg.size());
            std::cout << toHex (signature, 64) << "\n";
        }

        crypto_wipe (secret, sizeof (secret));
        return 0;
    }

    if (command == "verify")
    {
        auto args = parseArgs (argc, argv);
        const auto publicKey = fromHex<32> (args["--public"]);
        const auto signature = fromHex<64> (args["--signature"]);
        if (! publicKey || ! signature || args["--build"].empty())
            return usage();

        const auto msg = message (args);
        const bool ok = crypto_ed25519_check (signature->data(), publicKey->data(),
                                              reinterpret_cast<const uint8_t*> (msg.data()), msg.size()) == 0;
        std::cout << (ok ? "valid" : "INVALID") << "\n";
        return ok ? 0 : 1;
    }

    return usage();
}
