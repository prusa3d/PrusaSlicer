find_package(Z3 REQUIRED)
slic3r_remap_configs("z3::libz3" RelWithDebInfo Release)



add_library(libseqarrange STATIC src/seq_interface.cpp src/seq_preprocess.cpp src/seq_sequential.cpp src/seq_utilities.cpp)
target_include_directories(libseqarrange PUBLIC include PRIVATE src )
target_link_libraries(libseqarrange PUBLIC libslic3r PRIVATE z3::libz3)

add_executable(sequential_decimator src/sequential_decimator.cpp)
target_include_directories(sequential_decimator PRIVATE include)
target_link_libraries(sequential_decimator PRIVATE libseqarrange)





if (SLIC3R_BUILD_TESTS)
    find_package(Catch2 3.8 REQUIRED)

    add_executable(libseqarrange_tests test/prusaparts.cpp test/seq_test_polygon.cpp test/seq_test_sequential.cpp test/seq_test_preprocess.cpp test/seq_test_interface.cpp)
    target_include_directories(libseqarrange_tests PRIVATE src )
    target_link_libraries(libseqarrange_tests PRIVATE Catch2::Catch2WithMain libseqarrange)

    set(_catch_args "exclude:[NotWorking] exclude:[Slow]")
    list(APPEND _catch_args "${CATCH_EXTRA_ARGS}")
    add_test(NAME libseqarrange_tests
             COMMAND libseqarrange_tests ${_catch_args}
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

endif()




