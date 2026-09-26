// MD5 (RFC 1321). Only used to fill in the preset header's "hash" field,
// which Serum computes over the compressed payload.
#include "serum2/PresetFile.h"

#include <array>

namespace serum2
{
namespace
{
constexpr std::array<std::uint32_t, 64> kSine = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
};

constexpr std::array<int, 64> kShift = {
    7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
    5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
    4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
    6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21,
};

std::uint32_t rotl(std::uint32_t x, int c)
{
    return (x << c) | (x >> (32 - c));
}

void processBlock(const std::uint8_t* block, std::array<std::uint32_t, 4>& state)
{
    std::array<std::uint32_t, 16> m {};
    for (std::size_t i = 0; i < 16; ++i)
        m[i] = static_cast<std::uint32_t>(block[i * 4])
             | static_cast<std::uint32_t>(block[i * 4 + 1]) << 8
             | static_cast<std::uint32_t>(block[i * 4 + 2]) << 16
             | static_cast<std::uint32_t>(block[i * 4 + 3]) << 24;

    auto [a, b, c, d] = state;
    for (std::size_t i = 0; i < 64; ++i)
    {
        std::uint32_t f = 0;
        std::size_t g = 0;
        if (i < 16)
        {
            f = (b & c) | (~b & d);
            g = i;
        }
        else if (i < 32)
        {
            f = (d & b) | (~d & c);
            g = (5 * i + 1) % 16;
        }
        else if (i < 48)
        {
            f = b ^ c ^ d;
            g = (3 * i + 5) % 16;
        }
        else
        {
            f = c ^ (b | ~d);
            g = (7 * i) % 16;
        }
        const auto next = d;
        d = c;
        c = b;
        b = b + rotl(a + f + kSine[i] + m[g], kShift[i]);
        a = next;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
}
} // namespace

std::string md5Hex(std::span<const std::uint8_t> bytes)
{
    std::array<std::uint32_t, 4> state = { 0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476 };

    const std::size_t fullBlocks = bytes.size() / 64;
    for (std::size_t i = 0; i < fullBlocks; ++i)
        processBlock(bytes.data() + i * 64, state);

    // Final block(s): remaining bytes, 0x80, zero padding, 64-bit bit length.
    std::array<std::uint8_t, 128> tail {};
    const std::size_t rest = bytes.size() - fullBlocks * 64;
    for (std::size_t i = 0; i < rest; ++i)
        tail[i] = bytes[fullBlocks * 64 + i];
    tail[rest] = 0x80;
    const std::size_t tailSize = rest < 56 ? 64 : 128;
    const std::uint64_t bitLength = static_cast<std::uint64_t>(bytes.size()) * 8;
    for (std::size_t i = 0; i < 8; ++i)
        tail[tailSize - 8 + i] = static_cast<std::uint8_t>(bitLength >> (8 * i));
    for (std::size_t offset = 0; offset < tailSize; offset += 64)
        processBlock(tail.data() + offset, state);

    static constexpr char hex[] = "0123456789abcdef";
    std::string out;
    out.reserve(32);
    for (auto word : state)
        for (int byte = 0; byte < 4; ++byte)
        {
            const auto v = static_cast<std::uint8_t>(word >> (8 * byte));
            out += hex[v >> 4];
            out += hex[v & 0xf];
        }
    return out;
}
} // namespace serum2
