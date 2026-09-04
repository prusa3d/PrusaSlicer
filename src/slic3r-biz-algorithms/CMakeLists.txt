project(slic3r-biz-algorithms)

add_library(slic3r-biz-algorithms STATIC
    include/Slic3r/Biz/Algorithms/BoundingBox.hpp
    src/Slic3r/Biz/Algorithms/BoundingBox.cpp
    include/Slic3r/Biz/Algorithms/AABBMesh.hpp
    src/Slic3r/Biz/Algorithms/AABBMesh.cpp
    include/Slic3r/Biz/Algorithms/DouglasPeucker.hpp
    src/Slic3r/Biz/Algorithms/DouglasPeucker.cpp
    include/Slic3r/Biz/Algorithms/ExPolygon.hpp
    src/Slic3r/Biz/Algorithms/ExPolygon.cpp
    include/Slic3r/Biz/Algorithms/ExPolygonsWithId.hpp
    src/Slic3r/Biz/Algorithms/ExPolygonsWithId.cpp
    include/Slic3r/Biz/Algorithms/FacetsAnnotation.hpp
    src/Slic3r/Biz/Algorithms/FacetsAnnotation.cpp
    include/Slic3r/Biz/Algorithms/HealPolygon.hpp
    src/Slic3r/Biz/Algorithms/HealPolygon.cpp
    src/Slic3r/Biz/Algorithms/Scaling.cpp
    include/Slic3r/Biz/Algorithms/Line.hpp
    src/Slic3r/Biz/Algorithms/Line.cpp
    include/Slic3r/Biz/Algorithms/LruCache.hpp
    src/Slic3r/Biz/Algorithms/LruCache.cpp
    include/Slic3r/Biz/Algorithms/MultiPoint.hpp
    src/Slic3r/Biz/Algorithms/MultiPoint.cpp
    include/Slic3r/Biz/Algorithms/Point.hpp
    src/Slic3r/Biz/Algorithms/Point.cpp
    include/Slic3r/Biz/Algorithms/Polygon.hpp
    src/Slic3r/Biz/Algorithms/Polygon.cpp
    include/Slic3r/Biz/Algorithms/Polyline.hpp
    src/Slic3r/Biz/Algorithms/Polyline.cpp
    include/Slic3r/Biz/Algorithms/Scaling.hpp
    src/Slic3r/Biz/Algorithms/Scaling.cpp
    include/Slic3r/Biz/Algorithms/SVG.hpp
    src/Slic3r/Biz/Algorithms/SVG.cpp
    include/Slic3r/Biz/Algorithms/PNGReadWrite.hpp
    src/Slic3r/Biz/Algorithms/PNGReadWrite.cpp    
    include/Slic3r/Biz/Algorithms/TravelingSalesman.hpp
    src/Slic3r/Biz/Algorithms/TravelingSalesman.cpp
    include/Slic3r/Biz/Algorithms/ClipperUtils.hpp
    src/Slic3r/Biz/Algorithms/ClipperUtils.cpp
    include/Slic3r/Biz/Algorithms/KDTreeIndirect.hpp
    include/Slic3r/Biz/Algorithms/MutablePriorityQueue.hpp
    include/Slic3r/Biz/Algorithms/Geometry/Geometry.hpp
    src/Slic3r/Biz/Algorithms/Geometry/Geometry.cpp
    include/Slic3r/Biz/Algorithms/Geometry/ConvexHull.hpp
    src/Slic3r/Biz/Algorithms/Geometry/ConvexHull.cpp
    include/Slic3r/Biz/Algorithms/Execution/Execution.hpp
    include/Slic3r/Biz/Algorithms/Execution/ExecutionSeq.hpp
    include/Slic3r/Biz/Algorithms/Execution/ExecutionTBB.hpp
    src/Slic3r/Biz/Algorithms/Tesselate.cpp
    include/Slic3r/Biz/Algorithms/Tesselate.hpp
    src/Slic3r/Biz/Algorithms/TriangleMesh.cpp
    include/Slic3r/Biz/Algorithms/TriangleMesh.hpp
    src/Slic3r/Biz/Algorithms/TriangleSelector.cpp
    include/Slic3r/Biz/Algorithms/TriangleSelector.hpp
    src/Slic3r/Biz/Algorithms/TriangleSelectorPainter.cpp
    include/Slic3r/Biz/Algorithms/TriangleSelectorPainter.hpp
    src/Slic3r/Biz/Algorithms/QuadricEdgeCollapse.cpp
    include/Slic3r/Biz/Algorithms/QuadricEdgeCollapse.hpp
    src/Slic3r/Biz/Algorithms/ModelInstance.cpp
    include/Slic3r/Biz/Algorithms/ModelInstance.hpp
    src/Slic3r/Biz/Algorithms/Model.cpp
    include/Slic3r/Biz/Algorithms/Model.hpp
    src/Slic3r/Biz/Algorithms/ModelObject.cpp
    include/Slic3r/Biz/Algorithms/ModelObject.hpp
    src/Slic3r/Biz/Algorithms/ModelVolume.cpp
    include/Slic3r/Biz/Algorithms/ModelVolume.hpp
    src/Slic3r/Biz/Algorithms/Color.cpp
    include/Slic3r/Biz/Algorithms/Color.hpp
    include/Slic3r/Biz/Algorithms/StringUtils.hpp
    src/Slic3r/Biz/Algorithms/StringUtils.cpp
    include/Slic3r/Biz/Algorithms/AABBTreeIndirect.hpp
    include/Slic3r/Biz/Algorithms/IntersectionPoints.hpp
    src/Slic3r/Biz/Algorithms/IntersectionPoints.cpp
    include/Slic3r/Biz/Algorithms/LineUtils.hpp
    src/Slic3r/Biz/Algorithms/LineUtils.cpp
    include/Slic3r/Biz/Algorithms/PointUtils.hpp
    src/Slic3r/Biz/Algorithms/PointUtils.cpp
    include/Slic3r/Biz/Algorithms/Geometry/Circle.hpp
    src/Slic3r/Biz/Algorithms/Geometry/Circle.cpp
    include/Slic3r/Biz/Algorithms/PolygonsPointIndex.hpp
    include/Slic3r/Biz/Algorithms/PolygonsSegmentIndex.hpp
    include/Slic3r/Biz/Algorithms/Parabola.hpp
    include/Slic3r/Biz/Algorithms/PolygonUtils.hpp
    src/Slic3r/Biz/Algorithms/PolygonUtils.cpp
    include/Slic3r/Biz/Algorithms/IStackFunction.hpp
    include/Slic3r/Biz/Algorithms/Projection.hpp
    include/Slic3r/Biz/Algorithms/ImageUtils.hpp
    src/Slic3r/Biz/Algorithms/ImageUtils.cpp
    include/Slic3r/Biz/Algorithms/Bed.hpp
    src/Slic3r/Biz/Algorithms/Bed.cpp
    include/Slic3r/Biz/Algorithms/LayerHeight.hpp
    src/Slic3r/Biz/Algorithms/LayerHeight.cpp
    include/Slic3r/Biz/Algorithms/Optimize/NLoptOptimizer.hpp
    include/Slic3r/Biz/Algorithms/Optimize/Optimizer.hpp
    include/Slic3r/Biz/Algorithms/Optimize/BruteforceOptimizer.hpp
    include/Slic3r/Biz/Algorithms/BoostAdapter.hpp
    include/Slic3r/Biz/Algorithms/MiniZWrapper.hpp
    src/Slic3r/Biz/Algorithms/MiniZWrapper.cpp
    include/Slic3r/Biz/Algorithms/VirtualExtruder.hpp
    src/Slic3r/Biz/Algorithms/VirtualExtruder.cpp
    include/Slic3r/Biz/Algorithms/VirtualExtruderPresets.hpp
    src/Slic3r/Biz/Algorithms/VirtualExtruderPresets.cpp
)
target_include_directories(slic3r-biz-algorithms PUBLIC include)
target_include_directories(slic3r-biz-algorithms PRIVATE src)

find_package(Qhull 7.2 REQUIRED)
add_library(qhull INTERFACE)
if(SLIC3R_STATIC)
    slic3r_remap_configs("Qhull::qhullcpp;Qhull::qhullstatic_r" RelWithDebInfo Release)
    target_link_libraries(qhull INTERFACE Qhull::qhullcpp Qhull::qhullstatic_r)
else()
    slic3r_remap_configs("Qhull::qhullcpp;Qhull::qhull_r" RelWithDebInfo Release)
    target_link_libraries(qhull INTERFACE Qhull::qhullcpp Qhull::qhull_r)
endif()

find_package(prusa_fdm_mixer CONFIG REQUIRED)
slic3r_remap_configs(prusa_fdm_mixer::prusa_fdm_mixer RelWithDebInfo Release)

target_link_libraries(slic3r-biz-algorithms PRIVATE localesutils glu-libtess qhull prusa_fdm_mixer::prusa_fdm_mixer)
target_link_libraries(slic3r-biz-algorithms PUBLIC
    slic3r-domain
    clipper
    admesh
    libigl
    boost_libs
    stb_image
    stb_dxt
    PNG::PNG
    miniz
)

if (SLIC3R_PCH)
    target_precompile_headers(slic3r-biz-algorithms REUSE_FROM slic3r-domain)
endif()

if(SLIC3R_BUILD_TESTS)
    add_executable(slic3r-biz-algorithms-tests
        test/Slic3r/Biz/Algorithms/ScalingTests.cpp
        test/Slic3r/Biz/Algorithms/BoundingBoxTests.cpp
        test/Slic3r/Biz/Algorithms/BedContainmentTests.cpp
        test/Slic3r/Biz/Algorithms/OptimizersTests.cpp

    )
    target_include_directories(slic3r-biz-algorithms-tests PRIVATE test)

    find_package(Catch2 3.8 REQUIRED)
    slic3r_remap_configs(Catch2::Catch2 RelWithDebInfo Release)
    slic3r_remap_configs(Catch2::Catch2WithMain RelWithDebInfo Release)

    target_link_libraries(slic3r-biz-algorithms-tests PRIVATE slic3r-biz-algorithms Catch2::Catch2WithMain TBB::tbb)

    list(APPEND _catch_args "${CATCH_EXTRA_ARGS}")
    add_test(NAME slic3r-biz-algorithms-tests
        COMMAND slic3r-biz-algorithms-tests ${_catch_args}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
endif()
