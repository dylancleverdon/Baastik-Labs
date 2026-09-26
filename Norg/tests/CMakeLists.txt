# Plugin / DSP tests: link against the plugin's shared-code target, so they exercise the real processor.
file(GLOB NORG_ENGINE_TESTS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/engine/*.cpp)
add_executable(NorgTests TestMain.cpp ${NORG_ENGINE_TESTS})
target_link_libraries(NorgTests PRIVATE Norg Catch2::Catch2)
catch_discover_tests(NorgTests DISCOVERY_MODE PRE_TEST)

# Updater tests: pure logic plus file swaps in temporary folders (no network, no real installs).
file(GLOB NORG_UPDATER_TESTS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/updater/*.cpp)
add_executable(NorgUpdaterTests TestMain.cpp ${NORG_UPDATER_TESTS})
target_compile_definitions(NorgUpdaterTests PRIVATE JUCE_STANDALONE_APPLICATION=1 JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
target_link_libraries(NorgUpdaterTests PRIVATE
    NorgUpdaterCore juce::juce_core juce::juce_cryptography juce::juce_events
    juce::juce_recommended_config_flags Catch2::Catch2)
catch_discover_tests(NorgUpdaterTests DISCOVERY_MODE PRE_TEST)


# Tests compare exact values on purpose. (Set per source file so it comes after JUCE's warning flags.)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    set_source_files_properties(${NORG_ENGINE_TESTS} ${NORG_UPDATER_TESTS} TestMain.cpp
        PROPERTIES COMPILE_OPTIONS "-Wno-float-equal")
endif()
