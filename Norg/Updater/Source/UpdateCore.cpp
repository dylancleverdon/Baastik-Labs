#include "UpdateCore.h"
#include "UpdateCanonical.h"

#include <juce_cryptography/juce_cryptography.h>
#include <monocypher-ed25519.h>

namespace norg::update
{
    namespace
    {
        juce::String getString (const juce::var& obj, const char* key)
        {
            const auto& v = obj[key];
            return v.isVoid() ? juce::String() : v.toString();
        }

        juce::int64 getInt (const juce::var& obj, const char* key)
        {
            const auto& v = obj[key];
            if (v.isString())
                return v.toString().getLargeIntValue();
            return v.isVoid() ? 0 : static_cast<juce::int64> (v);
        }

        bool isHex (const juce::String& s)
        {
            return s.isNotEmpty() && s.containsOnly ("0123456789abcdefABCDEF");
        }
    }

    std::optional<Manifest> parseManifest (const juce::String& json, juce::String& error)
    {
        const auto parsed = juce::JSON::parse (json);
        if (! parsed.isObject())
        {
            error = "manifest is not a JSON object";
            return std::nullopt;
        }

        Manifest m;
        m.schema      = static_cast<int> (getInt (parsed, "schema"));
        m.version     = getString (parsed, "version");
        m.build       = getInt (parsed, "build");
        m.commit      = getString (parsed, "commit");
        m.notes       = getString (parsed, "notes");
        m.zipName     = getString (parsed, "zipName");
        m.zipUrl      = getString (parsed, "zipUrl");
        m.sha256      = getString (parsed, "sha256").toLowerCase();
        m.signature   = getString (parsed, "signature").toLowerCase();
        m.publishedAt = getString (parsed, "publishedAt");

        if (m.schema != 1)                         error = "unsupported manifest schema " + juce::String (m.schema);
        else if (m.version.isEmpty())              error = "manifest has no version";
        else if (m.build <= 0)                     error = "manifest has no build number";
        else if (! isSafeZipName (m.zipName))      error = "manifest zip name is not allowed";
        else if (m.sha256.length() != 64 || ! isHex (m.sha256))       error = "manifest sha256 is malformed";
        else if (m.signature.length() != 128 || ! isHex (m.signature)) error = "manifest signature is malformed";

        if (error.isNotEmpty())
            return std::nullopt;

        return m;
    }

    std::optional<ReleaseInfo> parseReleaseInfo (const juce::String& json)
    {
        const auto parsed = juce::JSON::parse (json);
        if (! parsed.isObject())
            return std::nullopt;

        ReleaseInfo r;
        r.version = getString (parsed, "version");
        r.build   = getInt (parsed, "build");
        r.commit  = getString (parsed, "commit");
        r.notes   = getString (parsed, "notes");
        r.date    = getString (parsed, "date");

        if (r.version.isEmpty() || r.build <= 0)
            return std::nullopt;

        return r;
    }

    juce::String toJson (const ReleaseInfo& r)
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty ("version", r.version);
        obj->setProperty ("build", r.build);
        obj->setProperty ("commit", r.commit);
        obj->setProperty ("notes", r.notes);
        obj->setProperty ("date", r.date);
        return juce::JSON::toString (juce::var (obj));
    }

    Settings parseSettings (const juce::String& json)
    {
        Settings s;
        const auto parsed = juce::JSON::parse (json);
        if (parsed.isObject())
        {
            if (parsed.hasProperty ("autoUpdate"))
                s.autoUpdate = static_cast<bool> (parsed["autoUpdate"]);
            s.skipBuild = getInt (parsed, "skipBuild");
        }
        return s;
    }

    juce::String toJson (const Settings& s)
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty ("autoUpdate", s.autoUpdate);
        obj->setProperty ("skipBuild", s.skipBuild);
        return juce::JSON::toString (juce::var (obj));
    }

    bool isAllowedAssetUrl (const juce::String& url, const juce::StringArray& extraAllowedPrefixes)
    {
        if (url.contains ("..") || url.containsAnyOf (" \t\r\n\"'\\"))
            return false;

        if (url.startsWith (allowedAssetPrefix))
            return true;

        for (const auto& prefix : extraAllowedPrefixes)
            if (prefix.isNotEmpty() && url.startsWith (prefix))
                return true;

        return false;
    }

    bool isSafeZipName (const juce::String& name)
    {
        return name.startsWith ("Norg-")
            && name.endsWith (".zip")
            && name.length() < 128
            && name.containsOnly ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-")
            && ! name.contains ("..");
    }

    std::optional<juce::MemoryBlock> fromHex (const juce::String& hex)
    {
        if (hex.length() % 2 != 0 || (hex.isNotEmpty() && ! isHex (hex)))
            return std::nullopt;

        juce::MemoryBlock block (static_cast<size_t> (hex.length() / 2));
        auto* bytes = static_cast<juce::uint8*> (block.getData());

        for (int i = 0; i < hex.length() / 2; ++i)
            bytes[i] = static_cast<juce::uint8> (hex.substring (i * 2, i * 2 + 2).getHexValue32());

        return block;
    }

    juce::String toHex (const void* data, size_t size)
    {
        return juce::String::toHexString (data, static_cast<int> (size), 0).toLowerCase();
    }

    juce::String sha256OfFile (const juce::File& file)
    {
        juce::FileInputStream in (file);
        if (! in.openedOk())
            return {};

        return juce::SHA256 (in).toHexString().toLowerCase();
    }

    bool verifyManifestSignature (const Manifest& m, const juce::String& publicKeyHex)
    {
        const auto key = fromHex (publicKeyHex);
        const auto sig = fromHex (m.signature);

        if (! key || key->getSize() != 32 || ! sig || sig->getSize() != 64)
            return false;

        // An all-zero key means the build was made without a real key: never trust it.
        bool allZero = true;
        for (size_t i = 0; i < key->getSize(); ++i)
            allZero = allZero && static_cast<const juce::uint8*> (key->getData())[i] == 0;
        if (allZero)
            return false;

        const auto message = canonicalMessage (m.version.toStdString(), m.build, m.zipName.toStdString(),
                                               m.zipUrl.toStdString(), m.sha256.toStdString());

        return crypto_ed25519_check (static_cast<const uint8_t*> (sig->getData()),
                                     static_cast<const uint8_t*> (key->getData()),
                                     reinterpret_cast<const uint8_t*> (message.data()),
                                     message.size()) == 0;
    }

    Decision decide (const Manifest& m, const std::optional<ReleaseInfo>& installed,
                     const Settings& settings, bool manualCheck)
    {
        if (! manualCheck && ! settings.autoUpdate)
            return Decision::disabled;

        if (installed && m.build <= installed->build)
            return Decision::upToDate;

        if (! manualCheck && settings.skipBuild != 0 && m.build == settings.skipBuild)
            return Decision::skipped;

        return Decision::install;
    }
}
