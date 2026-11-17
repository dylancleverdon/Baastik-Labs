#include "tutti/Ids.h"

#include <array>
#include <cstdint>
#include <random>

namespace tutti
{
    std::string randomId (const std::string& prefix)
    {
        static thread_local std::random_device device;
        const uint64_t value = (static_cast<uint64_t> (device()) << 32) ^ static_cast<uint64_t> (device());

        static constexpr std::array<char, 16> hex { '0', '1', '2', '3', '4', '5', '6', '7',
                                                    '8', '9', 'a', 'b', 'c', 'd', 'e', 'f' };
        std::string out = prefix;
        for (int shift = 60; shift >= 0; shift -= 4)
            out += hex[static_cast<size_t> ((value >> shift) & 0xf)];
        return out;
    }
}
