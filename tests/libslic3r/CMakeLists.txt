get_filename_component(_TEST_NAME ${CMAKE_CURRENT_LIST_DIR} NAME)

add_executable(${_TEST_NAME}_tests 
	${_TEST_NAME}_tests.cpp
    test_line.cpp
    test_point.cpp
	test_3mf.cpp
	test_aabbindirect.cpp
	test_kdtreeindirect.cpp
	test_arachne.cpp
	test_arc_welder.cpp
	test_clipper_offset.cpp
	test_clipper_utils.cpp
	test_color.cpp
	test_config.cpp
	test_curve_fitting.cpp
	test_cut_surface.cpp
	test_elephant_foot_compensation.cpp
	test_expolygon.cpp
	test_geometry.cpp
	test_placeholder_parser.cpp
	test_polygon.cpp
	test_polyline.cpp
	test_mutable_polygon.cpp
	test_mutable_priority_queue.cpp
	test_stl.cpp
	test_meshboolean.cpp
	test_marchingsquares.cpp
    test_multiple_beds.cpp
	test_region_expansion.cpp
	test_timeutils.cpp
	test_utils.cpp
	test_voronoi.cpp
    test_optimizers.cpp
    test_png_io.cpp
    test_surface_mesh.cpp
    test_timeutils.cpp
        test_quadric_edge_collapse.cpp
    test_triangulation.cpp
    test_emboss.cpp
    test_indexed_triangle_set.cpp
    test_astar.cpp
    test_anyptr.cpp
    test_jump_point_search.cpp
    test_support_spots_generator.cpp
    test_layer_region.cpp
    ../data/prusaparts.cpp
    ../data/prusaparts.hpp
     test_static_map.cpp
	)

if (TARGET OpenVDB::openvdb)
    target_sources(${_TEST_NAME}_tests PRIVATE test_hollowing.cpp)
endif()
    
target_link_libraries(${_TEST_NAME}_tests test_common libslic3r)
set_property(TARGET ${_TEST_NAME}_tests PROPERTY FOLDER "tests")

if (WIN32)
    prusaslicer_copy_dlls(${_TEST_NAME}_tests)
endif()

# catch_discover_tests(${_TEST_NAME}_tests TEST_PREFIX "${_TEST_NAME}: ")
add_test(${_TEST_NAME}_tests ${_TEST_NAME}_tests ${CATCH_EXTRA_ARGS})
