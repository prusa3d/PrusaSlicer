#/|/ Copyright (c) Prusa Research 2018 - 2023 Tomáš Mészáros @tamasmeszaros, Oleksandra Iushchenko @YuSanka, Enrico Turri @enricoturri1966, Vojtěch Bubník @bubnikv, Pavel Mikuš @Godrak, Lukáš Hejl @hejllukas, Lukáš Matěna @lukasmatena, Filip Sykala @Jony01, David Kocík @kocikdav, Vojtěch Král @vojtechkral
#/|/ Copyright (c) BambuStudio 2023 manch1n @manch1n
#/|/ Copyright (c) 2023 Mimoja @Mimoja
#/|/ Copyright (c) 2022 ole00 @ole00
#/|/
#/|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
#/|/
cmake_minimum_required(VERSION 3.13)
project(libslic3r)

include(PrecompiledHeader)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/libslic3r_version.h.in ${CMAKE_CURRENT_BINARY_DIR}/libslic3r_version.h @ONLY)

if (MINGW)
    add_compile_options(-Wa,-mbig-obj)
endif ()

set(OpenVDBUtils_SOURCES "")
if (TARGET OpenVDB::openvdb)
    set(OpenVDBUtils_SOURCES OpenVDBUtils.cpp OpenVDBUtils.hpp OpenVDBUtilsLegacy.hpp)
endif()

find_package(LibBGCode REQUIRED COMPONENTS Convert)
slic3r_remap_configs(LibBGCode::bgcode_core RelWithDebInfo Release)
slic3r_remap_configs(LibBGCode::bgcode_binarize RelWithDebInfo Release)
slic3r_remap_configs(LibBGCode::bgcode_convert RelWithDebInfo Release)

set(SLIC3R_SOURCES
    pchheader.cpp
    pchheader.hpp
    AStar.hpp
    AABBTreeIndirect.hpp
    AABBTreeLines.hpp
    AABBMesh.hpp
    AABBMesh.cpp
    ArrangeHelper.cpp
    ArrangeHelper.hpp
    Algorithm/LineSegmentation/LineSegmentation.cpp
    Algorithm/LineSegmentation/LineSegmentation.hpp
    Algorithm/PathSorting.hpp
    Algorithm/RegionExpansion.hpp
    Algorithm/RegionExpansion.cpp
    AnyPtr.hpp
    BoundingBox.cpp
    BoundingBox.hpp
    BridgeDetector.cpp
    BridgeDetector.hpp
    Brim.cpp
    Brim.hpp
    BuildVolume.cpp
    BuildVolume.hpp
    BoostAdapter.hpp
    clipper.cpp
    clipper.hpp
    ClipperUtils.cpp
    ClipperUtils.hpp
    ClipperZUtils.hpp
    Color.cpp
    Color.hpp
    Config.cpp
    Config.hpp
    CSGMesh/CSGMesh.hpp
    CSGMesh/SliceCSGMesh.hpp
    CSGMesh/ModelToCSGMesh.hpp
    CSGMesh/PerformCSGMeshBooleans.hpp
    CSGMesh/VoxelizeCSGMesh.hpp
    CSGMesh/TriangleMeshAdapter.hpp
    CSGMesh/CSGMeshCopy.hpp
    EdgeGrid.cpp
    EdgeGrid.hpp
    ElephantFootCompensation.cpp
    ElephantFootCompensation.hpp
    Emboss.cpp
    Emboss.hpp
    EmbossShape.hpp
    enum_bitmask.hpp
    ExPolygon.cpp
    ExPolygon.hpp
    ExPolygonSerialize.hpp
    ExPolygonsIndex.cpp
    ExPolygonsIndex.hpp
    Extruder.cpp
    Extruder.hpp
    ExtrusionEntity.cpp
    ExtrusionEntity.hpp
    ExtrusionEntityCollection.cpp
    ExtrusionEntityCollection.hpp
    ExtrusionRole.cpp
    ExtrusionRole.hpp
    ExtrusionSimulator.cpp
    ExtrusionSimulator.hpp
    Feature/Interlocking/InterlockingGenerator.cpp
    Feature/Interlocking/InterlockingGenerator.hpp
    Feature/Interlocking/VoxelUtils.cpp
    Feature/Interlocking/VoxelUtils.hpp
    FileParserError.hpp
    Feature/FuzzySkin/FuzzySkin.cpp
    Feature/FuzzySkin/FuzzySkin.hpp
    Fill/Fill.cpp
    Fill/Fill3DHoneycomb.cpp
    Fill/Fill3DHoneycomb.hpp
    Fill/FillAdaptive.cpp
    Fill/FillAdaptive.hpp
    Fill/FillBase.cpp
    Fill/FillBase.hpp
    Fill/FillConcentric.cpp
    Fill/FillConcentric.hpp
    Fill/FillEnsuring.cpp
    Fill/FillEnsuring.hpp
    Fill/FillHoneycomb.cpp
    Fill/FillHoneycomb.hpp
    Fill/FillGyroid.cpp
    Fill/FillGyroid.hpp
    Fill/FillPlanePath.cpp
    Fill/FillPlanePath.hpp
    Fill/FillLine.cpp
    Fill/FillLine.hpp
    Fill/FillLightning.cpp
    Fill/FillLightning.hpp
    Fill/Lightning/DistanceField.cpp
    Fill/Lightning/DistanceField.hpp
    Fill/Lightning/Generator.cpp
    Fill/Lightning/Generator.hpp
    Fill/Lightning/Layer.cpp
    Fill/Lightning/Layer.hpp
    Fill/Lightning/TreeNode.cpp
    Fill/Lightning/TreeNode.hpp
    Fill/FillRectilinear.cpp
    Fill/FillRectilinear.hpp
    Flow.cpp
    Flow.hpp
    format.hpp
    Format/3mf.cpp
    Format/3mf.hpp
    Format/AMF.cpp
    Format/AMF.hpp
    Format/OBJ.cpp
    Format/OBJ.hpp
    Format/objparser.cpp
    Format/objparser.hpp
    Format/STL.cpp
    Format/STL.hpp
    Format/SLAArchiveWriter.hpp
    Format/SLAArchiveWriter.cpp
    Format/SLAArchiveReader.hpp
    Format/SLAArchiveReader.cpp
    Format/ZipperArchiveImport.hpp
    Format/ZipperArchiveImport.cpp
    Format/SL1.hpp
    Format/SL1.cpp
    Format/SL1_SVG.hpp
    Format/SL1_SVG.cpp
    Format/AnycubicSLA.hpp
    Format/AnycubicSLA.cpp
    Format/STEP.hpp
    Format/STEP.cpp
    Format/SVG.hpp
    Format/SVG.cpp
    Format/SLAArchiveFormatRegistry.hpp
    Format/SLAArchiveFormatRegistry.cpp
    Format/PrintRequest.cpp
    Format/PrintRequest.cpp
    GCode/ThumbnailData.cpp
    GCode/ThumbnailData.hpp
    GCode/Thumbnails.cpp
    GCode/Thumbnails.hpp
    GCode/ConflictChecker.cpp
    GCode/ConflictChecker.hpp
    GCode/CoolingBuffer.cpp
    GCode/CoolingBuffer.hpp
    GCode/ExtrusionProcessor.cpp
    GCode/ExtrusionProcessor.hpp
    GCode/FindReplace.cpp
    GCode/FindReplace.hpp
    GCode/LabelObjects.cpp
    GCode/LabelObjects.hpp
    GCode/GCodeWriter.cpp
    GCode/GCodeWriter.hpp
    GCode/PostProcessor.cpp
    GCode/PostProcessor.hpp
    GCode/PressureEqualizer.cpp
    GCode/PressureEqualizer.hpp
    GCode/PrintExtents.cpp
    GCode/PrintExtents.hpp
    GCode/RetractWhenCrossingPerimeters.cpp
    GCode/RetractWhenCrossingPerimeters.hpp
    GCode/SpiralVase.cpp
    GCode/SpiralVase.hpp
    GCode/SeamPlacer.cpp
    GCode/SeamPlacer.hpp
    GCode/SeamChoice.cpp
    GCode/SeamChoice.hpp
    GCode/SeamPerimeters.cpp
    GCode/SeamPerimeters.hpp
    GCode/SeamShells.cpp
    GCode/SeamShells.hpp
    GCode/SeamGeometry.cpp
    GCode/SeamGeometry.hpp
    GCode/SeamAligned.cpp
    GCode/SeamAligned.hpp
    GCode/SeamRear.cpp
    GCode/SeamRear.hpp
    GCode/SeamRandom.cpp
    GCode/SeamRandom.hpp
    GCode/SeamPainting.cpp
    GCode/SeamPainting.hpp
    GCode/SeamScarf.cpp
    GCode/SeamScarf.hpp
    GCode/ModelVisibility.cpp
    GCode/ModelVisibility.hpp
    GCode/SmoothPath.cpp
    GCode/SmoothPath.hpp
    GCode/ToolOrdering.cpp
    GCode/ToolOrdering.hpp
    GCode/Wipe.cpp
    GCode/Wipe.hpp
    GCode/WipeTower.cpp
    GCode/WipeTower.hpp
    GCode/WipeTowerIntegration.cpp
    GCode/WipeTowerIntegration.hpp
    GCode/GCodeProcessor.cpp
    GCode/GCodeProcessor.hpp
    GCode/AvoidCrossingPerimeters.cpp
    GCode/AvoidCrossingPerimeters.hpp
    GCode/Travels.cpp
    GCode/Travels.hpp
    GCode/ExtrusionOrder.cpp
    GCode/ExtrusionOrder.hpp
    GCode.cpp
    GCode.hpp
    GCodeReader.cpp
    GCodeReader.hpp
    # GCodeSender.cpp
    # GCodeSender.hpp
    Geometry.cpp
    Geometry.hpp
    Geometry/ArcWelder.cpp
    Geometry/ArcWelder.hpp
    Geometry/Bicubic.hpp
    Geometry/Circle.cpp
    Geometry/Circle.hpp
    Geometry/ConvexHull.cpp
    Geometry/ConvexHull.hpp
    Geometry/Curves.hpp
    Geometry/MedialAxis.cpp
    Geometry/MedialAxis.hpp
    Geometry/Voronoi.hpp
    Geometry/VoronoiOffset.cpp
    Geometry/VoronoiOffset.hpp
    Geometry/VoronoiUtils.hpp
    Geometry/VoronoiUtils.cpp
    Geometry/VoronoiVisualUtils.hpp
    InfillAboveBridges.hpp
    InfillAboveBridges.cpp
    JumpPointSearch.cpp
    JumpPointSearch.hpp
    KDTreeIndirect.hpp
    Layer.cpp
    Layer.hpp
    LayerRegion.hpp
    LayerRegion.cpp
    libslic3r.h
    "${CMAKE_CURRENT_BINARY_DIR}/libslic3r_version.h"
    Line.cpp
    Line.hpp
    BlacklistedLibraryCheck.cpp
    BlacklistedLibraryCheck.hpp
    CutUtils.cpp
    CutUtils.hpp
    Model.cpp
    Model.hpp
    ModelProcessing.cpp
    ModelProcessing.hpp
    FileReader.cpp
    FileReader.hpp
    MultiMaterialSegmentation.cpp
    MultiMaterialSegmentation.hpp
    MeshNormals.hpp
    MeshNormals.cpp
    Measure.hpp
    Measure.cpp
    MeasureUtils.hpp
    CustomGCode.cpp
    CustomGCode.hpp
    MultiPoint.cpp
    MultiPoint.hpp
    MutablePriorityQueue.hpp
    NormalUtils.cpp
    NormalUtils.hpp
    NSVGUtils.cpp
    NSVGUtils.hpp
    ObjectID.cpp
    ObjectID.hpp
    PerimeterGenerator.cpp
    PerimeterGenerator.hpp
    PlaceholderParser.cpp
    PlaceholderParser.hpp
    Platform.cpp
    Platform.hpp
    Point.cpp
    Point.hpp
    Polygon.cpp
    Polygon.hpp
    MutablePolygon.cpp
    MutablePolygon.hpp
    PolygonTrimmer.cpp
    PolygonTrimmer.hpp
    Polyline.cpp
    Polyline.hpp
    Preset.cpp
    Preset.hpp
    PresetBundle.cpp
    PresetBundle.hpp
    PrincipalComponents2D.hpp
    PrincipalComponents2D.cpp
    AppConfig.cpp
    AppConfig.hpp
    Print.cpp
    Print.hpp
    PrintApply.cpp
    PrintBase.cpp
    PrintBase.hpp
    PrintConfig.cpp
    PrintConfig.hpp
    PrintObject.cpp
    PrintObjectSlice.cpp
    PrintRegion.cpp
    PointGrid.hpp
    PNGReadWrite.hpp
    PNGReadWrite.cpp
    QuadricEdgeCollapse.cpp
    QuadricEdgeCollapse.hpp
    Semver.cpp
    ShortEdgeCollapse.cpp
    ShortEdgeCollapse.hpp
    ShortestPath.cpp
    ShortestPath.hpp
    SLAPrint.cpp
    SLAPrintSteps.cpp
    SLAPrintSteps.hpp
    SLAPrint.hpp
    Slicing.cpp
    Slicing.hpp
    SlicesToTriangleMesh.hpp
    SlicesToTriangleMesh.cpp
    SlicingAdaptive.cpp
    SlicingAdaptive.hpp
    Subdivide.cpp
    Subdivide.hpp
    Support/SupportCommon.cpp
    Support/SupportCommon.hpp
    Support/SupportDebug.cpp
    Support/SupportDebug.hpp
    Support/SupportLayer.hpp
    Support/SupportMaterial.cpp
    Support/SupportMaterial.hpp
    Support/SupportParameters.cpp
    Support/SupportParameters.hpp
    Support/OrganicSupport.cpp
    Support/OrganicSupport.hpp
    Support/TreeSupport.cpp
    Support/TreeSupport.hpp
    Support/TreeSupportCommon.cpp
    Support/TreeSupportCommon.hpp
    Support/TreeModelVolumes.cpp
    Support/TreeModelVolumes.hpp
    SupportSpotsGenerator.cpp
    SupportSpotsGenerator.hpp
    Surface.cpp
    Surface.hpp
    SurfaceCollection.cpp
    SurfaceCollection.hpp
    SurfaceMesh.hpp
    SVG.cpp
    SVG.hpp
    Technologies.hpp
    Tesselate.cpp
    Tesselate.hpp
    TextConfiguration.hpp
    TriangleMesh.cpp
    TriangleMesh.hpp
    TriangleMeshSlicer.cpp
    TriangleMeshSlicer.hpp
    MeshSplitImpl.hpp
    TriangulateWall.hpp
    utils.cpp
    Utils.hpp
    Time.cpp
    Time.hpp
    Timer.cpp
    Timer.hpp
    Thread.cpp
    Thread.hpp
    TriangleSelector.cpp
    TriangleSelector.hpp
    TriangleSetSampling.cpp
    TriangleSetSampling.hpp
    TriangleSelectorWrapper.cpp
    TriangleSelectorWrapper.hpp
    MTUtils.hpp
    Zipper.hpp
    Zipper.cpp
    MinAreaBoundingBox.hpp
    MinAreaBoundingBox.cpp
    miniz_extension.hpp
    miniz_extension.cpp
    MarchingSquares.hpp
    MultipleBeds.cpp
    MultipleBeds.hpp
    Execution/Execution.hpp
    Execution/ExecutionSeq.hpp
    Execution/ExecutionTBB.hpp
    Optimize/Optimizer.hpp
    Optimize/NLoptOptimizer.hpp
    Optimize/BruteforceOptimizer.hpp
    ${OpenVDBUtils_SOURCES}
    SLA/Pad.hpp
    SLA/Pad.cpp
    SLA/SupportTreeBuilder.hpp
    SLA/SupportTreeMesher.hpp
    SLA/SupportTreeMesher.cpp
    SLA/SupportTreeUtils.hpp
    SLA/SupportTreeUtilsLegacy.hpp
    SLA/SupportTreeBuilder.cpp
    SLA/SupportTree.hpp
    SLA/SupportTree.cpp
    SLA/SupportTreeStrategies.hpp
#    SLA/SupportTreeIGL.cpp
    SLA/Rotfinder.hpp
    SLA/Rotfinder.cpp
    SLA/SpatIndex.hpp
    SLA/SpatIndex.cpp
    SLA/RasterBase.hpp
    SLA/RasterBase.cpp
    SLA/AGGRaster.hpp
    SLA/RasterToPolygons.hpp
    SLA/RasterToPolygons.cpp
    SLA/ConcaveHull.hpp
    SLA/ConcaveHull.cpp
    SLA/Hollowing.hpp
    SLA/Hollowing.cpp
    SLA/JobController.hpp
    SLA/SupportPoint.hpp
    SLA/SupportPointGenerator.hpp
    SLA/SupportPointGenerator.cpp
    SLA/Clustering.hpp
    SLA/Clustering.cpp
    SLA/ReprojectPointsOnMesh.hpp
    SLA/DefaultSupportTree.hpp
    SLA/DefaultSupportTree.cpp
    SLA/BranchingTreeSLA.hpp
    SLA/BranchingTreeSLA.cpp
    SLA/ZCorrection.hpp
    SLA/ZCorrection.cpp    
    SLA/SupportIslands/EvaluateNeighbor.cpp
    SLA/SupportIslands/EvaluateNeighbor.hpp
    SLA/SupportIslands/ExpandNeighbor.cpp
    SLA/SupportIslands/ExpandNeighbor.hpp
    SLA/SupportIslands/IStackFunction.hpp
    SLA/SupportIslands/LineUtils.cpp
    SLA/SupportIslands/LineUtils.hpp
    SLA/SupportIslands/NodeDataWithResult.hpp
    SLA/SupportIslands/Parabola.hpp
    SLA/SupportIslands/ParabolaUtils.cpp
    SLA/SupportIslands/ParabolaUtils.hpp
    SLA/SupportIslands/PointUtils.cpp
    SLA/SupportIslands/PointUtils.hpp
    SLA/SupportIslands/PolygonUtils.cpp
    SLA/SupportIslands/PolygonUtils.hpp
    SLA/SupportIslands/PostProcessNeighbor.cpp
    SLA/SupportIslands/PostProcessNeighbor.hpp
    SLA/SupportIslands/PostProcessNeighbors.cpp
    SLA/SupportIslands/PostProcessNeighbors.hpp
    SLA/SupportIslands/SampleConfig.hpp
    SLA/SupportIslands/SampleConfigFactory.cpp
    SLA/SupportIslands/SampleConfigFactory.hpp
    SLA/SupportIslands/SupportIslandPoint.cpp
    SLA/SupportIslands/SupportIslandPoint.hpp
    SLA/SupportIslands/UniformSupportIsland.cpp
    SLA/SupportIslands/UniformSupportIsland.hpp
    SLA/SupportIslands/VectorUtils.hpp
    SLA/SupportIslands/VoronoiGraph.hpp
    SLA/SupportIslands/VoronoiGraphUtils.cpp
    SLA/SupportIslands/VoronoiGraphUtils.hpp
    BranchingTree/BranchingTree.cpp
    BranchingTree/BranchingTree.hpp
    BranchingTree/PointCloud.cpp
    BranchingTree/PointCloud.hpp
    Arachne/BeadingStrategy/BeadingStrategy.hpp
    Arachne/BeadingStrategy/BeadingStrategy.cpp
    Arachne/BeadingStrategy/BeadingStrategyFactory.hpp
    Arachne/BeadingStrategy/BeadingStrategyFactory.cpp
    Arachne/BeadingStrategy/DistributedBeadingStrategy.hpp
    Arachne/BeadingStrategy/DistributedBeadingStrategy.cpp
    Arachne/BeadingStrategy/LimitedBeadingStrategy.hpp
    Arachne/BeadingStrategy/LimitedBeadingStrategy.cpp
    Arachne/BeadingStrategy/OuterWallInsetBeadingStrategy.hpp
    Arachne/BeadingStrategy/OuterWallInsetBeadingStrategy.cpp
    Arachne/BeadingStrategy/RedistributeBeadingStrategy.hpp
    Arachne/BeadingStrategy/RedistributeBeadingStrategy.cpp
    Arachne/BeadingStrategy/WideningBeadingStrategy.hpp
    Arachne/BeadingStrategy/WideningBeadingStrategy.cpp
    Arachne/utils/ExtrusionJunction.hpp
    Arachne/utils/ExtrusionLine.hpp
    Arachne/utils/ExtrusionLine.cpp
    Arachne/utils/HalfEdge.hpp
    Arachne/utils/HalfEdgeGraph.hpp
    Arachne/utils/HalfEdgeNode.hpp
    Arachne/utils/SparseGrid.hpp
    Arachne/utils/SparsePointGrid.hpp
    Arachne/utils/SparseLineGrid.hpp
    Arachne/utils/SquareGrid.hpp
    Arachne/utils/SquareGrid.cpp
    Arachne/utils/PolygonsPointIndex.hpp
    Arachne/utils/PolygonsSegmentIndex.hpp
    Arachne/utils/PolylineStitcher.hpp
    Arachne/utils/PolylineStitcher.cpp
    Geometry/Voronoi.cpp
    Geometry/VoronoiUtils.hpp
    Geometry/VoronoiUtils.cpp
    Arachne/PerimeterOrder.hpp
    Arachne/PerimeterOrder.cpp
    Arachne/SkeletalTrapezoidation.hpp
    Arachne/SkeletalTrapezoidation.cpp
    Arachne/SkeletalTrapezoidationEdge.hpp
    Arachne/SkeletalTrapezoidationGraph.hpp
    Arachne/SkeletalTrapezoidationGraph.cpp
    Arachne/SkeletalTrapezoidationJoint.hpp
    Arachne/WallToolPaths.hpp
    Arachne/WallToolPaths.cpp
    StaticMap.hpp
    Utils/DirectoriesUtils.hpp
    Utils/DirectoriesUtils.cpp
    Utils/JsonUtils.hpp
    Utils/JsonUtils.cpp
)

if (APPLE)
    list(APPEND SLIC3R_SOURCES
            MacUtils.mm
        )
endif ()

add_library(libslic3r STATIC ${SLIC3R_SOURCES})

if (WIN32)
    target_compile_definitions(libslic3r PUBLIC NOMINMAX)
endif()

foreach(_source IN ITEMS ${SLIC3R_SOURCES})
    get_filename_component(_source_path "${_source}" PATH)
    string(REPLACE "/" "\\" _group_path "${_source_path}")
    source_group("${_group_path}" FILES "${_source}")
endforeach()

# Create the source groups for source tree with root at CMAKE_CURRENT_SOURCE_DIR.
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCE_LIST_SLA})

if (SLIC3R_STATIC)
    set(CGAL_Boost_USE_STATIC_LIBS ON CACHE BOOL "" FORCE)
endif ()
set(CGAL_DO_NOT_WARN_ABOUT_CMAKE_BUILD_TYPE ON CACHE BOOL "" FORCE)

cmake_policy(PUSH)
cmake_policy(SET CMP0011 NEW)
find_package(CGAL REQUIRED)
cmake_policy(POP)

add_library(libslic3r_cgal STATIC
    CutSurface.hpp CutSurface.cpp
    Geometry/VoronoiUtilsCgal.hpp Geometry/VoronoiUtilsCgal.cpp
    IntersectionPoints.hpp IntersectionPoints.cpp
    MeshBoolean.hpp MeshBoolean.cpp
    TryCatchSignal.hpp TryCatchSignal.cpp
    Triangulation.hpp Triangulation.cpp
    SLA/SupportIslands/VoronoiDiagramCGAL.hpp SLA/SupportIslands/VoronoiDiagramCGAL.cpp
)
target_include_directories(libslic3r_cgal PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_include_directories(libslic3r_cgal PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

# Reset compile options of libslic3r_cgal. Despite it being linked privately, CGAL options
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
    target_compile_options(libslic3r_cgal PRIVATE "${_opts_bad}")
endif()

target_link_libraries(libslic3r_cgal PRIVATE ${_cgal_tgt} libigl)
target_link_libraries(libslic3r_cgal PUBLIC semver admesh localesutils)

if (MSVC AND "${CMAKE_SIZEOF_VOID_P}" STREQUAL "4") # 32 bit MSVC workaround
    target_compile_definitions(libslic3r_cgal PRIVATE CGAL_DO_NOT_USE_MPZF)
endif ()

encoding_check(libslic3r)

target_compile_definitions(libslic3r PUBLIC -DUSE_TBB -DTBB_USE_CAPTURED_EXCEPTION=0)
target_include_directories(libslic3r PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/.. ${CMAKE_CURRENT_BINARY_DIR})
target_include_directories(libslic3r PUBLIC ${EXPAT_INCLUDE_DIRS})

find_package(JPEG REQUIRED)

target_link_libraries(libslic3r PRIVATE
    libnest2d
    libcereal
    boost_libs
    clipper
    libexpat
    glu-libtess
    qhull
    TBB::tbb
    TBB::tbbmalloc
    libslic3r_cgal
    ${CMAKE_DL_LIBS}
    PNG::PNG
    ZLIB::ZLIB
	JPEG::JPEG
    qoi
    fastfloat
    int128
)
target_link_libraries(libslic3r PUBLIC
    Eigen3::Eigen
    semver
    admesh
    localesutils
    LibBGCode::bgcode_convert
    tcbspan
    miniz
    libigl
    agg
    ankerl
    boost_headeronly
    libseqarrange
)

if (APPLE)
    # TODO: we need to fix notarization with the separate shared library
    target_link_libraries(libslic3r PUBLIC OCCTWrapper)
endif ()

if (TARGET OpenVDB::openvdb)
    target_link_libraries(libslic3r PUBLIC OpenVDB::openvdb)
endif()

if(WIN32)
    target_link_libraries(libslic3r PUBLIC Psapi.lib)
endif()

if (APPLE)
    # This flag prevents the need for minimum SDK version 10.14
    # currently, PS targets v10.12
    target_compile_options(libslic3r PUBLIC "-fno-aligned-allocation")
    target_compile_options(libslic3r_cgal PUBLIC "-fno-aligned-allocation")
endif ()

if (SLIC3R_PCH AND NOT SLIC3R_SYNTAXONLY)
    add_precompiled_header(libslic3r pchheader.hpp FORCEINCLUDE)
endif ()
