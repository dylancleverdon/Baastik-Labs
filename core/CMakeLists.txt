# Tutti's testable core. An INTERFACE library, so each consumer (app, tests, tutti-peer) compiles
# it against its own copy of the JUCE modules and they are never linked twice.
#
# Consumers must link: juce::juce_core, juce::juce_cryptography, juce::juce_audio_formats.
add_library(tutti_core INTERFACE)

file(GLOB TUTTI_CORE_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
target_sources(tutti_core INTERFACE ${TUTTI_CORE_SOURCES})
target_include_directories(tutti_core INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${TUTTI_GENERATED_DIR})
target_link_libraries(tutti_core INTERFACE nlohmann_json::nlohmann_json)
