# Third-party dependencies, pinned and fetched at configure time.
# To reuse a local checkout, pass e.g. -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE.
include(FetchContent)

set(TUTTI_JUCE_TAG "9.0.2" CACHE STRING "JUCE git tag")

FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        ${TUTTI_JUCE_TAG}
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

# nlohmann::json for the document, ops and protocol messages.
FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
set(JSON_BuildTests OFF CACHE INTERNAL "")
# No implicit json -> T conversions: they make comparisons ambiguous under MSVC's C++20
# rewritten operators. Use .get<T>() explicitly.
set(JSON_ImplicitConversions OFF CACHE INTERNAL "")
set(JSON_Install OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(nlohmann_json)

if(TUTTI_BUILD_TESTS)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.9.1
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(Catch2)
    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
    include(CTest)
    include(Catch)
endif()
