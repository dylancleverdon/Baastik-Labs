# Core tests: pure logic plus file work in temporary folders (no network, no Live).
file(GLOB TUTTI_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
add_executable(TuttiTests ${TUTTI_TEST_SOURCES})

target_compile_definitions(TuttiTests PRIVATE
    JUCE_STANDALONE_APPLICATION=1
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_USE_OGGVORBIS=1
    JUCE_USE_FLAC=1
    TUTTI_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")

target_link_libraries(TuttiTests PRIVATE
    tutti_core
    juce::juce_core
    juce::juce_cryptography
    juce::juce_audio_formats
    juce::juce_recommended_config_flags
    Catch2::Catch2WithMain)

catch_discover_tests(TuttiTests DISCOVERY_MODE PRE_TEST)
