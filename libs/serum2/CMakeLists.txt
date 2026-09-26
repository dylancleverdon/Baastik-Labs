add_library(serum2 STATIC
    src/Md5.cpp
    src/Patch.cpp
    src/PresetFile.cpp
    src/Schema.cpp)
add_library(baastik::serum2 ALIAS serum2)

target_include_directories(serum2 PUBLIC include)
target_link_libraries(serum2
    PUBLIC nlohmann_json::nlohmann_json
    PRIVATE zstd::libzstd_static)
baastik_set_warnings(serum2)

baastik_embed_resources(serum2
    NAMESPACE serum2::embedded
    BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/data"
    FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/data/init_template.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/data/multisamples.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/data/serum2_schema.json")
