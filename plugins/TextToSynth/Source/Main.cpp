// Text To Synth by Baastik Labs: the MCP server Claude launches, with the
// auto-updater. Command-line actions (--register, --version, ...) run without
// starting JUCE; the server runs on a worker thread while the main thread
// runs the JUCE message loop the updater needs.

#include <juce_events/juce_events.h>

#include <thread>

#include "Updates.h"
#include "tts/App.h"

int main (int argc, char* argv[])
{
    const auto self = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getFullPathName();
    const std::filesystem::path selfPath (std::u8string (self.toUTF8().getAddress(),
                                                         self.toUTF8().getAddress() + self.getNumBytesAsUTF8()));

    if (! tts::wantsServer (argc, argv))
        return tts::runApp (argc, argv, TTS_VERSION_STRING, nullptr, selfPath);

    juce::ScopedJuceInitialiser_GUI juce;
    // TEXT_TO_SYNTH_UPDATE_URL points the updater elsewhere (testing a release
    // before it goes out, or a local file:// manifest).
    const auto manifestOverride = juce::SystemStats::getEnvironmentVariable ("TEXT_TO_SYNTH_UPDATE_URL", {});
    const juce::URL manifestUrl (manifestOverride.isNotEmpty() ? manifestOverride : juce::String (TTS_UPDATE_MANIFEST_URL));
    JuceUpdates updates (TTS_VERSION_STRING, manifestUrl, tts::Paths::defaults().data);
    updates.start();

    int exitCode = 0;
    std::thread server ([&] {
        exitCode = tts::runApp (argc, argv, TTS_VERSION_STRING, &updates, selfPath);
        // Claude closed stdin: shut down.
        juce::MessageManager::getInstance()->stopDispatchLoop();
    });
    juce::MessageManager::getInstance()->runDispatchLoop();
    server.join();
    return exitCode;
}
