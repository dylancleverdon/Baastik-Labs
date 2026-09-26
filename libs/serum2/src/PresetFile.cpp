#include "serum2/PresetFile.h"

#include <cstring>
#include <fstream>
#include <iterator>

#include <zstd.h>

namespace serum2
{
namespace
{
constexpr char kMagic[] = "XferJson"; // 8 chars + the terminating NUL = 9 bytes
constexpr std::size_t kMagicSize = sizeof(kMagic);
constexpr std::uint32_t kPayloadFormat = 2;

std::uint32_t readU32(std::span<const std::uint8_t> bytes, std::size_t offset)
{
    if (offset + 4 > bytes.size())
        throw FormatError("truncated preset header");
    return static_cast<std::uint32_t>(bytes[offset])
         | static_cast<std::uint32_t>(bytes[offset + 1]) << 8
         | static_cast<std::uint32_t>(bytes[offset + 2]) << 16
         | static_cast<std::uint32_t>(bytes[offset + 3]) << 24;
}

void appendU32(Bytes& out, std::uint32_t value)
{
    for (int shift = 0; shift < 32; shift += 8)
        out.push_back(static_cast<std::uint8_t>(value >> shift));
}

struct Sections
{
    std::span<const std::uint8_t> metadata;
    std::span<const std::uint8_t> compressed;
    std::uint32_t cborSize = 0;
};

Sections split(std::span<const std::uint8_t> file)
{
    if (file.size() < kMagicSize || std::memcmp(file.data(), kMagic, kMagicSize) != 0)
        throw FormatError("not a Serum 2 preset (missing XferJson header)");

    std::size_t offset = kMagicSize;
    const auto metaSize = readU32(file, offset);
    offset += 8;
    if (offset + metaSize > file.size())
        throw FormatError("truncated preset metadata");

    Sections s;
    s.metadata = file.subspan(offset, metaSize);
    offset += metaSize;

    s.cborSize = readU32(file, offset);
    const auto format = readU32(file, offset + 4);
    if (format != kPayloadFormat)
        throw FormatError("unsupported payload format " + std::to_string(format));
    offset += 8;
    s.compressed = file.subspan(offset);
    return s;
}

Bytes decompress(std::span<const std::uint8_t> compressed, std::size_t expectedSize)
{
    Bytes out(expectedSize);
    const auto result = ZSTD_decompress(out.data(), out.size(), compressed.data(), compressed.size());
    if (ZSTD_isError(result))
        throw FormatError(std::string("zstd: ") + ZSTD_getErrorName(result));
    if (result != expectedSize)
        throw FormatError("decompressed payload size does not match header");
    return out;
}

Bytes compress(std::span<const std::uint8_t> raw, int level)
{
    Bytes out(ZSTD_compressBound(raw.size()));
    const auto size = ZSTD_compress(out.data(), out.size(), raw.data(), raw.size(), level);
    if (ZSTD_isError(size))
        throw FormatError(std::string("zstd: ") + ZSTD_getErrorName(size));
    out.resize(size);
    return out;
}
} // namespace

Bytes extractCbor(std::span<const std::uint8_t> file)
{
    const auto s = split(file);
    return decompress(s.compressed, s.cborSize);
}

Preset decodePreset(std::span<const std::uint8_t> file)
{
    const auto s = split(file);
    Preset preset;
    try
    {
        preset.metadata = Json::parse(s.metadata.begin(), s.metadata.end());
        const auto cbor = decompress(s.compressed, s.cborSize);
        preset.data = Json::from_cbor(cbor);
    }
    catch (const nlohmann::json::exception& e)
    {
        throw FormatError(std::string("malformed preset: ") + e.what());
    }
    return preset;
}

Bytes encodePreset(const Preset& preset, int compressionLevel)
{
    // nlohmann writes floats as float32 when lossless and float64 otherwise,
    // and integers in their shortest form, which matches Serum's own writer.
    const auto cbor = Json::to_cbor(preset.data);
    const auto compressed = compress(cbor, compressionLevel);

    auto metadata = preset.metadata;
    metadata["hash"] = md5Hex(compressed);
    const auto metaText = metadata.dump(-1, ' ', true);

    Bytes out;
    out.reserve(kMagicSize + 16 + metaText.size() + compressed.size());
    out.insert(out.end(), kMagic, kMagic + kMagicSize);
    appendU32(out, static_cast<std::uint32_t>(metaText.size()));
    appendU32(out, 0);
    out.insert(out.end(), metaText.begin(), metaText.end());
    appendU32(out, static_cast<std::uint32_t>(cbor.size()));
    appendU32(out, kPayloadFormat);
    out.insert(out.end(), compressed.begin(), compressed.end());
    return out;
}

Bytes readFile(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw std::runtime_error("cannot open " + path.string());
    return Bytes(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void writeFile(const std::filesystem::path& path, std::span<const std::uint8_t> bytes)
{
    if (path.has_parent_path())
        std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
        throw std::runtime_error("cannot write " + path.string());
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

Preset readPresetFile(const std::filesystem::path& path)
{
    return decodePreset(readFile(path));
}

void writePresetFile(const std::filesystem::path& path, const Preset& preset)
{
    writeFile(path, encodePreset(preset));
}
} // namespace serum2
