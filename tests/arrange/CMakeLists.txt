get_filename_component(_TEST_NAME ${CMAKE_CURRENT_LIST_DIR} NAME)
add_executable(${_TEST_NAME}_tests ${_TEST_NAME}_tests_main.cpp
    test_arrange.cpp
    test_arrange_integration.cpp
    ../data/prusaparts.cpp
)

target_link_libraries(${_TEST_NAME}_tests test_common slic3r-arrange-wrapper)
set_property(TARGET ${_TEST_NAME}_tests PROPERTY FOLDER "tests")

if (WIN32)
    prusaslicer_copy_dlls(${_TEST_NAME}_tests)
endif()

set(_catch_args "exclude:[NotWorking] exclude:[Slow]")
list(APPEND _catch_args "${CATCH_EXTRA_ARGS}")
add_test(${_TEST_NAME}_tests ${_TEST_NAME}_tests ${_catch_args})
