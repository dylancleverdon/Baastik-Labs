#include "SamplePacks.h"
#include "Updater.h"
#include "engine/sfz/SampleCache.h"

#include <cstdlib>

#include <catch2/catch_test_macros.hpp>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_cryptography/juce_cryptography.h>

using namespace norg::update;

namespace
{
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
        return juce::JSON::toString (juce::var (obj));
    }

    Manifest sampleManifest()
    {
        Manifest m;
        m.schema = 2;
        m.version = "0.1.42";
        m.build = 42;
        m.zipName = "Norg-0.1.42-mac.zip";
        m.zipUrl = juce::String (allowedAssetPrefix) + "v0.1.42/Norg-0.1.42-mac.zip";
        m.sha256 = juce::String::repeatedString ("ab", 32);
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

        int downloads = 0;

        bool download (const juce::String& url, const juce::File& destination, juce::String& error,
                       bool, ProgressFn progress) override
        {
            ++downloads;
            const auto source = juce::URL (url).getLocalFile();
            if (! source.existsAsFile())
            {
                error = "not found: " + url;
                return false;
            }
            if (progress)
                progress (source.getSize());
            return source.copyFileTo (destination);
        }

        bool extractArchive (const juce::File& tarGz, const juce::File& destination, juce::String& error) override
        {
            juce::ChildProcess tar;
            if (! tar.start (juce::StringArray { "tar", "-xzf", tarGz.getFullPathName(), "-C", destination.getFullPathName() }))
                return false;
            error = tar.readAllProcessOutput();
            return tar.waitForProcessToFinish (60000) && tar.getExitCode() == 0;
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
        void removeLaunchAgent (const Layout&) override {}
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

            manifest.schema = 2;
            manifest.version = version;
            manifest.build = build;
            manifest.zipName = zip.getFileName();
            manifest.zipUrl = juce::URL (zip).toString (false);
            manifest.sha256 = sha256OfFile (zip);

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
        Updater updater { layout, platform, ops, [this] (const juce::String& s) { logLines.add (s); } };
    };
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
        m.schema = 1; // the old signed format is no longer accepted
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

    SECTION ("a manifest whose checksum doesn't match the download is rejected")
    {
        auto m = release.manifest;
        m.sha256 = juce::String::repeatedString ("0f", 32);
        release.manifestFile.replaceWithText (manifestJson (m));
        CHECK (h.updater.checkAndInstall (release.options()) == Outcome::failed);
        CHECK (h.updater.lastError().contains ("checksum"));
        CHECK_FALSE (h.layout.installedJson().exists());
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

//==============================================================================
namespace
{
    // A sample pack on disk: a folder with an SFZ, a FLAC and a WAV, tarred up, plus its packs.json.
    struct FakePack
    {
        FakePack (const juce::File& root, int version, bool corruptChecksum = false)
        {
            const auto folder = root.getChildFile ("src").getChildFile ("Test Piano");
            folder.getChildFile ("samples").createDirectory();
            folder.getChildFile ("Test Piano.sfz").replaceWithText ("<region> sample=samples/a.flac key=60\n<region> sample=samples/b.wav key=62\n");
            writeSine (folder.getChildFile ("samples/a.flac"), std::make_unique<juce::FlacAudioFormat>());
            writeSine (folder.getChildFile ("samples/b.wav"), std::make_unique<juce::WavAudioFormat>());

            archive = root.getChildFile ("test-piano-v" + juce::String (version) + ".tar.gz");
            juce::ChildProcess tar;
            REQUIRE (tar.start (juce::StringArray { "tar", "-czf", archive.getFullPathName(), "-C",
                                                    root.getChildFile ("src").getFullPathName(), "Test Piano" }));
            tar.waitForProcessToFinish (60000);
            REQUIRE (archive.existsAsFile());

            auto* pack = new juce::DynamicObject();
            pack->setProperty ("id", "test-piano");
            pack->setProperty ("name", "Test Piano");
            pack->setProperty ("folder", "Test Piano");
            pack->setProperty ("version", version);
            pack->setProperty ("url", juce::URL (archive).toString (false));
            pack->setProperty ("sha256", corruptChecksum ? juce::String::repeatedString ("00", 32) : sha256OfFile (archive));
            pack->setProperty ("size", archive.getSize());
            auto* list = new juce::DynamicObject();
            list->setProperty ("packs", juce::Array<juce::var> { juce::var (pack) });

            listFile = root.getChildFile ("packs-v" + juce::String (version) + ".json");
            listFile.replaceWithText (juce::JSON::toString (juce::var (list)));
        }

        static void writeSine (const juce::File& file, std::unique_ptr<juce::AudioFormat> format)
        {
            juce::AudioBuffer<float> audio (1, 4800);
            for (int i = 0; i < 4800; ++i)
                audio.setSample (0, i, 0.5f * std::sin (0.05f * static_cast<float> (i)));
            std::unique_ptr<juce::OutputStream> out (file.createOutputStream());
            auto writer = format->createWriterFor (out, juce::AudioFormatWriterOptions().withSampleRate (48000.0)
                                                            .withNumChannels (1).withBitsPerSample (16));
            REQUIRE (writer != nullptr);
            writer->writeFromAudioSampleBuffer (audio, 0, 4800);
        }

        juce::String listUrl() const { return juce::URL (listFile).toString (false); }

        juce::File archive, listFile;
    };

    struct PackHarness
    {
        PackHarness()
        {
            cache.dir.getChildFile ("cache").createDirectory();
            setenv ("NORG_SAMPLE_CACHE", cache.dir.getChildFile ("cache").getFullPathName().toRawUTF8(), 1);
        }
        ~PackHarness() { unsetenv ("NORG_SAMPLE_CACHE"); }

        TempHome home, source, cache;
        Layout layout { home.dir };
        FakePlatform platform;
        FileOps ops;
        PackInstaller installer { layout, platform, ops, [] (const juce::String&) {} };
    };
}

TEST_CASE ("pack lists are parsed and bad entries skipped", "[packs]")
{
    juce::String error;
    const auto packs = parsePacks (R"({"packs":[
        {"id":"grand","name":"Grand","folder":"Grand","version":1,"url":"u","sha256":"0000000000000000000000000000000000000000000000000000000000000000","size":5},
        {"id":"../evil","name":"x","folder":"x","version":1,"url":"u","sha256":"0000000000000000000000000000000000000000000000000000000000000000"},
        {"id":"escape","name":"x","folder":"../../x","version":1,"url":"u","sha256":"0000000000000000000000000000000000000000000000000000000000000000"}
    ]})", error);
    REQUIRE (packs.size() == 1);
    CHECK (packs[0].id == "grand");
    CHECK (packs[0].size == 5);
}

TEST_CASE ("sample packs download, prepare and install themselves", "[packs]")
{
    PackHarness h;
    FakePack pack (h.source.dir, 1);

    REQUIRE (h.installer.installAll (pack.listUrl(), { "file://" }, false));

    const auto installed = h.layout.samplesDir().getChildFile ("Test Piano");
    CHECK (installed.getChildFile ("Test Piano.sfz").existsAsFile());
    CHECK_FALSE (h.layout.packsStagingDir().getChildFile ("test-piano").exists());

    // The FLAC was prepared into the cache ahead of time, under the name the plugin will look for.
    CHECK (norg::sfz::cache::fileFor (installed.getChildFile ("samples/a.flac")).existsAsFile());

    const auto status = parsePackStatus (h.layout.packsStatusJson().loadFileAsString());
    REQUIRE (status.size() == 1);
    CHECK (status[0].state == "ready");

    SECTION ("a second run doesn't download it again")
    {
        const int before = h.platform.downloads;
        REQUIRE (h.installer.installAll (pack.listUrl(), { "file://" }, false));
        CHECK (h.platform.downloads == before + 1); // just the list
    }

    SECTION ("a newer version installs only when upgrades are allowed")
    {
        TempHome newer;
        FakePack v2 (newer.dir, 2);
        const int before = h.platform.downloads;
        REQUIRE (h.installer.installAll (v2.listUrl(), { "file://" }, false));
        CHECK (h.platform.downloads == before + 1);

        REQUIRE (h.installer.installAll (v2.listUrl(), { "file://" }, true));
        CHECK (h.platform.downloads == before + 3);
        CHECK (juce::JSON::parse (h.layout.packsStateJson().loadFileAsString())["test-piano"].operator int() == 2);
    }
}

TEST_CASE ("a pack that fails its checksum is not installed", "[packs]")
{
    PackHarness h;
    FakePack pack (h.source.dir, 1, true);

    CHECK_FALSE (h.installer.installAll (pack.listUrl(), { "file://" }, false));
    CHECK_FALSE (h.layout.samplesDir().getChildFile ("Test Piano").exists());
    const auto status = parsePackStatus (h.layout.packsStatusJson().loadFileAsString());
    REQUIRE (status.size() == 1);
    CHECK (status[0].state == "failed");
}

TEST_CASE ("packs must come from Norg's own releases", "[packs]")
{
    PackHarness h;
    FakePack pack (h.source.dir, 1);
    CHECK_FALSE (h.installer.installAll (pack.listUrl(), {}, false)); // file:// not allowed here
    CHECK_FALSE (h.layout.samplesDir().getChildFile ("Test Piano").exists());
}
