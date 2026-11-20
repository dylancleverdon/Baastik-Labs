# The Tutti engine (networking, sessions, Live bridge, files): shared by the app, tutti-peer and
# the end-to-end tests. INTERFACE, like tutti_core: consumers link juce_core, juce_cryptography
# and juce_audio_formats themselves.
add_library(tutti_engine INTERFACE)
target_sources(tutti_engine INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/Net.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Platform.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Engine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/EngineFiles.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/EngineVersions.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/EngineStandIn.cpp)
target_include_directories(tutti_engine INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tutti_engine INTERFACE tutti_core)
