# Tutti Stand-in: put it right after a plugin some of the group don't own. On owners' computers it
# records that track while they play it; on everyone else's it plays the shared recording in time.
juce_add_plugin(TuttiStandIn
    COMPANY_NAME "Baastik Labs"
    COMPANY_WEBSITE "https://github.com/dylancleverdon/Baastik-Labs"
    BUNDLE_ID com.baastiklabs.tutti.standin
    PLUGIN_MANUFACTURER_CODE Bstk
    PLUGIN_CODE TtSi
    FORMATS AU VST3
    PRODUCT_NAME "Tutti Stand-in"
    DESCRIPTION "Plays a shared recording of a plugin you don't own (Tutti)"
    VERSION ${TUTTI_VERSION}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    VST3_CATEGORIES Fx
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(TuttiStandIn PRIVATE
    Source/StandIn.cpp
    Source/StandIn.h
    ${CMAKE_SOURCE_DIR}/core/src/Protocol.cpp
    ${CMAKE_SOURCE_DIR}/core/src/SocketIO.cpp)
target_include_directories(TuttiStandIn PRIVATE
    ${CMAKE_SOURCE_DIR}/core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/Source)
target_compile_definitions(TuttiStandIn PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_USE_XINPUT=0)
target_link_libraries(TuttiStandIn
    PRIVATE
        nlohmann_json::nlohmann_json
    PUBLIC
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
