cmake_minimum_required(VERSION 3.22)

# Version = MAJOR.MINOR from the VERSION file + a build number (the CI run number).
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" NORG_VERSION_BASE LIMIT_COUNT 1)
set(NORG_BUILD_NUMBER "0" CACHE STRING "Build number, used as the patch version (CI run number)")
set(NORG_COMMIT "dev" CACHE STRING "Git commit the build was made from")
set(NORG_VERSION "${NORG_VERSION_BASE}.${NORG_BUILD_NUMBER}")

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
endif()

project(BaastikLabs VERSION ${NORG_VERSION} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

option(NORG_BUILD_TESTS "Build the Catch2 test suites" ON)
option(NORG_COPY_AFTER_BUILD "Copy plugins into the user plug-in folders after building" OFF)

include(cmake/NorgDeps.cmake)

add_subdirectory(Norg)
