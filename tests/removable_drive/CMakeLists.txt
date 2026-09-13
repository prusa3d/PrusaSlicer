get_filename_component(_TEST_NAME ${CMAKE_CURRENT_LIST_DIR} NAME)

add_executable(${_TEST_NAME}_tests
    ${_TEST_NAME}_tests_main.cpp
    test_mount_info.cpp
)

target_link_libraries(${_TEST_NAME}_tests test_common slic3r-shared)

# The parser lives next to the code that uses it rather than in the public
# headers, since nothing outside this module has any use for it.
target_include_directories(${_TEST_NAME}_tests PRIVATE "$<TARGET_PROPERTY:slic3r-shared,SOURCE_DIR>/src")

set_property(TARGET ${_TEST_NAME}_tests PROPERTY FOLDER "tests")

add_test(${_TEST_NAME}_tests ${_TEST_NAME}_tests ${CATCH_EXTRA_ARGS})
