#include "AppUpdater.h"

#include <juce_events/juce_events.h>

#include "Platform.h"
#include "TuttiVersion.h"
#include "tutti/Store.h"

namespace
{
    class Job : public juce::Thread
    {
    public:
        explicit Job (std::function<void()> fn) : juce::Thread ("Tutti updater"), body (std::move (fn)) {}
        void run() override { body(); }

    private:
        std::function<void()> body;
    };

    juce::String platformKey()
    {
       #if JUCE_MAC
        return "mac";
       #elif JUCE_WINDOWS
        return "windows";
       #else
        return "linux";
       #endif
    }

    // Fetches a URL into memory. curl on macOS (no quarantine flag, like Norg); JUCE elsewhere.
    bool fetchText (const juce::String& url, juce::String& out)
    {
        if (url.startsWith ("file://"))
        {
            out = juce::URL (url).getLocalFile().loadFileAsString();
            return out.isNotEmpty();
        }
       #if JUCE_MAC
        juce::ChildProcess p;
        if (! p.start (juce::StringArray { "/usr/bin/curl", "-fsSL", "--connect-timeout", "20", url }))
            return false;
        out = p.readAllProcessOutput();
        return p.waitForProcessToFinish (60000) && p.getExitCode() == 0 && out.isNotEmpty();
       #else
        auto stream = juce::URL (url).createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                                             .withConnectionTimeoutMs (20000)
                                                             .withNumRedirectsToFollow (8));
        if (stream == nullptr)
            return false;
        out = stream->readEntireStreamAsString();
        return out.isNotEmpty();
       #endif
    }

    bool download (const juce::String& url, const juce::File& target)
    {
        target.deleteFile();
        target.getParentDirectory().createDirectory();
        if (url.startsWith ("file://"))
            return juce::URL (url).getLocalFile().copyFileTo (target);
       #if JUCE_MAC
        juce::ChildProcess p;
        if (! p.start (juce::StringArray { "/usr/bin/curl", "-fsSL", "--retry", "3", "--connect-timeout", "20",
                                           "--speed-limit", "1000", "--speed-time", "60", "-o", target.getFullPathName(), url }, 0))
            return false;
        return p.waitForProcessToFinish (30 * 60 * 1000) && p.getExitCode() == 0 && target.existsAsFile();
       #else
        auto stream = juce::URL (url).createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                                             .withConnectionTimeoutMs (20000)
                                                             .withNumRedirectsToFollow (8));
        if (stream == nullptr)
            return false;
        juce::FileOutputStream out (target);
        if (! out.openedOk())
            return false;
        out.writeFromInputStream (*stream, -1);
        out.flush();
        return target.getSize() > 0;
       #endif
    }

    bool unzip (const juce::File& zip, const juce::File& dir, juce::String& error)
    {
        dir.deleteRecursively();
        dir.createDirectory();
       #if JUCE_MAC
        // ditto keeps bundles intact (symlinks, permissions, signatures).
        juce::ChildProcess p;
        if (! p.start (juce::StringArray { "/usr/bin/ditto", "-x", "-k", zip.getFullPathName(), dir.getFullPathName() }))
            return false;
        p.readAllProcessOutput();
        if (p.getExitCode() != 0)
        {
            error = "couldn't unpack the update";
            return false;
        }
        return true;
       #else
        juce::ZipFile archive (zip);
        for (int i = 0; i < archive.getNumEntries(); ++i)
            if (! tutti::update::isSafeEntry (archive.getEntry (i)->filename.toStdString()))
            {
                error = "the update contains an unsafe path";
                return false;
            }
        const auto result = archive.uncompressTo (dir);
        if (result.failed())
            error = result.getErrorMessage();
        return result.wasOk();
       #endif
    }

    juce::File userHome() { return juce::File::getSpecialLocation (juce::File::userHomeDirectory); }

    // Where each part of a payload is installed on this computer.
    struct Item
    {
        juce::String name;
        juce::File destination;
    };

    std::vector<Item> layout()
    {
        std::vector<Item> items;
       #if JUCE_MAC
        items.push_back ({ "Tutti Stand-in.vst3", userHome().getChildFile ("Library/Audio/Plug-Ins/VST3/Tutti Stand-in.vst3") });
        items.push_back ({ "Tutti Stand-in.component", userHome().getChildFile ("Library/Audio/Plug-Ins/Components/Tutti Stand-in.component") });
        items.push_back ({ "Tutti.app", juce::File::getSpecialLocation (juce::File::currentApplicationFile) });
       #elif JUCE_WINDOWS
        const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
        items.push_back ({ "Tutti Stand-in.vst3",
                           juce::File::getSpecialLocation (juce::File::globalApplicationsDirectory)
                               .getChildFile ("Common Files/VST3/Tutti Stand-in.vst3") });
        items.push_back ({ "RemoteScript", exe.getParentDirectory().getChildFile ("RemoteScript") });
        items.push_back ({ "Tutti.exe", exe });
       #endif
        return items;
    }

    // Moves `from` to `to`, replacing it. A running .exe can't be overwritten on Windows but can be
    // renamed, so the old one is moved aside first.
    bool replace (const juce::File& from, const juce::File& to, const juce::File& backup)
    {
        backup.deleteRecursively();
        if (to.exists())
        {
            if (! to.moveFileTo (backup) && ! to.copyFileTo (backup))
                return false;
        }
        to.getParentDirectory().createDirectory();
        if (from.isDirectory())
            return from.moveFileTo (to) || from.copyDirectoryTo (to);
        return from.moveFileTo (to) || from.copyFileTo (to);
    }
}

AppUpdater::AppUpdater (Options o) : options (std::move (o))
{
    auto j = tutti::parseJson (options.dataDir.getChildFile ("updater.json").loadFileAsString().toStdString())
                 .value_or (tutti::Json::object());
    settings = tutti::update::Settings::fromJson (j);
    lastCheck = tutti::integer (j, "last");
    endpoint = tutti::str (j, "endpoint");
    status = "Up to date";
}

AppUpdater::~AppUpdater()
{
    if (worker != nullptr)
        worker->stopThread (10000);
}

void AppUpdater::cleanUpAfterUpdate()
{
   #if JUCE_WINDOWS
    juce::File::getSpecialLocation (juce::File::currentExecutableFile).getSiblingFile ("Tutti.old.exe").deleteFile();
   #endif
}

void AppUpdater::saveSettings()
{
    auto j = settings.toJson();
    j["last"] = lastCheck;
    j["endpoint"] = endpoint;
    options.dataDir.getChildFile ("updater.json").replaceWithText (juce::String (j.dump (2)));
}

void AppUpdater::setStatus (const std::string& s)
{
    {
        std::lock_guard<std::mutex> g (lock);
        status = s;
    }
    juce::MessageManager::callAsync ([this] { if (onChange) onChange(); });
}

UpdaterState AppUpdater::state() const
{
    UpdaterState s;
    s.installedVersion = tutti::version::string;
    auto installed = tutti::parseJson (options.dataDir.getChildFile ("installed.json").loadFileAsString().toStdString());
    if (installed && tutti::integer (*installed, "build") == tutti::version::build)
        s.installedNotes = tutti::str (*installed, "notes");
    std::lock_guard<std::mutex> g (lock);
    s.status = status;
    s.availableVersion = availableVersion;
    s.availableNotes = availableNotes;
    s.autoUpdate = settings.autoUpdate;
    s.canRollBack = options.dataDir.getChildFile ("previous").isDirectory();
    s.busy = busy.load();
    return s;
}

std::string AppUpdater::suggestionsEndpoint() const
{
    std::lock_guard<std::mutex> g (lock);
    return endpoint;
}

void AppUpdater::setAutoUpdate (bool on)
{
    settings.autoUpdate = on;
    saveSettings();
    if (onChange)
        onChange();
}

void AppUpdater::tick()
{
    const auto nowMs = juce::Time::currentTimeMillis();
    // First check a minute after launch, then hourly.
    static const auto launched = nowMs;
    if (! busy && nowMs - launched > 60 * 1000 && nowMs - lastCheck > 60 * 60 * 1000)
        startCheck (false);

    if (! busy && staged != juce::File() && options.safeToInstall && options.safeToInstall())
        installStaged();
}

void AppUpdater::checkNow() { startCheck (true); }

void AppUpdater::startCheck (bool manual)
{
    if (busy.exchange (true))
        return;
    if (worker != nullptr)
        worker->stopThread (1000);
    worker = std::make_unique<Job> ([this, manual] {
        runCheck (manual);
        busy = false;
        juce::MessageManager::callAsync ([this] { if (onChange) onChange(); });
    });
    worker->startThread();
}

void AppUpdater::runCheck (bool manual)
{
    setStatus ("Checking for updates...");
    juce::String text;
    if (! fetchText (options.manifestUrl, text))
    {
        setStatus (manual ? "Couldn't reach the update server" : "Up to date");
        return;
    }
    std::string error;
    auto manifest = tutti::update::parse (text.toStdString(), error);
    lastCheck = juce::Time::currentTimeMillis();
    if (! manifest)
    {
        setStatus ("The update information was unreadable");
        return;
    }
    {
        std::lock_guard<std::mutex> g (lock);
        if (! manifest->suggestionsEndpoint.empty())
            endpoint = manifest->suggestionsEndpoint;
        availableVersion = manifest->version;
        availableNotes = manifest->notes;
    }
    saveSettings();

    const auto decision = tutti::update::decide (*manifest, tutti::version::build, settings, manual,
                                                 platformKey().toStdString(), options.extraPrefixes);
    using D = tutti::update::Decision;
    if (decision == D::upToDate || decision == D::unavailable)
    {
        setStatus ("Up to date");
        return;
    }
    if (decision == D::skipped || decision == D::disabled)
    {
        setStatus (decision == D::disabled ? "Update available (automatic updates are off)" : "Up to date");
        return;
    }

    const auto& asset = manifest->platforms.at (platformKey().toStdString());
    auto updates = options.dataDir.getChildFile ("updates");
    auto zip = updates.getChildFile (juce::File::createLegalFileName (juce::String (asset.name)));
    setStatus ("Downloading Tutti " + manifest->version + "...");
    if (! download (juce::String (asset.url), zip))
    {
        setStatus ("The download failed; trying again later");
        return;
    }
    if (tutti::Store::sha256 (zip) != asset.sha256)
    {
        zip.deleteFile();
        setStatus ("The download didn't match its checksum; trying again later");
        return;
    }
    juce::String unzipError;
    auto dir = updates.getChildFile ("staged-" + juce::String (manifest->build));
    if (! unzip (zip, dir, unzipError))
    {
        setStatus ("Couldn't unpack the update: " + unzipError.toStdString());
        return;
    }
    zip.deleteFile();
    // A zip made with its folder kept: use that folder.
    if (! dir.getChildFile ("Tutti.app").exists() && ! dir.getChildFile ("Tutti.exe").exists())
    {
        auto children = dir.findChildFiles (juce::File::findDirectories, false);
        if (children.size() == 1)
            dir = children[0];
    }
    {
        std::lock_guard<std::mutex> g (lock);
        staged = dir;
        stagedBuild = manifest->build;
        stagedVersion = manifest->version;
        stagedNotes = manifest->notes;
        status = "Tutti " + manifest->version + " is ready: it installs when you're not in a session";
    }
    juce::MessageManager::callAsync ([this] { if (onChange) onChange(); });
}

bool AppUpdater::swapIn (const juce::File& payload, const juce::File& previous)
{
    // Install everything; if anything fails, put back what was already replaced.
    previous.deleteRecursively();
    previous.createDirectory();
    std::vector<Item> done;
    for (const auto& item : layout())
    {
        auto from = payload.getChildFile (item.name);
        if (! from.exists())
            continue;
        if (! replace (from, item.destination, previous.getChildFile (item.name)))
        {
            for (auto it = done.rbegin(); it != done.rend(); ++it)
            {
                it->destination.deleteRecursively();
                previous.getChildFile (it->name).moveFileTo (it->destination);
            }
            return false;
        }
        done.push_back (item);
    }
    return ! done.empty();
}

bool AppUpdater::installStaged()
{
    juce::File dir;
    juce::int64 build;
    std::string version, notes;
    {
        std::lock_guard<std::mutex> g (lock);
        dir = staged;
        build = stagedBuild;
        version = stagedVersion;
        notes = stagedNotes;
        staged = juce::File();
    }
    if (dir == juce::File())
        return false;
    // The running version becomes "previous", ready for Roll back.
    if (! swapIn (dir, options.dataDir.getChildFile ("previous")))
    {
        setStatus ("Couldn't install the update; trying again later");
        return false;
    }
    auto previousInfo = options.dataDir.getChildFile ("installed.json").loadFileAsString();
    options.dataDir.getChildFile ("previous/installed.json").replaceWithText (previousInfo);
    tutti::Json info { { "version", version }, { "build", build }, { "notes", notes } };
    options.dataDir.getChildFile ("installed.json").replaceWithText (juce::String (info.dump (2)));
    dir.deleteRecursively();
    relaunch();
    return true;
}

void AppUpdater::rollBack()
{
    auto previous = options.dataDir.getChildFile ("previous");
    if (! previous.isDirectory() || (options.safeToInstall && ! options.safeToInstall()))
    {
        setStatus ("Roll back works when you're not in a session");
        return;
    }
    // Swap previous and current, so rolling back twice rolls forward again.
    auto holding = options.dataDir.getChildFile ("rollback-tmp");
    holding.deleteRecursively();
    previous.moveFileTo (holding);
    if (! swapIn (holding, previous))
    {
        holding.moveFileTo (previous);
        setStatus ("Couldn't roll back");
        return;
    }
    settings.skipBuild = tutti::version::build;
    saveSettings();
    auto info = holding.getChildFile ("installed.json");
    if (info.existsAsFile())
        info.copyFileTo (options.dataDir.getChildFile ("installed.json"));
    holding.deleteRecursively();
    relaunch();
}

void AppUpdater::relaunch()
{
   #if JUCE_MAC
    const auto app = juce::File::getSpecialLocation (juce::File::currentApplicationFile);
    juce::ChildProcess p;
    p.start (juce::StringArray { "/bin/sh", "-c", "sleep 1; /usr/bin/open -n \"" + app.getFullPathName() + "\" --args --background --updated" }, 0);
   #elif JUCE_WINDOWS
    juce::File::getSpecialLocation (juce::File::currentExecutableFile).startAsProcess ("--background --updated");
   #endif
    if (options.quitForRelaunch)
        options.quitForRelaunch();
}
