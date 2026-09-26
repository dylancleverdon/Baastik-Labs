#include "Updater.h"

namespace norg::update
{
    Updater::Updater (Layout l, Platform& p, FileOps& ops, juce::String key, LogFn logFn)
        : layout (std::move (l)), platform (p), fileOps (ops), publicKey (std::move (key)), log (std::move (logFn))
    {
    }

    Settings Updater::loadSettings() const
    {
        return parseSettings (layout.settingsJson().loadFileAsString());
    }

    void Updater::saveSettings (const Settings& s) const
    {
        layout.supportDir().createDirectory();
        layout.settingsJson().replaceWithText (toJson (s));
    }

    std::optional<ReleaseInfo> Updater::installedRelease() const
    {
        return parseReleaseInfo (layout.installedJson().loadFileAsString());
    }

    juce::String Updater::statusJson() const
    {
        auto* obj = new juce::DynamicObject();
        if (const auto installed = installedRelease())
            obj->setProperty ("installed", juce::JSON::parse (toJson (*installed)));
        obj->setProperty ("settings", juce::JSON::parse (toJson (loadSettings())));
        obj->setProperty ("previousAvailable", hasPrevious (layout));
        return juce::JSON::toString (juce::var (obj));
    }

    Outcome Updater::fail (const juce::String& message)
    {
        error = message;
        log ("update failed: " + message);
        return Outcome::failed;
    }

    juce::File Updater::findPayloadDir (const juce::File& extracted) const
    {
        const auto containsItems = [this] (const juce::File& dir)
        {
            for (const auto& item : layout.items())
                if (dir.getChildFile (item.name).exists())
                    return true;
            return false;
        };

        if (containsItems (extracted))
            return extracted;

        const auto children = extracted.findChildFiles (juce::File::findDirectories, false);
        for (const auto& child : children)
            if (containsItems (child))
                return child;

        return {};
    }

    Outcome Updater::checkAndInstall (const RunOptions& options)
    {
        error = {};
        const auto settings = loadSettings();

        if (! options.manualCheck && ! settings.autoUpdate)
        {
            log ("auto-update is off; not checking");
            return Outcome::disabled;
        }

        const auto staging = layout.stagingDir();
        fileOps.removeRecursively (staging);
        if (! staging.createDirectory())
            return fail ("could not create " + staging.getFullPathName());

        const auto cleanup = [this, staging] { fileOps.removeRecursively (staging); };

        // 1. Manifest
        const auto manifestFile = staging.getChildFile ("norg-update.json");
        juce::String err;
        if (! platform.download (options.manifestUrl, manifestFile, err))
        {
            cleanup();
            return fail ("could not download the update manifest: " + err);
        }

        const auto manifest = parseManifest (manifestFile.loadFileAsString(), err);
        if (! manifest)
        {
            cleanup();
            return fail ("bad manifest: " + err);
        }

        if (! isAllowedAssetUrl (manifest->zipUrl, options.extraAllowedPrefixes))
        {
            cleanup();
            return fail ("manifest points at a URL outside Norg's releases: " + manifest->zipUrl);
        }

        if (! verifyManifestSignature (*manifest, publicKey))
        {
            cleanup();
            return fail ("manifest signature is not valid; ignoring this update");
        }

        // 2. Decide
        switch (decide (*manifest, installedRelease(), settings, options.manualCheck))
        {
            case Decision::disabled: cleanup(); return Outcome::disabled;
            case Decision::upToDate: cleanup(); log ("up to date (" + manifest->version + ")"); return Outcome::upToDate;
            case Decision::skipped:  cleanup(); log ("skipping rolled-back build " + manifest->version); return Outcome::skipped;
            case Decision::install:  break;
        }

        log ("downloading Norg " + manifest->version);

        // 3. Download + verify the zip
        const auto zip = staging.getChildFile (manifest->zipName);
        if (! platform.download (manifest->zipUrl, zip, err))
        {
            cleanup();
            return fail ("could not download the update: " + err);
        }

        if (sha256OfFile (zip) != manifest->sha256)
        {
            cleanup();
            return fail ("downloaded update does not match its signed checksum");
        }

        // 4. Extract and check what's inside
        const auto extracted = staging.getChildFile ("payload");
        if (! extracted.createDirectory() || ! platform.extractZip (zip, extracted, err))
        {
            cleanup();
            return fail ("could not unpack the update: " + err);
        }

        const auto payload = findPayloadDir (extracted);
        if (payload == juce::File())
        {
            cleanup();
            return fail ("the update archive has no Norg files in it");
        }

        if (const auto release = parseReleaseInfo (payload.getChildFile ("release.json").loadFileAsString()))
        {
            if (release->build != manifest->build)
            {
                cleanup();
                return fail ("update contents are for a different build than the manifest");
            }
        }

        for (const auto& item : layout.items())
        {
            const auto candidate = payload.getChildFile (item.name);
            if (candidate.exists() && ! platform.verifyCodeSignature (candidate, err))
            {
                cleanup();
                return fail (item.name + " failed its code-signature check: " + err);
            }
        }

        // 5. Swap it in
        const auto result = swapIn (layout, payload, fileOps);
        cleanup();
        if (result.failed())
            return fail (result.getErrorMessage());

        platform.refreshAudioComponents();
        platform.notify ("Norg " + manifest->version + " installed",
                         "Restart your DAW to use the new version.");
        log ("installed Norg " + manifest->version + " (build " + juce::String (manifest->build) + ")");
        return Outcome::installed;
    }

    juce::Result Updater::rollbackToPrevious()
    {
        const auto before = installedRelease();
        const auto result = rollback (layout, fileOps);
        if (result.failed())
        {
            log ("rollback failed: " + result.getErrorMessage());
            return result;
        }

        // Don't let the next automatic check reinstall the build we just rolled back from.
        auto settings = loadSettings();
        settings.skipBuild = before ? before->build : 0;
        saveSettings (settings);

        platform.refreshAudioComponents();
        const auto now = installedRelease();
        log ("rolled back to " + (now ? now->version : juce::String ("previous version")));
        platform.notify ("Norg rolled back", "Now on " + (now ? now->version : juce::String ("the previous version"))
                                                 + ". Restart your DAW to use it.");
        return result;
    }
}
