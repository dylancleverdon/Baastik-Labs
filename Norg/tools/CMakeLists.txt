# norg-render: renders MIDI through the real plugin processor into WAV files, for listening
# tests and offline checks.
add_executable(norg-render norg-render/Main.cpp)
target_link_libraries(norg-render PRIVATE Norg)
