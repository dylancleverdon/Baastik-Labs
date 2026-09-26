#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace serum2
{
// Key-sorted, like every map Serum writes, so decode + encode reproduces the
// original bytes and generated presets come out in Serum's canonical order.
using Json = nlohmann::json;
using Bytes = std::vector<std::uint8_t>;

class FormatError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

// A decoded .SerumPreset: the small JSON header plus the engine state.
struct Preset
{
    Json metadata;
    Json data;
};

// Container layout (reverse-engineered, see docs/serum2-format.md):
//   "XferJson\0" | u32le metaLen | u32le 0 | metaLen bytes of JSON
//   | u32le cborLen | u32le 2 | zstd frame of the CBOR engine state
// metadata.hash is the MD5 of the zstd frame.
Preset decodePreset(std::span<const std::uint8_t> file);
Bytes encodePreset(const Preset& preset, int compressionLevel = 19);

// The raw (decompressed) CBOR payload, for byte-level round-trip checks.
Bytes extractCbor(std::span<const std::uint8_t> file);

Bytes readFile(const std::filesystem::path& path);
void writeFile(const std::filesystem::path& path, std::span<const std::uint8_t> bytes);

Preset readPresetFile(const std::filesystem::path& path);
void writePresetFile(const std::filesystem::path& path, const Preset& preset);

std::string md5Hex(std::span<const std::uint8_t> bytes);
} // namespace serum2
