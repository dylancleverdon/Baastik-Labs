#include "Updater.h"
#include "UpdateCanonical.h"

#include <catch2/catch_test_macros.hpp>
#include <juce_cryptography/juce_cryptography.h>
#include <monocypher-ed25519.h>

using namespace norg::update;

namespace
{
    // A throwaway key pair for tests, derived from a fixed seed.
    struct TestKey
    {
        TestKey()
        {
            uint8_t seed[32];
            for (int i = 0; i < 32; ++i)
                seed[i] = static_cast<uint8_t> (i * 7 + 3);
            crypto_ed25519_key_pair (secret, publicKey, seed);
        }

        juce::String publicHex() const { return toHex (publicKey, 32); }

        juce::String sign (const Manifest& m) const
        {
            const auto msg = canonicalMessage (m.version.toStdString(), m.build, m.zipName.toStdString(),
                                               m.zipUrl.toStdString(), m.sha256.toStdString());
            uint8_t sig[64];
            crypto_ed25519_sign (sig, secret, reinterpret_cast<const uint8_t*> (msg.data()), msg.size());
            return toHex (sig, 64);
        }

        uint8_t secret[64] {}, publicKey[32] {};
    };

    const TestKey& testKey()
    {
        static TestKey key;
        return key;
    }

    juce::String manifestJson (const Manifest& m)
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty ("schema", m.schema);
        obj->setProperty ("version", m.version);
        obj->setProperty ("build", m.build);
        obj->setProperty ("commit", m.commit);
        obj->setProperty ("notes", m.notes);
        obj->setProperty ("zipName", m.zipName);
        obj->setProperty ("zipUrl", m.zipUrl);
        obj->setProperty ("sha256", m.sha256);
        obj->setProperty ("signature", m.signature);
        return juce::JSON::toString (juce::var (obj));
    }

    Manifest sampleManifest()
    {
        Manifest m;
        m.schema = 1;
        m.version = "0.1.42";
        m.build = 42;
        m.zipName = "Norg-0.1.42-mac.zip";
        m.zipUrl = juce::String (allowedAssetPrefix) + "v0.1.42/Norg-0.1.42-mac.zip";
        m.sha256 = juce::String::repeatedString ("ab", 32);
        m.signature = testKey().sign (m);
        return m;
    }

    // A temporary "home folder" that is deleted afterwards.
    struct TempHome
    {
        TempHome() : dir (juce::File::createTempFile ("norg-home"))
        {
            dir.createDirectory();
        }
        ~TempHome() { dir.deleteRecursively(); }

        juce::File dir;
    };

    // Creates a fake bundle folder with a marker file saying which version it is.
    void makeBundle (const juce::File& bundle, const juce::String& marker)
    {
        bundle.getChildFile ("Contents").createDirectory();
        bundle.getChildFile ("Contents/marker.txt").replaceWithText (marker);
    }

    juce::String markerOf (const juce::File& bundle)
    {
        return bundle.getChildFile ("Contents/marker.txt").loadFileAsString();
    }

    void makePayload (const juce::File& dir, const juce::String& version, juce::int64 build)
    {
        makeBundle (dir.getChildFile ("Norg.component"), "component " + version);
        makeBundle (dir.getChildFile ("Norg.vst3"), "vst3 " + version);
        makeBundle (dir.getChildFile ("Norg.app"), "app " + version);
        dir.getChildFile ("NorgUpdater").replaceWithText ("updater " + version);

        ReleaseInfo info;
        info.version = version;
        info.build = build;
        info.notes = "- notes for " + version;
        dir.getChildFile ("release.json").replaceWithText (toJson (info));
    }

    // Fails the Nth call to move(), to prove partial installs are undone.
    struct FailingFileOps final : FileOps
    {
        int failOnMove = -1;
        int moves = 0;

        bool move (const juce::File& from, const juce::File& to) override
        {
            if (moves++ == failOnMove)
                return false;
            return FileOps::move (from, to);
        }
    };

    // Serves file:// URLs from disk; everything else is a no-op.
    struct FakePlatform final : Platform
    {
        int notifications = 0;

        bool download (const juce::String& url, const juce::File& destination, juce::String& error) override
        {
            const auto source = juce::URL (url).getLocalFile();
            if (! source.existsAsFile())
            {
                error = "not found: " + url;
                return false;
            }
            return source.copyFileTo (destination);
        }

        bool extractZip (const juce::File& zip, const juce::File& destination, juce::String& error) override
        {
            juce::ZipFile archive (zip);
            const auto result = archive.uncompressTo (destination);
            error = result.getErrorMessage();
            return result.wasOk();
        }

        bool verifyCodeSignature (const juce::File&, juce::String&) override { return true; }
        void refreshAudioComponents() override {}
        void notify (const juce::String&, const juce::String&) override { ++notifications; }
        bool installLaunchAgent (const Layout&, juce::String&) override { return true; }
    };

    // A published "release" on disk: a signed manifest plus the zip it points at.
    struct FakeRelease
    {
        FakeRelease (const juce::File& root, const juce::String& version, juce::int64 build)
        {
            const auto folderName = "Norg-" + version;
            const auto payload = root.getChildFile ("build-" + juce::String (build)).getChildFile (folderName);
            payload.createDirectory();
            makePayload (payload, version, build);

            zip = root.getChildFile ("Norg-" + version + "-mac.zip");
            juce::ZipFile::Builder builder;
            for (const auto& f : payload.findChildFiles (juce::File::findFiles, true))
                builder.addFile (f, 5, folderName + "/" + f.getRelativePathFrom (payload).replaceCharacter ('\\', '/'));
            {
                juce::FileOutputStream out (zip);
                builder.writeToStream (out, nullptr);
            }

            manifest.schema = 1;
            manifest.version = version;
            manifest.build = build;
            manifest.zipName = zip.getFileName();
            manifest.zipUrl = juce::URL (zip).toString (false);
            manifest.sha256 = sha256OfFile (zip);
            manifest.signature = testKey().sign (manifest);

            manifestFile = root.getChildFile ("norg-update-" + juce::String (build) + ".json");
            manifestFile.replaceWithText (manifestJson (manifest));
        }

        RunOptions options (bool manual = false) const
        {
            RunOptions o;
            o.manualCheck = manual;
            o.manifestUrl = juce::URL (manifestFile).toString (false);
            o.extraAllowedPrefixes.add ("file://");
            return o;
        }

        juce::File zip, manifestFile;
        Manifest manifest;
    };

    struct Harness
    {
        TempHome home;
        TempHome releases;
        Layout layout { home.dir };
        FakePlatform platform;
        FileOps ops;
        juce::StringArray logLines;
        Updater updater { layout, platform, ops, testKey().publicHex(),
                          [this] (const juce::String& s) { logLines.add (s); } };
    };
}

TEST_CASE ("canonical message format is stable", "[updater]")
{
    CHECK (canonicalMessage ("0.1.2", 2, "Norg-0.1.2-mac.zip", "https://x/y.zip", "ff")
           == "norg-update-v1\nversion=0.1.2\nbuild=2\nzip=Norg-0.1.2-mac.zip\nurl=https://x/y.zip\nsha256=ff\n");
}

TEST_CASE ("manifest parsing", "[updater]")
{
    juce::String error;

    SECTION ("a well-formed manifest parses")
    {
        const auto parsed = parseManifest (manifestJson (sampleManifest()), error);
        REQUIRE (parsed.has_value());
        CHECK (parsed->build == 42);
        CHECK (parsed->version == "0.1.42");
    }

    SECTION ("bad fields are rejected")
    {
        auto m = sampleManifest();
        m.schema = 2;
        CHECK_FALSE (parseManifest (manifestJson (m), error).has_value());

        m = sampleManifest();
        m.zipName = "../evil.zip";
        CHECK_FALSE (parseManifest (manifestJson (m), error).has_value());

        m = sampleManifest();
        m.sha256 = "1234";
        CHECK_FALSE (parseManifest (manifestJson (m), error).has_value());

        CHECK_FALSE (parseManifest ("not json", error).has_value());
    }
}

TEST_CASE ("only norg-* release URLs from this repo are accepted", "[updater]")
{
    CHECK (isAllowedAssetUrl ("https://github.com/dylancleverdon/Baastik-Labs/releases/download/norg-v0.1.5/Norg-0.1.5-mac.zip"));
    CHECK_FALSE (isAllowedAssetUrl ("https://github.com/dylancleverdon/Baastik-Labs/releases/download/v1.0/Other.zip"));
    CHECK_FALSE (isAllowedAssetUrl ("https://github.com/someone-else/Baastik-Labs/releases/download/norg-v1/Norg.zip"));
    CHECK_FALSE (isAllowedAssetUrl ("https://evil.example/releases/download/norg-v1/Norg.zip"));
    CHECK_FALSE (isAllowedAssetUrl ("https://github.com/dylancleverdon/Baastik-Labs/releases/download/norg-v1/../../x.zip"));
    CHECK_FALSE (isAllowedAssetUrl ("file:///tmp/Norg.zip"));
    CHECK (isAllowedAssetUrl ("file:///tmp/Norg.zip", { "file://" }));
}

TEST_CASE ("manifest signatures", "[updater]")
{
    const auto key = testKey().publicHex();
    auto m = sampleManifest();
    CHECK (verifyManifestSignature (m, key));

    SECTION ("tampering with any signed field breaks the signature")
    {
        auto t = m; t.version = "0.1.43";                 CHECK_FALSE (verifyManifestSignature (t, key));
        t = m;      t.build = 43;                         CHECK_FALSE (verifyManifestSignature (t, key));
        t = m;      t.zipUrl = t.zipUrl + "x";            CHECK_FALSE (verifyManifestSignature (t, key));
        t = m;      t.sha256 = juce::String::repeatedString ("cd", 32); CHECK_FALSE (verifyManifestSignature (t, key));
    }

    SECTION ("wrong, missing or all-zero keys never verify")
    {
        CHECK_FALSE (verifyManifestSignature (m, juce::String::repeatedString ("11", 32)));
        CHECK_FALSE (verifyManifestSignature (m, juce::String::repeatedString ("00", 32)));
        CHECK_FALSE (verifyManifestSignature (m, ""));
    }
}

TEST_CASE ("install decisions", "[updater]")
{
    const auto m = sampleManifest(); // build 42
    ReleaseInfo installed;
    installed.version = "0.1.41";
    installed.build = 41;

    Settings on;
    Settings off;
    off.autoUpdate = false;
    Settings skip42;
    skip42.skipBuild = 42;

    CHECK (decide (m, installed, on, false) == Decision::install);
    CHECK (decide (m, std::nullopt, on, false) == Decision::install);
    CHECK (decide (m, installed, off, false) == Decision::disabled);
    CHECK (decide (m, installed, off, true) == Decision::install);
    CHECK (decide (m, installed, skip42, false) == Decision::skipped);
    CHECK (decide (m, installed, skip42, true) == Decision::install);

    installed.build = 42;
    CHECK (decide (m, installed, on, true) == Decision::upToDate);
}

TEST_CASE ("swapIn installs, keeps the previous version, and undoes a failure", "[updater]")
{
    TempHome home, staging;
    Layout layout (home.dir);

    // An existing install at 0.1.1
    for (const auto& item : layout.items())
        item.destination.getParentDirectory().createDirectory();
    makeBundle (layout.componentsDir().getChildFile ("Norg.component"), "component 0.1.1");
    makeBundle (layout.vst3Dir().getChildFile ("Norg.vst3"), "vst3 0.1.1");
    layout.supportDir().createDirectory();
    layout.installedJson().replaceWithText (toJson (ReleaseInfo { "0.1.1", 1, {}, {}, {} }));

    const auto payload = staging.dir.getChildFile ("Norg-0.1.2");
    payload.createDirectory();
    makePayload (payload, "0.1.2", 2);

    SECTION ("a successful swap")
    {
        FileOps ops;
        REQUIRE (swapIn (layout, payload, ops).wasOk());
        CHECK (markerOf (layout.componentsDir().getChildFile ("Norg.component")) == "component 0.1.2");
        CHECK (markerOf (layout.vst3Dir().getChildFile ("Norg.vst3")) == "vst3 0.1.2");
        CHECK (markerOf (layout.appsDir().getChildFile ("Norg.app")) == "app 0.1.2");
        CHECK (layout.updaterExecutable().loadFileAsString() == "updater 0.1.2");
        CHECK (parseReleaseInfo (layout.installedJson().loadFileAsString())->build == 2);

        CHECK (markerOf (layout.previousDir().getChildFile ("Norg.component")) == "component 0.1.1");
        CHECK (parseReleaseInfo (layout.previousDir().getChildFile ("installed.json").loadFileAsString())->build == 1);

        SECTION ("rollback swaps back, and a second rollback rolls forward again")
        {
            REQUIRE (rollback (layout, ops).wasOk());
            CHECK (markerOf (layout.componentsDir().getChildFile ("Norg.component")) == "component 0.1.1");
            CHECK (parseReleaseInfo (layout.installedJson().loadFileAsString())->build == 1);

            REQUIRE (rollback (layout, ops).wasOk());
            CHECK (markerOf (layout.componentsDir().getChildFile ("Norg.component")) == "component 0.1.2");
            CHECK (parseReleaseInfo (layout.installedJson().loadFileAsString())->build == 2);
        }
    }

    SECTION ("a failure part-way through leaves the old install untouched")
    {
        for (int failAt = 0; failAt < 6; ++failAt)
        {
            FailingFileOps ops;
            ops.failOnMove = failAt;
            REQUIRE (swapIn (layout, payload, ops).failed());

            CHECK (markerOf (layout.componentsDir().getChildFile ("Norg.component")) == "component 0.1.1");
            CHECK (markerOf (layout.vst3Dir().getChildFile ("Norg.vst3")) == "vst3 0.1.1");
            CHECK_FALSE (layout.appsDir().getChildFile ("Norg.app").exists());
            CHECK (parseReleaseInfo (layout.installedJson().loadFileAsString())->build == 1);
            // The payload is put back, so the next attempt can use it.
            CHECK (markerOf (payload.getChildFile ("Norg.component")) == "component 0.1.2");
        }
    }
}

TEST_CASE ("end-to-end update run with a fake network", "[updater]")
{
    Harness h;
    FakeRelease release (h.releases.dir, "0.1.7", 7);

    SECTION ("installs a new version, then reports up to date")
    {
        CHECK (h.updater.checkAndInstall (release.options()) == Outcome::installed);
        CHECK (markerOf (h.layout.componentsDir().getChildFile ("Norg.component")) == "component 0.1.7");
        CHECK (h.updater.installedRelease()->build == 7);
        CHECK (h.platform.notifications == 1);
        CHECK_FALSE (h.layout.stagingDir().exists());

        CHECK (h.updater.checkAndInstall (release.options()) == Outcome::upToDate);

        SECTION ("rolling back skips that build on automatic checks")
        {
            FakeRelease newer (h.releases.dir, "0.1.8", 8);
            REQUIRE (h.updater.checkAndInstall (newer.options()) == Outcome::installed);
            REQUIRE (h.updater.rollbackToPrevious().wasOk());
            CHECK (h.updater.installedRelease()->build == 7);
            CHECK (h.updater.checkAndInstall (newer.options()) == Outcome::skipped);
            CHECK (h.updater.checkAndInstall (newer.options (true)) == Outcome::installed);
        }
    }

    SECTION ("a tampered download is rejected and nothing changes")
    {
        release.zip.appendText ("tampered");
        CHECK (h.updater.checkAndInstall (release.options()) == Outcome::failed);
        CHECK_FALSE (h.layout.componentsDir().getChildFile ("Norg.component").exists());
        CHECK_FALSE (h.layout.installedJson().exists());
    }

    SECTION ("a manifest signed with another key is rejected")
    {
        auto m = release.manifest;
        m.signature = juce::String::repeatedString ("0f", 64);
        release.manifestFile.replaceWithText (manifestJson (m));
        CHECK (h.updater.checkAndInstall (release.options()) == Outcome::failed);
        CHECK (h.updater.lastError().contains ("signature"));
    }

    SECTION ("a manifest pointing outside Norg's releases is rejected")
    {
        auto options = release.options();
        options.extraAllowedPrefixes.clear();
        CHECK (h.updater.checkAndInstall (options) == Outcome::failed);
        CHECK_FALSE (h.layout.installedJson().exists());
    }

    SECTION ("auto-update off means automatic runs do nothing, but Check now still works")
    {
        Settings s;
        s.autoUpdate = false;
        h.updater.saveSettings (s);
        CHECK (h.updater.checkAndInstall (release.options()) == Outcome::disabled);
        CHECK (h.updater.checkAndInstall (release.options (true)) == Outcome::installed);
    }
}
