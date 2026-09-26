# norg-sign: signs update manifests in CI (plain C++, no JUCE).
add_executable(norg-sign norg-sign/Main.cpp)
target_include_directories(norg-sign PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../Updater/Source)
target_link_libraries(norg-sign PRIVATE monocypher)

# norg-render: renders MIDI through the real plugin processor into WAV files, for listening
# tests and offline checks.
add_executable(norg-render norg-render/Main.cpp)
target_link_libraries(norg-render PRIVATE Norg)
