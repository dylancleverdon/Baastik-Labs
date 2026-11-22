#include "Platform.h"

namespace tutti::platform
{
    namespace
    {
        juce::File home() { return juce::File::getSpecialLocation (juce::File::userHomeDirectory); }

        juce::File fromEnv (const char* name)
        {
            const auto value = juce::SystemStats::getEnvironmentVariable (name, {});
            return value.isNotEmpty() ? juce::File (value) : juce::File();
        }

        juce::String run (const juce::StringArray& command, int timeoutMs = 4000)
        {
            juce::ChildProcess p;
            if (! p.start (command, juce::ChildProcess::wantStdOut))
                return {};
            auto out = p.readAllProcessOutput();
            p.waitForProcessToFinish (timeoutMs);
            return out;
        }
    }

    juce::String osName()
    {
       #if JUCE_MAC
        return "mac";
       #elif JUCE_WINDOWS
        return "windows";
       #else
        return "linux";
       #endif
    }

    juce::File dataDir()
    {
        if (auto f = fromEnv ("TUTTI_DATA_DIR"); f != juce::File())
            return f;
       #if JUCE_MAC
        return home().getChildFile ("Library/Application Support/Tutti");
       #elif JUCE_WINDOWS
        return juce::File::getSpecialLocation (juce::File::windowsLocalAppData).getChildFile ("Tutti");
       #else
        return home().getChildFile (".local/share/Tutti");
       #endif
    }

    juce::File songsDir()
    {
        if (auto f = fromEnv ("TUTTI_SONGS_DIR"); f != juce::File())
            return f;
        return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Tutti");
    }

    juce::File logsDir()
    {
       #if JUCE_MAC
        return home().getChildFile ("Library/Logs/Tutti");
       #else
        return dataDir().getChildFile ("Logs");
       #endif
    }

    bool openInLive (const juce::File& als)
    {
       #if JUCE_MAC
        // Prefer the Live that's running, so a Mac with several Live versions opens the right one.
        const auto processes = juce::StringArray::fromLines (run ({ "/bin/ps", "-axo", "comm=" }));
        for (const auto& p : processes)
        {
            const auto app = p.upToFirstOccurrenceOf (".app/", true, false);
            if (app.contains ("Ableton Live") && app.endsWith (".app/"))
            {
                juce::ChildProcess open;
                return open.start (juce::StringArray { "/usr/bin/open", "-a", app.dropLastCharacters (1), als.getFullPathName() });
            }
        }
        juce::ChildProcess open;
        return open.start (juce::StringArray { "/usr/bin/open", als.getFullPathName() });
       #elif JUCE_WINDOWS
        const auto exe = run ({ "powershell", "-NoProfile", "-Command",
                                "(Get-Process -Name 'Ableton Live*' -ErrorAction SilentlyContinue | Select-Object -First 1).Path" })
                             .trim();
        if (exe.isNotEmpty() && juce::File (exe).existsAsFile())
            return juce::File (exe).startAsProcess ("\"" + als.getFullPathName() + "\"");
        return als.startAsProcess();
       #else
        return als.startAsProcess();
       #endif
    }

    juce::File liveLogFile()
    {
       #if JUCE_MAC
        auto prefs = home().getChildFile ("Library/Preferences/Ableton");
       #elif JUCE_WINDOWS
        auto prefs = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Ableton");
       #else
        auto prefs = home().getChildFile (".ableton");
       #endif
        juce::File newest;
        for (const auto& f : prefs.findChildFiles (juce::File::findFiles, true, "Log.txt"))
            if (newest == juce::File() || f.getLastModificationTime() > newest.getLastModificationTime())
                newest = f;
        return newest;
    }

    juce::Array<juce::File> userLibraries()
    {
        juce::Array<juce::File> out;
       #if JUCE_MAC
        out.add (home().getChildFile ("Music/Ableton/User Library"));
        auto prefs = home().getChildFile ("Library/Preferences/Ableton");
       #elif JUCE_WINDOWS
        out.add (juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Ableton/User Library"));
        auto prefs = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Ableton");
       #else
        out.add (home().getChildFile ("Ableton/User Library"));
        auto prefs = home().getChildFile (".ableton");
       #endif
        // Live's preferences may point the User Library somewhere else: look for paths ending in
        // "User Library" in each version's Library.cfg.
        for (const auto& cfg : prefs.findChildFiles (juce::File::findFiles, true, "Library.cfg"))
        {
            const auto text = cfg.loadFileAsString();
            int at = 0;
            while ((at = text.indexOf (at, "Value=\"")) >= 0)
            {
                at += 7;
                const auto value = text.substring (at, text.indexOf (at, "\""));
                if (value.endsWith ("User Library") && juce::File::isAbsolutePath (value))
                {
                    juce::File lib (value);
                    if (lib.isDirectory() && ! out.contains (lib))
                        out.add (lib);
                }
            }
        }
        return out;
    }

    juce::File bundledRemoteScript()
    {
        const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
       #if JUCE_MAC
        // .../Tutti.app/Contents/MacOS/Tutti -> .../Tutti.app/Contents/Resources/RemoteScript/Tutti
        auto inBundle = exe.getParentDirectory().getParentDirectory().getChildFile ("Resources/RemoteScript/Tutti");
        if (inBundle.isDirectory())
            return inBundle;
       #endif
        return exe.getParentDirectory().getChildFile ("RemoteScript/Tutti");
    }

    juce::Array<juce::File> installRemoteScript (const juce::File& source)
    {
        // Overwrite the files in place, one by one. Deleting the old folder first fails half-way
        // on Windows when Live has a file open, which left Live without Tutti's __init__.py (so
        // Tutti vanished from Live's Control Surface list). Files that are already identical are
        // left alone; ones the new version no longer has are removed afterwards (best effort).
        juce::Array<juce::File> installed;
        if (! source.getChildFile ("__init__.py").existsAsFile())
            return installed;
        for (const auto& lib : userLibraries())
        {
            auto target = lib.getChildFile ("Remote Scripts").getChildFile ("Tutti");
            lib.getChildFile ("Remote Scripts").getChildFile (".Tutti-new").deleteRecursively(); // older Tuttis' leftovers
            if (installRemoteScriptTo (source, target))
                installed.add (target);
        }
        return installed;
    }

    bool installRemoteScriptTo (const juce::File& source, const juce::File& target)
    {
        const auto files = source.findChildFiles (juce::File::findFiles, true);
        {
            if (! target.createDirectory())
                return false;
            bool ok = true;
            juce::StringArray wanted;
            for (const auto& f : files)
            {
                if (f.getFileName().endsWith (".pyc") || f.getFullPathName().contains ("__pycache__"))
                    continue;
                const auto rel = f.getRelativePathFrom (source);
                wanted.add (rel.replaceCharacter ('\\', '/'));
                auto dest = target.getChildFile (rel);
                if (dest.existsAsFile() && dest.getSize() == f.getSize() && dest.hasIdenticalContentTo (f))
                    continue;
                dest.getParentDirectory().createDirectory();
                bool copied = false;
                for (int attempt = 0; attempt < 5 && ! copied; ++attempt)
                {
                    copied = f.copyFileTo (dest);
                    if (! copied)
                        juce::Thread::sleep (200);
                }
                ok = ok && copied;
            }
            for (const auto& old : target.findChildFiles (juce::File::findFiles, true))
            {
                const auto rel = old.getRelativePathFrom (target).replaceCharacter ('\\', '/');
                if (old.getFileName().endsWith (".pyc") || rel.contains ("__pycache__") || ! wanted.contains (rel))
                    old.deleteFile(); // stale bytecode and removed files; fine if it fails
            }
            return ok && target.getChildFile ("__init__.py").existsAsFile();
        }
    }

    bool setStartAtLogin (const juce::File& executable, bool enabled)
    {
       #if JUCE_MAC
        auto plist = home().getChildFile ("Library/LaunchAgents/com.baastiklabs.tutti.plist");
        if (! enabled)
            return plist.deleteFile();
        // The bundle, not the binary, so macOS shows it as Tutti.
        auto app = executable.getParentDirectory().getParentDirectory().getParentDirectory();
        const juce::String xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
            "<plist version=\"1.0\"><dict>\n"
            "  <key>Label</key><string>com.baastiklabs.tutti</string>\n"
            "  <key>ProgramArguments</key><array><string>/usr/bin/open</string><string>-g</string><string>"
            + app.getFullPathName() + "</string><string>--args</string><string>--background</string></array>\n"
            "  <key>RunAtLoad</key><true/>\n"
            "</dict></plist>\n";
        plist.getParentDirectory().createDirectory();
        return plist.replaceWithText (xml);
       #elif JUCE_WINDOWS
        const juce::String key = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run\\Tutti";
        if (! enabled)
        {
            juce::WindowsRegistry::deleteValue (key);
            return true;
        }
        return juce::WindowsRegistry::setValue (key, "\"" + executable.getFullPathName() + "\" --background");
       #else
        juce::ignoreUnused (executable, enabled);
        return false;
       #endif
    }
}
