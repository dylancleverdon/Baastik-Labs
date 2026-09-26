#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <optional>

namespace baastik
{
// Where one plugin looks for its updates. Each plugin has its own release
// channel (a GitHub release tagged "<pluginId>-latest" holding update.json),
// so publishing one plugin never offers an update for another.
struct UpdaterConfig
{
    juce::String pluginId;    // "serum-preset-generator"
    juce::String displayName; // "Serum Preset Generator"
    Version currentVersion;
    juce::URL manifestUrl;
};

struct ReleaseInfo
{
    Version version;
    juce::String notes;
    juce::URL installerUrl;
    juce::String installerName;
};

// update.json:
// {
//   "plugin": "serum-preset-generator",
//   "version": "0.1.12",
//   "notes": "What changed",
//   "downloads": { "mac": "https://...pkg", "windows": "https://...exe" },
//   ...plugin-specific sections (e.g. "content")...
// }
class UpdateChecker
{
public:
    // update is set only when the channel has a newer, non-skipped version.
    // manifest is the whole update.json (void if the check failed).
    using CheckCallback = std::function<void (std::optional<ReleaseInfo> update, juce::var manifest)>;
    using ProgressCallback = std::function<void (float)>;
    using DoneCallback = std::function<void (bool ok, juce::String message)>;

    explicit UpdateChecker (UpdaterConfig config);
    ~UpdateChecker();

    const UpdaterConfig& config() const { return config_; }

    // Fetches the manifest on a background thread; calls back on the message thread.
    void checkNow (CheckCallback callback);
    // Checks when auto-update is on and the last check is older than minInterval.
    void checkIfDue (CheckCallback callback, juce::RelativeTime minInterval = juce::RelativeTime::hours (1));

    // Downloads the installer to the temp folder and opens it.
    void downloadAndInstall (const ReleaseInfo& release, ProgressCallback progress, DoneCallback done);

    bool isAutoCheckEnabled();
    void setAutoCheckEnabled (bool enabled);
    void skipVersion (const Version& version);

    // Per-user settings file shared by this plugin's instances.
    juce::PropertiesFile& settings();

    static juce::String platformKey(); // "mac", "windows" or "linux"
    static bool launchInstaller (const juce::File& installer);

private:
    std::optional<ReleaseInfo> parseRelease (const juce::var& manifest);

    UpdaterConfig config_;
    std::unique_ptr<juce::PropertiesFile> settings_;
    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>> (true);
    std::unique_ptr<juce::URL::DownloadTask> download_;
    std::unique_ptr<juce::URL::DownloadTaskListener> downloadListener_;
};
} // namespace baastik
