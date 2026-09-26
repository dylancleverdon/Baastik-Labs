#include "Updater.h"

#if JUCE_MAC || JUCE_LINUX
 #include <unistd.h>
#endif

namespace norg::update
{
    namespace
    {
        // Runs a program directly (no shell), returning its exit code and combined output.
        int run (const juce::StringArray& args, juce::String& output, int timeoutMs = 10 * 60 * 1000)
        {
            juce::ChildProcess process;
            if (! process.start (args))
            {
                output = "could not start " + args[0];
                return -1;
            }

            output = process.readAllProcessOutput();
            if (! process.waitForProcessToFinish (timeoutMs))
            {
                process.kill();
                output << " (timed out)";
                return -1;
            }

            return static_cast<int> (process.getExitCode());
        }

       #if JUCE_MAC
        // Runs a helper whose output we don't need, killing it if it overruns (never blocks on pipes).
        int runQuiet (const juce::StringArray& args, int timeoutMs)
        {
            juce::ChildProcess process;
            if (! process.start (args, 0))
                return -1;

            if (! process.waitForProcessToFinish (timeoutMs))
            {
                process.kill();
                return -1;
            }

            return static_cast<int> (process.getExitCode());
        }

        juce::String escapeAppleScript (const juce::String& s)
        {
            return s.replace ("\\", "\\\\").replace ("\"", "\\\"");
        }
       #endif

        juce::String escapeXml (const juce::String& s)
        {
            return s.replace ("&", "&amp;").replace ("<", "&lt;").replace (">", "&gt;");
        }

        class SystemPlatform final : public Platform
        {
        public:
            bool download (const juce::String& url, const juce::File& destination, juce::String& error) override
            {
                // curl (not a browser) so the download never gets a quarantine flag.
                juce::String out;
                const int code = run ({ "/usr/bin/curl", "-fsSL", "--retry", "2", "--connect-timeout", "20",
                                        "--max-time", "900", "-o", destination.getFullPathName(), url }, out);
                if (code != 0 || ! destination.existsAsFile())
                {
                    error = "curl exited with " + juce::String (code) + ": " + out.trim();
                    return false;
                }
                return true;
            }

            bool extractZip (const juce::File& zip, const juce::File& destinationDir, juce::String& error) override
            {
               #if JUCE_MAC
                juce::String out;
                const int code = run ({ "/usr/bin/ditto", "-x", "-k", zip.getFullPathName(),
                                        destinationDir.getFullPathName() }, out);
                if (code != 0)
                {
                    error = "ditto exited with " + juce::String (code) + ": " + out.trim();
                    return false;
                }
                return true;
               #else
                juce::ZipFile archive (zip);
                const auto result = archive.uncompressTo (destinationDir);
                if (result.failed())
                    error = result.getErrorMessage();
                return result.wasOk();
               #endif
            }

            bool verifyCodeSignature (const juce::File& item, juce::String& error) override
            {
               #if JUCE_MAC
                juce::String out;
                const int code = run ({ "/usr/bin/codesign", "--verify", "--deep", "--strict",
                                        item.getFullPathName() }, out);
                if (code != 0)
                {
                    error = out.trim();
                    return false;
                }
               #else
                juce::ignoreUnused (item, error);
               #endif
                return true;
            }

            void refreshAudioComponents() override
            {
               #if JUCE_MAC
                // Makes the AU registry notice the new Norg.component straight away.
                runQuiet ({ "/usr/bin/killall", "-9", "AudioComponentRegistrar" }, 10000);
               #endif
            }

            void notify (const juce::String& title, const juce::String& message) override
            {
               #if JUCE_MAC
                runQuiet ({ "/usr/bin/osascript", "-e",
                            "display notification \"" + escapeAppleScript (message) + "\" with title \""
                                + escapeAppleScript (title) + "\"" }, 10000);
               #else
                juce::ignoreUnused (title, message);
               #endif
            }

            bool installLaunchAgent (const Layout& layout, juce::String& error) override
            {
                const auto plist = layout.launchAgentPlist();
                plist.getParentDirectory().createDirectory();
                layout.logsDir().createDirectory();

                const auto updater = layout.updaterExecutable().getFullPathName();
                const auto logPath = layout.logsDir().getChildFile ("updater-launchd.log").getFullPathName();

                juce::String xml;
                xml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                    << "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
                    << "<plist version=\"1.0\">\n<dict>\n"
                    << "  <key>Label</key><string>" << Layout::launchAgentLabel << "</string>\n"
                    << "  <key>ProgramArguments</key>\n  <array>\n"
                    << "    <string>" << escapeXml (updater) << "</string>\n"
                    << "    <string>--auto</string>\n  </array>\n"
                    << "  <key>RunAtLoad</key><true/>\n"
                    << "  <key>StartInterval</key><integer>3600</integer>\n"
                    << "  <key>ProcessType</key><string>Background</string>\n"
                    << "  <key>LowPriorityIO</key><true/>\n"
                    << "  <key>StandardOutPath</key><string>" << escapeXml (logPath) << "</string>\n"
                    << "  <key>StandardErrorPath</key><string>" << escapeXml (logPath) << "</string>\n"
                    << "</dict>\n</plist>\n";

                if (! plist.replaceWithText (xml))
                {
                    error = "could not write " + plist.getFullPathName();
                    return false;
                }

               #if JUCE_MAC
                const auto domain = "gui/" + juce::String (static_cast<int> (getuid()));
                runQuiet ({ "/bin/launchctl", "bootout", domain + "/" + Layout::launchAgentLabel }, 20000);
                if (const int code = runQuiet ({ "/bin/launchctl", "bootstrap", domain, plist.getFullPathName() }, 20000); code != 0)
                {
                    error = "launchctl bootstrap exited with " + juce::String (code);
                    return false;
                }
               #endif
                return true;
            }
        };
    }

    std::unique_ptr<Platform> createSystemPlatform()
    {
        return std::make_unique<SystemPlatform>();
    }
}
