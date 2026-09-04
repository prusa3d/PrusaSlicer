cmake_minimum_required(VERSION 3.13)
project(clipper)

add_library(clipper STATIC src/clipper.cpp)
target_link_libraries(clipper PRIVATE TBB::tbb TBB::tbbmalloc int128)
target_link_libraries(clipper PUBLIC slic3r-domain-types)
target_include_directories(clipper PUBLIC include)

add_library(clipper_z STATIC src/clipper.cpp)
target_compile_definitions(clipper_z PRIVATE CLIPPERLIB_USE_XYZ=1)
target_link_libraries(clipper_z PRIVATE TBB::tbb TBB::tbbmalloc int128)
target_link_libraries(clipper_z PUBLIC slic3r-domain-types)
target_include_directories(clipper_z PUBLIC include)
