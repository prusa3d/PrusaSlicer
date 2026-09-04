project(slic3r-biz-cgal-algorithms)

add_library(slic3r-biz-cgal-algorithms STATIC
    include/Slic3r/Biz/CGAL/Algorithms/CutSurface.hpp
    src/Slic3r/Biz/CGAL/Algorithms/CutSurface.cpp
    include/Slic3r/Biz/CGAL/Algorithms/VoronoiUtilsCgal.hpp
    src/Slic3r/Biz/CGAL/Algorithms/VoronoiUtilsCgal.cpp
    include/Slic3r/Biz/CGAL/Algorithms/MeshBoolean.hpp
    src/Slic3r/Biz/CGAL/Algorithms/MeshBoolean.cpp
    include/Slic3r/Biz/CGAL/Algorithms/MergeObjectVolumes.hpp
    src/Slic3r/Biz/CGAL/Algorithms/MergeObjectVolumes.cpp
    src/Slic3r/Biz/CGAL/Algorithms/TryCatchSignal.hpp
    src/Slic3r/Biz/CGAL/Algorithms/TryCatchSignal.cpp
    src/Slic3r/Biz/CGAL/Algorithms/TryCatchSignalSEH.hpp
    include/Slic3r/Biz/CGAL/Algorithms/Triangulation.hpp
    src/Slic3r/Biz/CGAL/Algorithms/Triangulation.cpp
    include/Slic3r/Biz/CGAL/Algorithms/VoronoiDiagramCGAL.hpp
    src/Slic3r/Biz/CGAL/Algorithms/VoronoiDiagramCGAL.cpp
    include/Slic3r/Biz/CGAL/Algorithms/Voronoi.hpp
    src/Slic3r/Biz/CGAL/Algorithms/Voronoi.cpp
    include/Slic3r/Biz/CGAL/Algorithms/VoronoiOffset.hpp
    src/Slic3r/Biz/CGAL/Algorithms/VoronoiOffset.cpp
    include/Slic3r/Biz/CGAL/Algorithms/VoronoiGraph.hpp
    include/Slic3r/Biz/CGAL/Algorithms/VoronoiGraphUtils.hpp
    src/Slic3r/Biz/CGAL/Algorithms/VoronoiGraphUtils.cpp
    include/Slic3r/Biz/CGAL/Algorithms/VoronoiUtils.hpp
    src/Slic3r/Biz/CGAL/Algorithms/VoronoiUtils.cpp
    include/Slic3r/Biz/CGAL/Algorithms/VoronoiVisualUtils.hpp
    include/Slic3r/Biz/CGAL/Algorithms/EvaluateNeighbor.hpp
    src/Slic3r/Biz/CGAL/Algorithms/EvaluateNeighbor.cpp
    src/Slic3r/Biz/CGAL/Algorithms/ExpandNeighbor.cpp
    src/Slic3r/Biz/CGAL/Algorithms/ExpandNeighbor.hpp
    include/Slic3r/Biz/CGAL/Algorithms/PostProcessNeighbors.hpp
    src/Slic3r/Biz/CGAL/Algorithms/PostProcessNeighbors.cpp
    src/Slic3r/Biz/CGAL/Algorithms/PostProcessNeighbor.hpp
    src/Slic3r/Biz/CGAL/Algorithms/PostProcessNeighbor.cpp
    include/Slic3r/Biz/CGAL/Algorithms/NodeDataWithResult.hpp
    include/Slic3r/Biz/CGAL/Algorithms/ParabolaUtils.hpp
    src/Slic3r/Biz/CGAL/Algorithms/ParabolaUtils.cpp
)
target_include_directories(slic3r-biz-cgal-algorithms PUBLIC include)
target_include_directories(slic3r-biz-cgal-algorithms PRIVATE src)

if (APPLE AND CMAKE_OSX_DEPLOYMENT_TARGET VERSION_LESS "10.14")
    # See matching comment in slic3r-domain CMakeLists
    target_compile_options(slic3r-biz-cgal-algorithms PUBLIC "-fno-aligned-allocation")
endif ()

if (EMSCRIPTEN)
    # Emscripten has no support for rounding FP env
    # For discussion see:
    # https://github.com/emscripten-core/emscripten/issues/11086
    # https://github.com/WebAssembly/relaxed-simd/issues/19
    target_compile_definitions(slic3r-biz-cgal-algorithms PUBLIC -DCGAL_ALWAYS_ROUND_TO_NEAREST)
endif ()

if (SLIC3R_STATIC)
    set(CGAL_Boost_USE_STATIC_LIBS ON CACHE BOOL "" FORCE)
endif ()
set(CGAL_DO_NOT_WARN_ABOUT_CMAKE_BUILD_TYPE ON CACHE BOOL "" FORCE)

cmake_policy(PUSH)
cmake_policy(SET CMP0011 NEW)
find_package(CGAL REQUIRED)
cmake_policy(POP)

# Reset compile options of slic3r-biz-cgal-algorithms. Despite it being linked privately, CGAL options
# (-frounding-math) still propagate to dependent libs which is not desired.
get_target_property(_cgal_tgt CGAL::CGAL ALIASED_TARGET)
if (NOT TARGET ${_cgal_tgt})
    set (_cgal_tgt CGAL::CGAL)
endif ()
get_target_property(_opts ${_cgal_tgt} INTERFACE_COMPILE_OPTIONS)
if (_opts)
    set(_opts_bad "${_opts}")
    set(_opts_good "${_opts}")
    list(FILTER _opts_bad INCLUDE REGEX frounding-math)
    list(FILTER _opts_good EXCLUDE REGEX frounding-math)
    set_target_properties(${_cgal_tgt} PROPERTIES INTERFACE_COMPILE_OPTIONS "${_opts_good}")
    target_compile_options(slic3r-biz-cgal-algorithms PRIVATE "${_opts_bad}")
endif()


target_link_libraries(slic3r-biz-cgal-algorithms PRIVATE ${_cgal_tgt} libigl)
target_link_libraries(slic3r-biz-cgal-algorithms PUBLIC
    admesh
    localesutils
    slic3r-domain
    slic3r-biz-algorithms
    clipper
    clipper_z
)

if (MSVC AND "${CMAKE_SIZEOF_VOID_P}" STREQUAL "4") # 32 bit MSVC workaround
    target_compile_definitions(slic3r-biz-cgal-algorithms PRIVATE CGAL_DO_NOT_USE_MPZF)
endif ()

if(SLIC3R_BUILD_TESTS)
    add_executable(slic3r-biz-cgal-algorithms-tests
        test/Slic3r/Biz/CGAL/Algorithms/CGALTests.cpp
    )
    target_include_directories(slic3r-biz-cgal-algorithms-tests PRIVATE test)
    target_link_libraries(slic3r-biz-cgal-algorithms-tests PRIVATE slic3r-biz-cgal-algorithms Catch2::Catch2WithMain)

    add_test(NAME slic3r-biz-cgal-algorithms-tests
        COMMAND slic3r-biz-cgal-algorithms-tests ${CATC_ALL_ARGS}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
endif()
