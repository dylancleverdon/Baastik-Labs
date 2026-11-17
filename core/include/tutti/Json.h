#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>

namespace tutti
{
    using Json = nlohmann::json;

    // Small readers that never throw: a missing or mistyped field gives the fallback.
    inline std::string str (const Json& j, const char* key, const std::string& fallback = {})
    {
        if (j.is_object())
            if (auto it = j.find (key); it != j.end() && it->is_string())
                return it->get<std::string>();
        return fallback;
    }

    inline int64_t integer (const Json& j, const char* key, int64_t fallback = 0)
    {
        if (j.is_object())
            if (auto it = j.find (key); it != j.end() && it->is_number())
                return it->is_number_float() ? static_cast<int64_t> (it->get<double>()) : it->get<int64_t>();
        return fallback;
    }

    inline uint64_t uinteger (const Json& j, const char* key, uint64_t fallback = 0)
    {
        const auto v = integer (j, key, static_cast<int64_t> (fallback));
        return v < 0 ? fallback : static_cast<uint64_t> (v);
    }

    inline double number (const Json& j, const char* key, double fallback = 0.0)
    {
        if (j.is_object())
            if (auto it = j.find (key); it != j.end() && it->is_number())
                return it->get<double>();
        return fallback;
    }

    inline bool boolean (const Json& j, const char* key, bool fallback = false)
    {
        if (j.is_object())
            if (auto it = j.find (key); it != j.end() && it->is_boolean())
                return it->get<bool>();
        return fallback;
    }

    inline const Json& field (const Json& j, const char* key)
    {
        static const Json null;
        if (j.is_object())
            if (auto it = j.find (key); it != j.end())
                return *it;
        return null;
    }

    // Parses without throwing; nullopt on malformed text.
    inline std::optional<Json> parseJson (const std::string& text)
    {
        auto j = Json::parse (text, nullptr, false);
        if (j.is_discarded())
            return std::nullopt;
        return j;
    }
}
