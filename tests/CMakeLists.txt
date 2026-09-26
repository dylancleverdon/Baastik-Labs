add_executable(baastik_tests
    test_content.cpp
    test_generator.cpp
    test_preset_file.cpp
    test_text_to_synth.cpp)
target_link_libraries(baastik_tests PRIVATE baastik::serum2 baastik::serumgen baastik::texttosynth Catch2::Catch2WithMain)
baastik_set_warnings(baastik_tests)
target_compile_definitions(baastik_tests PRIVATE
    BAASTIK_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

include(Catch)
catch_discover_tests(baastik_tests)
