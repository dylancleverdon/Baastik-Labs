# Text To Synth: a Claude plugin (MCP server) that turns descriptions into
# Serum 2 presets and edits them in plain English.
#
#   texttosynth_core   JUCE-free engine: targets, edits, describe, history,
#                      MCP protocol and tools (tested in tests/)
#   text-to-synth-dev  JUCE-free server for local testing and CI (no updater)
#   TextToSynth        the shipped server with the Baastik auto-updater
#                      (Plugin.cmake, needs JUCE)

# Version: VERSION holds major.minor; release builds pass the full x.y.z.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" TTS_BASE_VERSION)
string(STRIP "${TTS_BASE_VERSION}" TTS_BASE_VERSION)
set(TTS_VERSION "${TTS_BASE_VERSION}.0" CACHE STRING "Text To Synth version (x.y.z)")

file(GLOB_RECURSE TTS_CONTENT CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Resources/content/*.json")

add_library(texttosynth_core STATIC
    core/src/App.cpp
    core/src/Content.cpp
    core/src/Describe.cpp
    core/src/Edits.cpp
    core/src/Registration.cpp
    core/src/Server.cpp
    core/src/Store.cpp
    core/src/Targets.cpp)
add_library(baastik::texttosynth ALIAS texttosynth_core)
target_include_directories(texttosynth_core PUBLIC core/include)
target_link_libraries(texttosynth_core PUBLIC baastik::serum2 baastik::serumgen)
baastik_set_warnings(texttosynth_core)
baastik_embed_resources(texttosynth_core
    NAMESPACE tts::embedded
    BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Resources/content"
    FILES ${TTS_CONTENT})

add_executable(text-to-synth-dev dev/DevMain.cpp)
target_link_libraries(text-to-synth-dev PRIVATE baastik::texttosynth)
target_compile_definitions(text-to-synth-dev PRIVATE TTS_VERSION_STRING="${TTS_VERSION}-dev")
baastik_set_warnings(text-to-synth-dev)

if(BAASTIK_BUILD_PLUGINS)
    include(Plugin.cmake)
endif()
