#include "tts/Content.h"

#include <fstream>

#include "tts/embedded/Resources.h"

namespace tts
{
using serum2::Json;

const Json& Content::builtin()
{
    static const Json bundle = [] {
        const auto text = embedded::find("vocabulary.json");
        if (!text)
            throw std::runtime_error("missing embedded vocabulary.json");
        auto parsed = Json::parse(*text);
#ifdef TTS_CONTENT_VERSION
        // Release builds stamp the workflow run number, so a later
        // content-only release always counts as newer.
        parsed["version"] = TTS_CONTENT_VERSION;
#endif
        return parsed;
    }();
    return bundle;
}

bool Content::isValid(const Json& bundle)
{
    return bundle.is_object() && bundle.value("version", 0) > 0 && bundle.value("minEngine", 1) <= kEngineVersion
        && bundle.contains("descriptors") && bundle["descriptors"].is_object();
}

Json Content::load(const std::filesystem::path& cacheFile)
{
    std::ifstream in(cacheFile, std::ios::binary);
    if (in)
    {
        try
        {
            auto cached = Json::parse(in);
            if (isValid(cached) && cached["version"].get<int>() > builtin()["version"].get<int>())
                return cached;
        }
        catch (const std::exception&)
        {
            // A broken download falls back to the built-in vocabulary.
        }
    }
    return builtin();
}

std::filesystem::path Content::cacheFile(const std::filesystem::path& dataDir)
{
    return dataDir / "content.json";
}
} // namespace tts
