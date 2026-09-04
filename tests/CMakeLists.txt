# TODO Add individual tests as executables in separate directories
# add_subirectory(<testcase>)

find_package(Catch2 3.8 REQUIRED)
slic3r_remap_configs(Catch2::Catch2 RelWithDebInfo Release)
slic3r_remap_configs(Catch2::Catch2WithMain RelWithDebInfo Release)

include(Catch)

set(TEST_DATA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/data)
file(TO_NATIVE_PATH "${TEST_DATA_DIR}" TEST_DATA_DIR)

set(CATCH_EXTRA_ARGS "" CACHE STRING "Extra arguments for catch2 test suites.")

add_library(test_common INTERFACE)
target_include_directories(test_common INTERFACE ${CMAKE_CURRENT_LIST_DIR})

# TODO: This temporarily allows using private header files from libslic3r in all tests.
target_include_directories(test_common INTERFACE "$<TARGET_PROPERTY:libslic3r,SOURCE_DIR>/src")

target_compile_definitions(test_common INTERFACE TEST_DATA_DIR=R"\(${TEST_DATA_DIR}\)" CATCH_CONFIG_FAST_COMPILE)
target_link_libraries(test_common INTERFACE slic3r-test-utils Catch2::Catch2WithMain)

if (APPLE)
    target_link_libraries(test_common INTERFACE "-liconv -framework IOKit" "-framework CoreFoundation" -lc++)
endif()

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

add_subdirectory(thumbnails)
add_subdirectory(libslic3r)
add_subdirectory(fff_print)
add_subdirectory(sla_print)
