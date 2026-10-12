get_filename_component(_TEST_NAME ${CMAKE_CURRENT_LIST_DIR} NAME)
add_executable(${_TEST_NAME}_tests
    ${_TEST_NAME}_tests_main.cpp
    slic3r_jobs_tests.cpp
    slic3r_version_tests.cpp
    slic3r_arrangejob_tests.cpp
    secretstore_tests.cpp
    bedmesh_tests.cpp
    filamentdb_tests.cpp
    filamentscanclient_tests.cpp
    printer_tools_tests.cpp
    filament_sync_tests.cpp
    prusalink_mock_tests.cpp
    )

# mold linker for successful linking needs also to link TBB library and link it before libslic3r.
target_link_libraries(${_TEST_NAME}_tests test_common TBB::tbb TBB::tbbmalloc libslic3r_gui libslic3r libseqarrange)

if (MSVC)
    target_link_libraries(${_TEST_NAME}_tests Setupapi.lib)
endif ()
set_property(TARGET ${_TEST_NAME}_tests PROPERTY FOLDER "tests")

if (WIN32)
    prusaslicer_copy_dlls(${_TEST_NAME}_tests)
endif()

# catch_discover_tests(${_TEST_NAME}_tests TEST_PREFIX "${_TEST_NAME}: ")
set(_catch_args "exclude:[NotWorking];-s")
list(APPEND _catch_args "${CATCH_EXTRA_ARGS}")
add_test(${_TEST_NAME}_tests ${_TEST_NAME}_tests ${_catch_args})

# Sync filaments from the printer: its unit tests, on their own so CI can run just them
add_test(NAME ${_TEST_NAME}_filament_sync COMMAND $<TARGET_FILE:${_TEST_NAME}_tests> "[PrinterTools],[FilamentSync]")
set_tests_properties(${_TEST_NAME}_filament_sync PROPERTIES LABELS filament_sync)

# The [PrusaLinkMock] tests are hidden and run against prusalink-mock
# (https://github.com/hyiger/prusalink-mock), a PrusaLink server that answers like the printer
# firmware. Its "run" wrapper starts it on free ports and passes the URLs to the tests.
set(PRUSALINK_MOCK_DIR "$ENV{PRUSALINK_MOCK_DIR}" CACHE PATH "prusalink-mock checkout for the [PrusaLinkMock] tests; empty looks next to this repository")
option(PRUSALINK_MOCK_REQUIRED "Fail to configure when the [PrusaLinkMock] tests can't be registered" OFF)
set(_plmock_dir "${PRUSALINK_MOCK_DIR}")
if (NOT _plmock_dir AND EXISTS "${PROJECT_SOURCE_DIR}/../prusalink-mock/src/prusalink_mock/cli.py")
    get_filename_component(_plmock_dir "${PROJECT_SOURCE_DIR}/../prusalink-mock" ABSOLUTE)
endif ()
find_package(Python3 3.10 COMPONENTS Interpreter)
if (Python3_Interpreter_FOUND AND EXISTS "${_plmock_dir}/src/prusalink_mock/cli.py")
    add_test(NAME ${_TEST_NAME}_prusalink_mock
        COMMAND ${Python3_EXECUTABLE} -m prusalink_mock run --scenario indx-hy8
                --log-file ${CMAKE_CURRENT_BINARY_DIR}/prusalink-mock.jsonl
                -- $<TARGET_FILE:${_TEST_NAME}_tests> "[PrusaLinkMock]" ${CATCH_EXTRA_ARGS})
    set_tests_properties(${_TEST_NAME}_prusalink_mock PROPERTIES
        ENVIRONMENT "PYTHONPATH=${_plmock_dir}/src"
        # Catch2 exits 4 when every selected test skipped
        SKIP_RETURN_CODE 4
        TIMEOUT 300
        LABELS prusalink_mock)
    message(STATUS "[PrusaLinkMock] tests: prusalink-mock at ${_plmock_dir}, ${Python3_EXECUTABLE}")
elseif (PRUSALINK_MOCK_REQUIRED)
    message(FATAL_ERROR "[PrusaLinkMock] tests: need Python 3.10 or newer (found: ${Python3_Interpreter_FOUND}) and prusalink-mock (PRUSALINK_MOCK_DIR: '${_plmock_dir}')")
else ()
    message(STATUS "[PrusaLinkMock] tests not registered: need Python 3.10 or newer and prusalink-mock (PRUSALINK_MOCK_DIR)")
endif ()
