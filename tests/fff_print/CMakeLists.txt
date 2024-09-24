get_filename_component(_TEST_NAME ${CMAKE_CURRENT_LIST_DIR} NAME)
add_executable(${_TEST_NAME}_tests 
	${_TEST_NAME}_tests.cpp
	test_avoid_crossing_perimeters.cpp
	test_bridges.cpp
	test_cooling.cpp
	test_clipper.cpp
	test_custom_gcode.cpp
	test_data.cpp
	test_data.hpp
	test_extrusion_entity.cpp
	test_fill.cpp
	test_flow.cpp
	test_gaps.cpp
	test_gcode.cpp
	test_gcode_travels.cpp
    test_seam_perimeters.cpp
    test_seam_shells.cpp
    test_seam_geometry.cpp
    test_seam_aligned.cpp
    test_seam_rear.cpp
    test_seam_random.cpp
    benchmark_seams.cpp
	test_gcodefindreplace.cpp
	test_gcodewriter.cpp
	test_cancel_object.cpp
    test_layers.cpp
	test_model.cpp
	test_multi.cpp
	test_perimeters.cpp
	test_print.cpp
	test_printgcode.cpp
	test_printobject.cpp
    test_retraction.cpp
	test_shells.cpp
	test_skirt_brim.cpp
	test_support_material.cpp
	test_thin_walls.cpp
	test_trianglemesh.cpp
	)
target_link_libraries(${_TEST_NAME}_tests test_common libslic3r)
set_property(TARGET ${_TEST_NAME}_tests PROPERTY FOLDER "tests")
target_compile_definitions(${_TEST_NAME}_tests PUBLIC CATCH_CONFIG_ENABLE_BENCHMARKING)

if (WIN32)
    prusaslicer_copy_dlls(${_TEST_NAME}_tests)
endif()

# catch_discover_tests(${_TEST_NAME}_tests TEST_PREFIX "${_TEST_NAME}: ")
add_test(${_TEST_NAME}_tests ${_TEST_NAME}_tests ${CATCH_EXTRA_ARGS})
