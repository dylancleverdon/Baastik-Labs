# ---------------------------------------------------------------------------
# Norg: the plugin, its updater helper, tools and tests.
# ---------------------------------------------------------------------------

set(NORG_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
configure_file(Source/common/NorgVersion.h.in "${NORG_GENERATED_DIR}/NorgVersion.h" @ONLY)

# --- Binary resources -------------------------------------------------------
juce_add_binary_data(NorgBinaryData
    NAMESPACE NorgBinary
    SOURCES
        Resources/fonts/ArchivoBlack-Regular.ttf
        Resources/fonts/BarlowSemiCondensed-SemiBold.ttf
        Resources/fonts/BarlowSemiCondensed-Bold.ttf)

# --- The plugin -------------------------------------------------------------
juce_add_plugin(Norg
    COMPANY_NAME "Baastik Labs"
    COMPANY_WEBSITE "https://github.com/dylancleverdon/Baastik-Labs"
    BUNDLE_ID com.baastiklabs.norg
    PLUGIN_MANUFACTURER_CODE Bstk
    PLUGIN_CODE Norg
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "Norg"
    DESCRIPTION "Norg stage keyboard: organ, piano, synth and sample sections"
    VERSION ${NORG_VERSION}
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    VST3_CATEGORIES Instrument Synth
    COPY_PLUGIN_AFTER_BUILD ${NORG_COPY_AFTER_BUILD}
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE)

file(GLOB_RECURSE NORG_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.h")
target_sources(Norg PRIVATE ${NORG_SOURCES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/Source" FILES ${NORG_SOURCES})

target_include_directories(Norg PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/Source"
    "${NORG_GENERATED_DIR}")

target_compile_definitions(Norg PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_USE_XINPUT=0)

target_link_libraries(Norg
    PRIVATE
        NorgBinaryData
        NorgUpdaterCore
    PUBLIC
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_dsp
        juce::juce_cryptography
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# --- Updater ----------------------------------------------------------------
add_subdirectory(Updater)

# --- Tools ------------------------------------------------------------------
add_subdirectory(tools)

# --- Tests ------------------------------------------------------------------
if(NORG_BUILD_TESTS)
    add_subdirectory(tests)
endif()
