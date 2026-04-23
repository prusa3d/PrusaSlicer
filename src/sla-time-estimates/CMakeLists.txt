cmake_minimum_required(VERSION 3.1)

project(sla_time_estimates)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(nlohmann_json REQUIRED)

add_library(sla_time_estimate STATIC "${CMAKE_CURRENT_SOURCE_DIR}/src/SLATimeEstimate.cpp")
target_include_directories(sla_time_estimate PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(sla_time_estimate PUBLIC nlohmann_json::nlohmann_json)

add_executable(sla_time_estimate_app main.cpp)
target_link_libraries(sla_time_estimate_app PRIVATE sla_time_estimate)
