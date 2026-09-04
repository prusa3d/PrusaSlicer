add_library(slic3r-biz-parser STATIC
    include/Slic3r/Biz/Parser/PlaceholderParser.hpp
    src/Slic3r/Biz/Parser/PlaceholderParser.cpp
    include/Slic3r/Biz/Parser/IO.hpp
    src/Slic3r/Biz/Parser/IO.cpp
)

target_include_directories(slic3r-biz-parser PUBLIC include)
target_include_directories(slic3r-biz-parser PRIVATE src)

target_link_libraries(slic3r-biz-parser PRIVATE localesutils)
target_link_libraries(slic3r-biz-parser PUBLIC slic3r-domain)
target_compile_definitions(slic3r-biz-parser PRIVATE SLIC3R_VERSION="3.0.0")


if(SLIC3R_BUILD_TESTS)
    add_executable(slic3r-biz-parser-tests
        test/Slic3r/Biz/Parser/IOTests.cpp
        test/Slic3r/Biz/Parser/ParserTests.cpp
    )
    target_include_directories(slic3r-biz-parser-tests PRIVATE test)

    target_link_libraries(slic3r-biz-parser-tests PRIVATE slic3r-biz-parser Catch2::Catch2WithMain)

    set(TEST_DATA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/test/data)
    file(TO_NATIVE_PATH "${TEST_DATA_DIR}" TEST_DATA_DIR)
    target_compile_definitions(slic3r-biz-parser-tests PRIVATE TEST_DATA_DIR=R"\(${TEST_DATA_DIR}\)")

    add_test(NAME slic3r-biz-parser-tests
        COMMAND slic3r-biz-parser-tests ${CATCH_ALL_ARGS}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
endif()
