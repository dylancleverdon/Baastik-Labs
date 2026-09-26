namespace baastik
{
namespace
{
constexpr auto kAutoCheck = "autoCheckUpdates";
constexpr auto kLastCheck = "lastUpdateCheck";
constexpr auto kSkipped = "skippedVersion";

// Runs fn on the message thread unless the owner has been destroyed.
void onMessageThread (std::shared_ptr<std::atomic<bool>> alive, std::function<void()> fn)
{
    juce::MessageManager::callAsync ([alive = std::move (alive), fn = std::move (fn)] {
        if (alive->load())
            fn();
    });
}
} // namespace

UpdateChecker::UpdateChecker (UpdaterConfig config)
    : config_ (std::move (config))
{
}

UpdateChecker::~UpdateChecker()
{
    alive_->store (false);
    download_.reset();
}

juce::PropertiesFile& UpdateChecker::settings()
{
    if (settings_ == nullptr)
    {
        juce::PropertiesFile::Options options;
        options.applicationName = config_.displayName;
        options.folderName = "Baastik Labs";
        options.filenameSuffix = ".settings";
        options.osxLibrarySubFolder = "Application Support";
        settings_ = std::make_unique<juce::PropertiesFile> (options);
    }
    return *settings_;
}

bool UpdateChecker::isAutoCheckEnabled()
{
    return settings().getBoolValue (kAutoCheck, true);
}

void UpdateChecker::setAutoCheckEnabled (bool enabled)
{
    settings().setValue (kAutoCheck, enabled);
    settings().saveIfNeeded();
}

void UpdateChecker::skipVersion (const Version& version)
{
    settings().setValue (kSkipped, version.toString());
    settings().saveIfNeeded();
}

juce::String UpdateChecker::platformKey()
{
#if JUCE_MAC
    return "mac";
#elif JUCE_WINDOWS
    return "windows";
#else
    return "linux";
#endif
}

std::optional<ReleaseInfo> UpdateChecker::parseRelease (const juce::var& manifest)
{
    if (! manifest.isObject())
        return std::nullopt;
    const auto plugin = manifest.getProperty ("plugin", {}).toString();
    if (plugin.isNotEmpty() && plugin != config_.pluginId)
        return std::nullopt; // someone else's channel

    ReleaseInfo info;
    info.version = Version::parse (manifest.getProperty ("version", {}).toString());
    info.notes = manifest.getProperty ("notes", {}).toString();
    const auto url = manifest.getProperty ("downloads", {}).getProperty (platformKey(), {}).toString();
    if (url.isEmpty())
        return std::nullopt;
    info.installerUrl = juce::URL (url);
    info.installerName = url.fromLastOccurrenceOf ("/", false, false);

    if (! (config_.currentVersion < info.version))
        return std::nullopt;
    if (Version::parse (settings().getValue (kSkipped)) == info.version)
        return std::nullopt;
    return info;
}

void UpdateChecker::checkNow (CheckCallback callback)
{
    auto alive = alive_;
    const auto url = config_.manifestUrl;
    juce::Thread::launch ([this, alive, url, callback = std::move (callback)] {
        juce::var manifest;
        const auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                 .withConnectionTimeoutMs (10000)
                                 .withNumRedirectsToFollow (5);
        if (auto stream = url.createInputStream (options))
            manifest = juce::JSON::parse (stream->readEntireStreamAsString());

        onMessageThread (alive, [this, manifest, callback] {
            // Only a successful check counts, so an offline start retries next time.
            if (manifest.isObject())
            {
                settings().setValue (kLastCheck, juce::Time::getCurrentTime().toMilliseconds());
                settings().saveIfNeeded();
            }
            callback (parseRelease (manifest), manifest);
        });
    });
}

void UpdateChecker::checkIfDue (CheckCallback callback, juce::RelativeTime minInterval)
{
    if (! isAutoCheckEnabled())
        return;
    const auto last = juce::Time (settings().getValue (kLastCheck, "0").getLargeIntValue());
    if (juce::Time::getCurrentTime() - last < minInterval)
        return;
    checkNow (std::move (callback));
}

bool UpdateChecker::launchInstaller (const juce::File& installer)
{
    // .pkg opens in macOS Installer; .exe runs the Windows setup (which asks
    // for admin rights itself). Either way the DAW picks the new version up on
    // its next launch.
    return installer.startAsProcess();
}

void UpdateChecker::downloadAndInstall (const ReleaseInfo& release, ProgressCallback progress, DoneCallback done)
{
    struct Listener : juce::URL::DownloadTaskListener
    {
        std::shared_ptr<std::atomic<bool>> alive;
        juce::File target;
        ProgressCallback onProgress;
        DoneCallback onDone;

        void finished (juce::URL::DownloadTask* task, bool success) override
        {
            const bool ok = success && task != nullptr && ! task->hadError() && target.getSize() > 0;
            onMessageThread (alive, [ok, target = target, done = onDone] {
                if (! ok)
                {
                    done (false, "The download failed. Check your connection and try again.");
                    return;
                }
                if (! UpdateChecker::launchInstaller (target))
                {
                    done (false, "Couldn't open the installer. It was saved to " + target.getFullPathName());
                    return;
                }
                done (true, "Installer opened. Restart your DAW after it finishes to load the new version.");
            });
        }

        void progress (juce::URL::DownloadTask*, juce::int64 downloaded, juce::int64 total) override
        {
            if (total <= 0)
                return;
            const auto fraction = static_cast<float> (static_cast<double> (downloaded) / static_cast<double> (total));
            onMessageThread (alive, [cb = onProgress, fraction] { cb (fraction); });
        }
    };

    auto name = release.installerName.isNotEmpty() ? release.installerName : config_.pluginId + "-installer";
    auto target = juce::File::getSpecialLocation (juce::File::tempDirectory)
                      .getChildFile ("Baastik Labs Updates")
                      .getChildFile (name);
    target.getParentDirectory().createDirectory();
    target.deleteFile();

    auto listener = std::make_unique<Listener>();
    listener->alive = alive_;
    listener->target = target;
    listener->onProgress = std::move (progress);
    listener->onDone = std::move (done);

    const auto options = juce::URL::DownloadTaskOptions().withListener (listener.get());
    auto url = release.installerUrl;
    download_ = url.downloadToFile (target, options);
    downloadListener_ = std::move (listener);
    if (download_ == nullptr)
        downloadListener_->finished (nullptr, false);
}
} // namespace baastik
