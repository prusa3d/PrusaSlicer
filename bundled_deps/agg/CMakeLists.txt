cmake_minimum_required(VERSION 3.13)
project(agg)

add_library(agg INTERFACE)

# Include directories as if it was a system library to suppress warnings.
target_include_directories(agg SYSTEM INTERFACE .)
