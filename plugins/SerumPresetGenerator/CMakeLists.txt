# Serum Preset Generator: a JUCE-free core (also used by serumtool and the
# tests) plus the plugin itself.

file(GLOB_RECURSE SERUMGEN_CONTENT CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Resources/content/*.json")

add_library(serumgen_core STATIC
    core/src/Content.cpp
    core/src/Generator.cpp
    core/src/Knobs.cpp)
add_library(baastik::serumgen ALIAS serumgen_core)
target_include_directories(serumgen_core PUBLIC core/include)
target_link_libraries(serumgen_core PUBLIC baastik::serum2)
baastik_set_warnings(serumgen_core)

set(SERUMGEN_CONTENT_VERSION "" CACHE STRING "Content version stamped by release builds (empty = content.json)")
if(SERUMGEN_CONTENT_VERSION)
    target_compile_definitions(serumgen_core PRIVATE SERUMGEN_CONTENT_VERSION=${SERUMGEN_CONTENT_VERSION})
endif()
baastik_embed_resources(serumgen_core
    NAMESPACE serumgen::embedded
    BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Resources/content"
    FILES ${SERUMGEN_CONTENT})

if(BAASTIK_BUILD_PLUGINS)
    include(Plugin.cmake)
endif()
