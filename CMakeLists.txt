cmake_minimum_required(VERSION 3.24)

project(BaastikLabs VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures")
endif()

option(BAASTIK_BUILD_PLUGINS "Build the JUCE audio plugins" ON)
option(BAASTIK_BUILD_TOOLS "Build command-line tools" ON)
option(BAASTIK_BUILD_TESTS "Build unit tests" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Dependencies)
include(EmbedResources)
include(CompilerWarnings)

if(BAASTIK_BUILD_PLUGINS)
    # Shared JUCE modules for every plugin in the repo.
    juce_add_module(modules/baastik_updater)
endif()

add_subdirectory(libs/serum2)
add_subdirectory(plugins/SerumPresetGenerator)
add_subdirectory(plugins/TextToSynth)

if(BAASTIK_BUILD_TOOLS)
    add_subdirectory(tools/serumtool)
endif()

if(BAASTIK_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
