get_filename_component(_TEST_NAME ${CMAKE_CURRENT_LIST_DIR} NAME)
add_executable(${_TEST_NAME}_tests 
	${_TEST_NAME}_tests.cpp
	test_path_planning.cpp
	test_pattern_generation.cpp
	test_collision_detection.cpp
	test_gcode_generation.cpp
	test_material_database.cpp
	test_variable_speed.cpp
	test_integration_pipeline.cpp
	test_integration_models.cpp
	test_integration_edge_cases.cpp
	test_integration_layer_heights.cpp
	test_integration_multi_object.cpp
	test_gcode_validation.cpp
	test_gcode_patterns.cpp
	test_gcode_methods.cpp
	test_gcode_performance.cpp
	../fff_print/test_data.cpp
	)
target_link_libraries(${_TEST_NAME}_tests test_common libslic3r slic3r-arrange-wrapper)
set_property(TARGET ${_TEST_NAME}_tests PROPERTY FOLDER "tests")
target_compile_definitions(${_TEST_NAME}_tests PUBLIC CATCH_CONFIG_ENABLE_BENCHMARKING)

if (WIN32)
    prusaslicer_copy_dlls(${_TEST_NAME}_tests)
endif()

# catch_discover_tests(${_TEST_NAME}_tests TEST_PREFIX "${_TEST_NAME}: ")
add_test(${_TEST_NAME}_tests ${_TEST_NAME}_tests ${CATCH_EXTRA_ARGS})

