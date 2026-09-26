# Monocypher 4.0.3 (BSD-2-Clause OR CC0-1.0), vendored for Ed25519 update signatures.
add_library(monocypher STATIC monocypher.c monocypher-ed25519.c)
target_include_directories(monocypher PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(monocypher PROPERTIES C_STANDARD 99 POSITION_INDEPENDENT_CODE ON)
