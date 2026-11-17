#include "tutti/Protocol.h"

namespace tutti::protocol
{
    namespace
    {
        void putU32 (std::string& out, uint32_t v)
        {
            out += static_cast<char> ((v >> 24) & 0xff);
            out += static_cast<char> ((v >> 16) & 0xff);
            out += static_cast<char> ((v >> 8) & 0xff);
            out += static_cast<char> (v & 0xff);
        }

        uint32_t getU32 (const std::string& s, size_t at)
        {
            auto b = [&] (size_t i) { return static_cast<uint32_t> (static_cast<unsigned char> (s[at + i])); };
            return (b (0) << 24) | (b (1) << 16) | (b (2) << 8) | b (3);
        }
    }

    std::string encode (const Json& message)
    {
        const auto text = message.dump (-1, ' ', false, Json::error_handler_t::replace);
        std::string out;
        out.reserve (text.size() + 5);
        putU32 (out, static_cast<uint32_t> (text.size() + 1));
        out += static_cast<char> (Frame::Kind::json);
        out += text;
        return out;
    }

    std::string encodeBinary (const Json& header, const void* data, size_t size)
    {
        const auto head = header.dump (-1, ' ', false, Json::error_handler_t::replace);
        std::string out;
        out.reserve (head.size() + size + 9);
        putU32 (out, static_cast<uint32_t> (1 + 4 + head.size() + size));
        out += static_cast<char> (Frame::Kind::binary);
        putU32 (out, static_cast<uint32_t> (head.size()));
        out += head;
        out.append (static_cast<const char*> (data), size);
        return out;
    }

    void Decoder::feed (const void* data, size_t size)
    {
        if (bad)
            return;
        // Drop consumed bytes once in a while so the buffer doesn't grow forever.
        if (readPos > 0 && readPos >= buffer.size() / 2)
        {
            buffer.erase (0, readPos);
            readPos = 0;
        }
        buffer.append (static_cast<const char*> (data), size);
    }

    std::optional<Frame> Decoder::next()
    {
        if (bad || buffer.size() - readPos < 4)
            return std::nullopt;

        const uint32_t length = getU32 (buffer, readPos);
        if (length < 1 || length > kMaxFrameBytes)
        {
            bad = true;
            return std::nullopt;
        }
        if (buffer.size() - readPos < 4 + static_cast<size_t> (length))
            return std::nullopt;

        const size_t start = readPos + 4;
        const char kind = buffer[start];
        readPos = start + length;

        Frame frame;
        if (kind == static_cast<char> (Frame::Kind::json))
        {
            auto parsed = parseJson (buffer.substr (start + 1, length - 1));
            if (! parsed)
            {
                bad = true;
                return std::nullopt;
            }
            frame.kind = Frame::Kind::json;
            frame.json = std::move (*parsed);
            return frame;
        }

        if (kind == static_cast<char> (Frame::Kind::binary) && length >= 5)
        {
            const uint32_t headLength = getU32 (buffer, start + 1);
            if (static_cast<size_t> (headLength) + 5 > length)
            {
                bad = true;
                return std::nullopt;
            }
            auto parsed = parseJson (buffer.substr (start + 5, headLength));
            if (! parsed)
            {
                bad = true;
                return std::nullopt;
            }
            frame.kind = Frame::Kind::binary;
            frame.json = std::move (*parsed);
            frame.data = buffer.substr (start + 5 + headLength, length - 5 - headLength);
            return frame;
        }

        bad = true;
        return std::nullopt;
    }
}
