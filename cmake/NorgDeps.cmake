# Third-party dependencies, fetched at configure time.
# To reuse a local checkout, pass e.g. -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE.
include(FetchContent)

set(NORG_JUCE_TAG "9.0.2" CACHE STRING "JUCE git tag")
set(NORG_CATCH2_TAG "v3.9.1" CACHE STRING "Catch2 git tag")

FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        ${NORG_JUCE_TAG}
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

if(NORG_BUILD_TESTS)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        ${NORG_CATCH2_TAG}
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(Catch2)
    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
    include(CTest)
    include(Catch)
endif()
