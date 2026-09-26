# The shipped Text To Synth binary: the MCP server plus the Baastik
# auto-updater. Included from CMakeLists.txt when BAASTIK_BUILD_PLUGINS is on.
# It's a console app (Claude launches it over stdio); JUCE only provides the
# networking, settings and message loop the updater needs.

# This tool's own update channel: a GitHub release tagged
# "text-to-synth-latest" in the repo below.
set(TTS_UPDATE_REPO "dylancleverdon/Baastik-Labs" CACHE STRING "GitHub repo that hosts Text To Synth releases")
set(TTS_UPDATE_CHANNEL "text-to-synth-latest" CACHE STRING "Release tag of Text To Synth's update channel")
set(TTS_UPDATE_MANIFEST_URL "https://github.com/${TTS_UPDATE_REPO}/releases/download/${TTS_UPDATE_CHANNEL}/update.json"
    CACHE STRING "Where Text To Synth looks for update.json (override to test against a local server)")

juce_add_console_app(TextToSynth
    PRODUCT_NAME "text-to-synth"
    COMPANY_NAME "Baastik Labs"
    COMPANY_WEBSITE "https://github.com/${TTS_UPDATE_REPO}"
    BUNDLE_ID "com.baastiklabs.texttosynth"
    VERSION "${TTS_VERSION}"
    NEEDS_CURL TRUE
    NEEDS_WEB_BROWSER FALSE)

target_sources(TextToSynth PRIVATE
    Source/Main.cpp
    Source/Updates.cpp)

target_compile_definitions(TextToSynth PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=1
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_STANDALONE_APPLICATION=1
    TTS_VERSION_STRING="${TTS_VERSION}"
    TTS_UPDATE_MANIFEST_URL="${TTS_UPDATE_MANIFEST_URL}")

target_link_libraries(TextToSynth
    PRIVATE
        baastik::texttosynth
        baastik_updater
        juce::juce_core
        juce::juce_events
        juce::juce_data_structures
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
