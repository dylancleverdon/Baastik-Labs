#pragma once

#include <juce_core/juce_core.h>

// Where things live on each OS, and the few OS actions Tutti needs.
namespace tutti::platform
{
    juce::String osName(); // "mac", "windows", "linux"

    juce::File dataDir();  // settings, store, versions (TUTTI_DATA_DIR overrides)
    juce::File songsDir(); // the session copies Live opens (TUTTI_SONGS_DIR overrides)
    juce::File logsDir();

    // Live's own log (Log.txt) for the Live version used most recently, or an empty File.
    juce::File liveLogFile();

    // Google Drive for desktop folders on this computer (My Drive and shared drives), newest
    // first; empty if Drive for desktop isn't installed or signed in.
    juce::Array<juce::File> googleDriveFolders();

    // Opens a Live Set in the Live that's running (or the default app for .als files).
    bool openInLive (const juce::File& als);

    // Live's User Library folders on this computer (default plus any set in Live's preferences).
    juce::Array<juce::File> userLibraries();

    // Copies the bundled Remote Script into every User Library's "Remote Scripts/Tutti".
    // Returns the folders it was installed to.
    juce::Array<juce::File> installRemoteScript (const juce::File& bundledScriptFolder);

    // The Remote Script shipped with the app (inside the .app on macOS, next to the .exe on Windows).
    juce::File bundledRemoteScript();

    // Start Tutti when the person logs in (LaunchAgent on macOS, Run key on Windows).
    bool setStartAtLogin (const juce::File& appExecutable, bool enabled);
}
