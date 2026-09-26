# Update logic shared by the NorgUpdater helper and its tests. An INTERFACE library, so each
# consumer compiles it against its own copy of the JUCE modules.
add_library(NorgUpdaterCore INTERFACE)
target_sources(NorgUpdaterCore INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/UpdateCore.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Installer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Updater.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/SystemPlatform.cpp)
target_include_directories(NorgUpdaterCore INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/Source
    ${NORG_GENERATED_DIR})
target_link_libraries(NorgUpdaterCore INTERFACE monocypher)

juce_add_console_app(NorgUpdater
    PRODUCT_NAME "NorgUpdater"
    COMPANY_NAME "Baastik Labs"
    BUNDLE_ID com.baastiklabs.norg.updater
    VERSION ${NORG_VERSION})

target_sources(NorgUpdater PRIVATE Source/Main.cpp)

target_compile_definitions(NorgUpdater PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1)

target_link_libraries(NorgUpdater
    PRIVATE
        NorgUpdaterCore
        juce::juce_core
        juce::juce_cryptography
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
