#pragma once

#include "Installer.h"
#include "UpdateCore.h"

#include <functional>

namespace norg::update
{
    // Everything that touches the OS or network, behind an interface so tests can fake it.
    struct Platform
    {
        virtual ~Platform() = default;
        using ProgressFn = std::function<void (juce::int64 bytesSoFar)>;

        // Downloads to `destination`. With `resume`, continues a partial file left by an earlier try.
        virtual bool download (const juce::String& url, const juce::File& destination, juce::String& error,
                               bool resume = false, ProgressFn progress = {}) = 0;
        virtual bool extractArchive (const juce::File& tarGz, const juce::File& destinationDir, juce::String& error) = 0;
        virtual bool extractZip (const juce::File& zip, const juce::File& destinationDir, juce::String& error) = 0;
        virtual bool verifyCodeSignature (const juce::File& item, juce::String& error) = 0;
        virtual void refreshAudioComponents() = 0;
        virtual void notify (const juce::String& title, const juce::String& message) = 0;
        virtual bool installLaunchAgent (const Layout&, juce::String& error) = 0;
        virtual void removeLaunchAgent (const Layout&) = 0;
    };

    // The real implementation: curl, ditto, codesign, launchctl, osascript on macOS.
    std::unique_ptr<Platform> createSystemPlatform();

    enum class Outcome { installed, upToDate, skipped, disabled, failed };

    struct RunOptions
    {
        bool manualCheck = false;
        juce::String manifestUrl = norg::update::manifestUrl;
        juce::StringArray extraAllowedPrefixes; // test hook: e.g. "file://" for local end-to-end runs
    };

    class Updater
    {
    public:
        using LogFn = std::function<void (const juce::String&)>;

        Updater (Layout, Platform&, FileOps&, LogFn);

        Outcome checkAndInstall (const RunOptions&);
        juce::Result rollbackToPrevious();

        Settings loadSettings() const;
        void saveSettings (const Settings&) const;
        std::optional<ReleaseInfo> installedRelease() const;
        juce::String statusJson() const;

        juce::String lastError() const { return error; }

    private:
        Outcome fail (const juce::String& message);
        juce::File findPayloadDir (const juce::File& extracted) const;

        Layout layout;
        Platform& platform;
        FileOps& fileOps;
        LogFn log;
        juce::String error;
    };
}
