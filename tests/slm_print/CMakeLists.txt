get_filename_component(_TEST_NAME ${CMAKE_CURRENT_LIST_DIR} NAME)
add_executable(${_TEST_NAME}_tests 
	${_TEST_NAME}_tests_main.cpp
	slm_test_utils.cpp
	test_slm_config.cpp
	test_slm_slicing.cpp
	test_slm_hatch_patterns.cpp
	test_slm_scan_paths.cpp
	test_slm_integration.cpp
	)
target_link_libraries(${_TEST_NAME}_tests PRIVATE test_common libslic3r)
# Link against SLM library and translator libraries (for Writer classes)
if (TARGET SLM_static)
    target_link_libraries(${_TEST_NAME}_tests PRIVATE SLM_static)
    # Link translator libraries (static versions when BUILD_PYTHON is ON)
    if (TARGET SLMSol_static)
        target_link_libraries(${_TEST_NAME}_tests PRIVATE SLMSol_static MTT_static EOS_static CLI_static Realizer_static)
    endif()
elseif (TARGET SLM)
    target_link_libraries(${_TEST_NAME}_tests PRIVATE SLM)
    # Link translator libraries (shared versions when BUILD_PYTHON is OFF)
    if (TARGET SLMSol)
        target_link_libraries(${_TEST_NAME}_tests PRIVATE SLMSol MTT EOS CLI Realizer)
    endif()
endif()
set_property(TARGET ${_TEST_NAME}_tests PROPERTY FOLDER "tests")
target_compile_definitions(${_TEST_NAME}_tests PUBLIC CATCH_CONFIG_ENABLE_BENCHMARKING)

if (WIN32)
    prusaslicer_copy_dlls(${_TEST_NAME}_tests)
endif()

# catch_discover_tests(${_TEST_NAME}_tests TEST_PREFIX "${_TEST_NAME}: ")
add_test(${_TEST_NAME}_tests ${_TEST_NAME}_tests ${CATCH_EXTRA_ARGS})

