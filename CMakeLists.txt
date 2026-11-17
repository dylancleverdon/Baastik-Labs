cmake_minimum_required(VERSION 3.22)

# Version = MAJOR.MINOR from the VERSION file + a build number (the CI run number).
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" TUTTI_VERSION_BASE LIMIT_COUNT 1)
set(TUTTI_BUILD_NUMBER "0" CACHE STRING "Build number, used as the patch version (CI run number)")
set(TUTTI_COMMIT "dev" CACHE STRING "Git commit the build was made from")
set(TUTTI_VERSION "${TUTTI_VERSION_BASE}.${TUTTI_BUILD_NUMBER}")

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
endif()

project(Tutti VERSION ${TUTTI_VERSION} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

option(TUTTI_BUILD_TESTS "Build the Catch2 test suites" ON)
option(TUTTI_BUILD_APP "Build the Tutti app" ON)
option(TUTTI_BUILD_PLUGIN "Build the Tutti Stand-in plugin" ON)
option(TUTTI_BUILD_TOOLS "Build tutti-peer (headless Tutti for end-to-end tests)" ON)

include(cmake/TuttiDeps.cmake)

set(TUTTI_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
configure_file(core/TuttiVersion.h.in "${TUTTI_GENERATED_DIR}/TuttiVersion.h" @ONLY)

add_subdirectory(core)
add_subdirectory(engine)

if(TUTTI_BUILD_APP)
    add_subdirectory(app)
endif()

if(TUTTI_BUILD_PLUGIN AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/plugin/CMakeLists.txt)
    add_subdirectory(plugin)
endif()

if(TUTTI_BUILD_TOOLS)
    add_subdirectory(tools/cli)
endif()

if(TUTTI_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
