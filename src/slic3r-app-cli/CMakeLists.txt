set(SLIC3R_APP_CLI_FILES
        src/Slic3r/App/CLI/CLIApp.cpp
        include/Slic3r/App/CLI/CLIApp.hpp
        src/Slic3r/App/CLI/CLIRuntime.cpp
        include/Slic3r/App/CLI/CLIRuntime.hpp
        src/Slic3r/App/CLI/CLIUtils.cpp
        include/Slic3r/App/CLI/CLIUtils.hpp
        src/Slic3r/App/CLI/ProcessTransform.cpp
        include/Slic3r/App/CLI/ProcessTransform.hpp
        src/Slic3r/App/CLI/ProcessActions.cpp
        include/Slic3r/App/CLI/ProcessActions.hpp
        src/Slic3r/App/CLI/LoadPrintData.cpp
        include/Slic3r/App/CLI/LoadPrintData.hpp
        src/Slic3r/App/CLI/ProfilesSharingUtils.cpp
        include/Slic3r/App/CLI/ProfilesSharingUtils.hpp
        src/Slic3r/App/CLI/ProfilePresetSelection.cpp
        include/Slic3r/App/CLI/ProfilePresetSelection.hpp
)


add_library(slic3r-app-cli ${SLIC3R_APP_CLI_FILES})

target_link_libraries(slic3r-app-cli PUBLIC
        slic3r-shared
        slic3r-biz-arrange
)

target_include_directories(slic3r-app-cli PUBLIC include)

# TODO: This temporarily allows using private header files from libslic3r in CLI.
target_include_directories(slic3r-app-cli PUBLIC "$<TARGET_PROPERTY:libslic3r,SOURCE_DIR>/src")

install(TARGETS slic3r-app-cli RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

if (SLIC3R_BUILD_TESTS)
    add_executable(slic3r-app-cli-tests
            test/main.cpp
            test/Slic3r/App/CLI/CLITestUtils.hpp
            test/Slic3r/App/CLI/CLITestUtils.cpp
            test/Slic3r/App/CLI/CLISliceTests.cpp
    )

    target_include_directories(slic3r-app-cli-tests PRIVATE test src)
    target_link_libraries(slic3r-app-cli-tests PRIVATE slic3r-app-cli Catch2::Catch2)

    # The tests run the CLI against the real preset bundle from the repository.
    set(CLI_TEST_RESOURCES_DIR ${CMAKE_SOURCE_DIR}/resources)
    file(TO_NATIVE_PATH "${CLI_TEST_RESOURCES_DIR}" CLI_TEST_RESOURCES_DIR)

    target_compile_definitions(slic3r-app-cli-tests PRIVATE CLI_TEST_RESOURCES_DIR=R"\(${CLI_TEST_RESOURCES_DIR}\)")

    add_test(NAME slic3r-app-cli-tests
            COMMAND slic3r-app-cli-tests ${CATCH_ALL_ARGS}
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )

    if (WIN32)
        prusaslicer_copy_dlls(slic3r-app-cli-tests)
    endif ()

endif ()
