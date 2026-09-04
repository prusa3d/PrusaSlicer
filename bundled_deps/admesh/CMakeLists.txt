cmake_minimum_required(VERSION 3.13)
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
target_link_libraries(admesh PRIVATE slic3r-base localesutils)
target_link_libraries(admesh PUBLIC slic3r-domain-types)
