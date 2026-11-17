#pragma once

#include "Json.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace tutti::protocol
{
    // Bump when messages change incompatibly. Peers with different versions refuse to share a
    // session; the one with the older version checks for an update.
    inline constexpr int kProtocolVersion = 1;

    inline constexpr int kPeerPort = 47380;  // Tutti app <-> Tutti app, on the Tailscale address
    inline constexpr int kLocalPort = 47381; // Tutti app <-> Remote Script / Stand-in, on 127.0.0.1

    inline constexpr size_t kMaxFrameBytes = 64u * 1024u * 1024u;

    // Frames: [4-byte big-endian length of the rest][kind][payload]
    //   kind 'J': payload is UTF-8 JSON text
    //   kind 'B': payload is [4-byte big-endian header length][JSON header][raw bytes]
    struct Frame
    {
        enum class Kind : char { json = 'J', binary = 'B' };
        Kind kind = Kind::json;
        Json json;        // the message, or the binary frame's header
        std::string data; // binary frames only
    };

    std::string encode (const Json& message);
    std::string encodeBinary (const Json& header, const void* data, size_t size);

    // Turns a byte stream back into frames.
    class Decoder
    {
    public:
        void feed (const void* data, size_t size);
        std::optional<Frame> next();
        bool failed() const { return bad; }

    private:
        std::string buffer;
        size_t readPos = 0;
        bool bad = false;
    };
}
