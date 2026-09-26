# The JUCE plugin target. Included from CMakeLists.txt when
# BAASTIK_BUILD_PLUGINS is on.

# Version: VERSION holds major.minor; release builds pass the full x.y.z.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" SPG_BASE_VERSION)
string(STRIP "${SPG_BASE_VERSION}" SPG_BASE_VERSION)
set(SPG_VERSION "${SPG_BASE_VERSION}.0" CACHE STRING "Serum Preset Generator version (x.y.z)")

# This plugin's own update channel: a GitHub release tagged
# "serum-preset-generator-latest" in the repo below.
set(SPG_UPDATE_REPO "dylancleverdon/Baastik-Labs" CACHE STRING "GitHub repo that hosts releases")
set(SPG_UPDATE_CHANNEL "serum-preset-generator-latest" CACHE STRING "Release tag of this plugin's update channel")
set(SPG_UPDATE_MANIFEST_URL "https://github.com/${SPG_UPDATE_REPO}/releases/download/${SPG_UPDATE_CHANNEL}/update.json"
    CACHE STRING "Where the plugin looks for update.json (override to test against a local server)")

set(SPG_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND SPG_FORMATS AU)
endif()

juce_add_plugin(SerumPresetGenerator
    PRODUCT_NAME "Serum Preset Generator"
    COMPANY_NAME "Baastik Labs"
    COMPANY_WEBSITE "https://github.com/${SPG_UPDATE_REPO}"
    BUNDLE_ID "com.baastiklabs.serumpresetgenerator"
    PLUGIN_MANUFACTURER_CODE Bstk
    PLUGIN_CODE Spg1
    VERSION "${SPG_VERSION}"
    FORMATS ${SPG_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    NEEDS_CURL TRUE
    NEEDS_WEB_BROWSER FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Tools
    AU_MAIN_TYPE kAudioUnitType_Effect)

target_sources(SerumPresetGenerator PRIVATE
    Source/ContentManager.cpp
    Source/PluginEditor.cpp
    Source/PluginProcessor.cpp)

target_compile_definitions(SerumPresetGenerator PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    SPG_VERSION_STRING="${SPG_VERSION}"
    SPG_UPDATE_MANIFEST_URL="${SPG_UPDATE_MANIFEST_URL}")

target_link_libraries(SerumPresetGenerator
    PRIVATE
        baastik::serumgen
        baastik_updater
        juce::juce_audio_utils
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
