add_executable(serumtool main.cpp)
target_link_libraries(serumtool PRIVATE baastik::serum2 baastik::serumgen)
baastik_set_warnings(serumtool)
