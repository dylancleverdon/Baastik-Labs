#pragma once

#include <string>

namespace tutti
{
    // Random identifiers: a prefix plus 16 lowercase hex digits (64 bits from the OS's CSPRNG).
    // Entity ids made by the Remote Script use the same shape, e.g. "e3fa94c0d1b2a7e65".
    std::string randomId (const std::string& prefix);

    inline std::string newPeerId()    { return randomId ("p"); }
    inline std::string newEntityId()  { return randomId ("e"); }
    inline std::string newSessionId() { return randomId ("s"); }
    inline std::string newVersionId() { return randomId ("v"); }
}
