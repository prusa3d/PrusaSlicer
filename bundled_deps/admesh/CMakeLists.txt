cmake_minimum_required(VERSION 2.8.12)
project(admesh)

add_library(admesh STATIC
    admesh/connect.cpp
    admesh/normals.cpp
    admesh/shared.cpp
    admesh/stl.h
    admesh/stl_io.cpp
    admesh/stlinit.cpp
    admesh/util.cpp
)

target_include_directories(admesh PUBLIC .)
target_link_libraries(admesh PRIVATE boost_headeronly localesutils)
target_link_libraries(admesh PUBLIC Eigen3::Eigen)
