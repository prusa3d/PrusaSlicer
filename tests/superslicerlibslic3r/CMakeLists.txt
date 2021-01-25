get_filename_component(_TEST_NAME ${CMAKE_CURRENT_LIST_DIR} NAME)

add_executable(${_TEST_NAME}_tests 
	${_TEST_NAME}_tests.cpp
	test_complete_objects.cpp
    # test_fill.cpp
    # test_flow.cpp
    # test_gcodewriter.cpp
    # test_geometry.cpp
    # test_model.cpp
    test_print.cpp
    # test_thin.cpp
	# test_denserinfill.cpp
	# test_extrusion_entity.cpp
	# test_skirt_brim.cpp
	test_data.hpp
	test_data.cpp
	# test_clipper_utils.cpp
	test_dense_infill.cpp
	)

    
target_link_libraries(${_TEST_NAME}_tests test_common libslic3r)
set_property(TARGET ${_TEST_NAME}_tests PROPERTY FOLDER "tests")

if (WIN32)
    prusaslicer_copy_dlls(${_TEST_NAME}_tests)
endif()

# catch_discover_tests(${_TEST_NAME}_tests TEST_PREFIX "${_TEST_NAME}: ")
add_test(${_TEST_NAME}_tests ${_TEST_NAME}_tests ${CATCH_EXTRA_ARGS})
