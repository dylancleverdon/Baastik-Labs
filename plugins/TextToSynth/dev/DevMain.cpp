// Text To Synth without JUCE or the updater, for local testing and CI:
//   claude mcp add text-to-synth-dev -- /path/to/text-to-synth-dev
#include <filesystem>

#include "tts/App.h"

#ifndef TTS_VERSION_STRING
#define TTS_VERSION_STRING "0.0.0-dev"
#endif

int main(int argc, char** argv)
{
    std::error_code ec;
    auto self = std::filesystem::absolute(argv[0], ec);
    return tts::runApp(argc, argv, TTS_VERSION_STRING, nullptr, self);
}
