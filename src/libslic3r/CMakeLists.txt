#/|/ Copyright (c) Prusa Research 2018 - 2023 Tomáš Mészáros @tamasmeszaros, Oleksandra Iushchenko @YuSanka, Enrico Turri @enricoturri1966, Vojtěch Bubník @bubnikv, Pavel Mikuš @Godrak, Lukáš Hejl @hejllukas, Lukáš Matěna @lukasmatena, Filip Sykala @Jony01, David Kocík @kocikdav, Vojtěch Král @vojtechkral
#/|/ Copyright (c) BambuStudio 2023 manch1n @manch1n
#/|/ Copyright (c) 2023 Mimoja @Mimoja
#/|/ Copyright (c) 2022 ole00 @ole00
#/|/
#/|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
#/|/
cmake_minimum_required(VERSION 3.13)
project(libslic3r)

if(SLIC3R_STATIC)
    set(OPENVDB_USE_STATIC_LIBS ON)
    set(USE_BLOSC TRUE)
endif ()

find_package(OpenVDB 5.0 COMPONENTS openvdb)
if(OpenVDB_FOUND)
    slic3r_remap_configs(IlmBase::Half RelWithDebInfo Release)
    slic3r_remap_configs(Blosc::blosc RelWithDebInfo Release)
else ()
    message(FATAL_ERROR "OpenVDB could not be found with the bundled find module. "
                   "You can try to specify the find module location of your "
                   "OpenVDB installation with the OPENVDB_FIND_MODULE_PATH cache variable.")
endif ()

set(OpenVDBUtils_SOURCES "")
if (TARGET OpenVDB::openvdb)
    set(OpenVDBUtils_SOURCES
        src/libslic3r/OpenVDBUtils.cpp
        src/libslic3r/OpenVDBUtils.hpp
        src/libslic3r/OpenVDBUtilsLegacy.hpp
    )
endif ()

find_package(LibBGCode REQUIRED COMPONENTS Convert)
slic3r_remap_configs(LibBGCode::bgcode_core RelWithDebInfo Release)
slic3r_remap_configs(LibBGCode::bgcode_binarize RelWithDebInfo Release)
slic3r_remap_configs(LibBGCode::bgcode_convert RelWithDebInfo Release)

set(SLIC3R_SOURCES
    src/libslic3r/AnyPtr.hpp
    src/libslic3r/Algorithm/LineSegmentation/LineSegmentation.cpp
    src/libslic3r/Algorithm/LineSegmentation/LineSegmentation.hpp
    src/libslic3r/Algorithm/PathSorting.hpp
    src/libslic3r/Algorithm/RegionExpansion.cpp
    src/libslic3r/Algorithm/RegionExpansion.hpp
    src/libslic3r/Arachne/BeadingStrategy/BeadingStrategy.cpp
    src/libslic3r/Arachne/BeadingStrategy/BeadingStrategy.hpp
    src/libslic3r/Arachne/BeadingStrategy/BeadingStrategyFactory.cpp
    src/libslic3r/Arachne/BeadingStrategy/BeadingStrategyFactory.hpp
    src/libslic3r/Arachne/BeadingStrategy/DistributedBeadingStrategy.cpp
    src/libslic3r/Arachne/BeadingStrategy/DistributedBeadingStrategy.hpp
    src/libslic3r/Arachne/BeadingStrategy/LimitedBeadingStrategy.cpp
    src/libslic3r/Arachne/BeadingStrategy/LimitedBeadingStrategy.hpp
    src/libslic3r/Arachne/BeadingStrategy/OuterWallInsetBeadingStrategy.cpp
    src/libslic3r/Arachne/BeadingStrategy/OuterWallInsetBeadingStrategy.hpp
    src/libslic3r/Arachne/BeadingStrategy/RedistributeBeadingStrategy.cpp
    src/libslic3r/Arachne/BeadingStrategy/RedistributeBeadingStrategy.hpp
    src/libslic3r/Arachne/BeadingStrategy/WideningBeadingStrategy.cpp
    src/libslic3r/Arachne/BeadingStrategy/WideningBeadingStrategy.hpp
    src/libslic3r/Arachne/PerimeterOrder.cpp
    src/libslic3r/Arachne/PerimeterOrder.hpp
    src/libslic3r/Arachne/SkeletalTrapezoidation.cpp
    src/libslic3r/Arachne/SkeletalTrapezoidation.hpp
    src/libslic3r/Arachne/SkeletalTrapezoidationEdge.hpp
    src/libslic3r/Arachne/SkeletalTrapezoidationGraph.cpp
    src/libslic3r/Arachne/SkeletalTrapezoidationGraph.hpp
    src/libslic3r/Arachne/SkeletalTrapezoidationJoint.hpp
    src/libslic3r/Arachne/utils/ExtrusionJunction.hpp
    src/libslic3r/Arachne/utils/ExtrusionLine.cpp
    src/libslic3r/Arachne/utils/ExtrusionLine.hpp
    src/libslic3r/Arachne/utils/HalfEdge.hpp
    src/libslic3r/Arachne/utils/HalfEdgeGraph.hpp
    src/libslic3r/Arachne/utils/HalfEdgeNode.hpp
    src/libslic3r/Arachne/utils/PolylineStitcher.cpp
    src/libslic3r/Arachne/utils/PolylineStitcher.hpp
    src/libslic3r/Arachne/utils/SparseGrid.hpp
    src/libslic3r/Arachne/utils/SparseLineGrid.hpp
    src/libslic3r/Arachne/utils/SparsePointGrid.hpp
    src/libslic3r/Arachne/utils/SquareGrid.cpp
    src/libslic3r/Arachne/utils/SquareGrid.hpp
    src/libslic3r/Arachne/WallToolPaths.cpp
    src/libslic3r/Arachne/WallToolPaths.hpp
#    src/libslic3r/ArrangeHelper.cpp
#    src/libslic3r/ArrangeHelper.hpp
    src/libslic3r/AStar.hpp
    src/libslic3r/BranchingTree/BranchingTree.cpp
    src/libslic3r/BranchingTree/BranchingTree.hpp
    src/libslic3r/BranchingTree/PointCloud.cpp
    src/libslic3r/BranchingTree/PointCloud.hpp
    src/libslic3r/BridgeDetector.cpp
    src/libslic3r/BridgeDetector.hpp
    src/libslic3r/Brim.cpp
    src/libslic3r/Brim.hpp
    src/libslic3r/BuildVolume.cpp
    src/libslic3r/BuildVolume.hpp
    src/libslic3r/ClipperUtils.cpp
    src/libslic3r/ClipperUtils.hpp
    src/libslic3r/ClipperZUtils.hpp
    src/libslic3r/ConfigUtils.hpp
    src/libslic3r/ConfigUtils.cpp
    src/libslic3r/ConfigViews.hpp
    src/libslic3r/ConfigViews.cpp
    src/libslic3r/CSGMesh/CSGMesh.hpp
    src/libslic3r/CSGMesh/VoxelizeCSGMesh.hpp
    src/libslic3r/CSGMesh/CSGMeshCopy.hpp
    src/libslic3r/CSGMesh/ModelToCSGMesh.hpp
    src/libslic3r/CSGMesh/PerformCSGMeshBooleans.hpp
    src/libslic3r/CSGMesh/SliceCSGMesh.hpp
    src/libslic3r/CSGMesh/TriangleMeshAdapter.hpp
    src/libslic3r/CustomGCode.cpp
    src/libslic3r/CustomParametersHandling.hpp
    src/libslic3r/CustomParametersHandling.cpp
    src/libslic3r/EdgeGrid.cpp
    src/libslic3r/EdgeGrid.hpp
    src/libslic3r/ElephantFootCompensation.cpp
    src/libslic3r/ElephantFootCompensation.hpp
    src/libslic3r/ExPolygon.cpp
    src/libslic3r/ExPolygon.hpp
    src/libslic3r/Extruder.cpp
    src/libslic3r/Extruder.hpp
    src/libslic3r/ExtruderCandidates.cpp
    src/libslic3r/ExtrusionEntity.hpp
    src/libslic3r/ExtrusionEntity.cpp
    src/libslic3r/ExtrusionEntityCollection.hpp
    src/libslic3r/ExtrusionEntityCollection.cpp
    src/libslic3r/ExtrusionRole.hpp
    src/libslic3r/ExtrusionRole.cpp
    src/libslic3r/ExtrusionSimulator.cpp
    src/libslic3r/ExtrusionSimulator.hpp
    src/libslic3r/ExtrudeConfig.hpp
    src/libslic3r/ExtrudeConfig.cpp
    src/libslic3r/Feature/FuzzySkin/FuzzySkin.cpp
    src/libslic3r/Feature/FuzzySkin/FuzzySkin.hpp
    src/libslic3r/Feature/VirtualExtruder/VirtualExtruder.cpp
    src/libslic3r/Feature/VirtualExtruder/VirtualExtruder.hpp
    src/libslic3r/Feature/Interlocking/InterlockingGenerator.cpp
    src/libslic3r/Feature/Interlocking/InterlockingGenerator.hpp
    src/libslic3r/Feature/Interlocking/VoxelUtils.cpp
    src/libslic3r/Feature/Interlocking/VoxelUtils.hpp
    src/libslic3r/FileParserError.hpp
    src/libslic3r/Fill/FillAdaptive.hpp
    src/libslic3r/Fill/FillBase.hpp
    src/libslic3r/Fill/FillLightning.hpp
    src/libslic3r/Fill/Fill.cpp
    src/libslic3r/Fill/Fill3DHoneycomb.cpp
    src/libslic3r/Fill/Fill3DHoneycomb.hpp
    src/libslic3r/Fill/FillAdaptive.cpp
    src/libslic3r/Fill/FillBase.cpp
    src/libslic3r/Fill/FillConcentric.cpp
    src/libslic3r/Fill/FillConcentric.hpp
    src/libslic3r/Fill/FillEnsuring.cpp
    src/libslic3r/Fill/FillEnsuring.hpp
    src/libslic3r/Fill/FillGyroid.cpp
    src/libslic3r/Fill/FillGyroid.hpp
    src/libslic3r/Fill/FillHoneycomb.cpp
    src/libslic3r/Fill/FillHoneycomb.hpp
    src/libslic3r/Fill/FillLightning.cpp
    src/libslic3r/Fill/FillLine.cpp
    src/libslic3r/Fill/FillLine.hpp
    src/libslic3r/Fill/FillPlanePath.cpp
    src/libslic3r/Fill/FillPlanePath.hpp
    src/libslic3r/Fill/FillRectilinear.cpp
    src/libslic3r/Fill/FillRectilinear.hpp
    src/libslic3r/Fill/Lightning/DistanceField.cpp
    src/libslic3r/Fill/Lightning/DistanceField.hpp
    src/libslic3r/Fill/Lightning/Generator.cpp
    src/libslic3r/Fill/Lightning/Generator.hpp
    src/libslic3r/Fill/Lightning/Layer.cpp
    src/libslic3r/Fill/Lightning/Layer.hpp
    src/libslic3r/Fill/Lightning/TreeNode.cpp
    src/libslic3r/Fill/Lightning/TreeNode.hpp
    src/libslic3r/Flow.hpp
    src/libslic3r/Flow.cpp
    #src/libslic3r/Format/PrintRequest.cpp
    #src/libslic3r/Format/PrintRequest.cpp
    src/libslic3r/Format/SL1.cpp
    src/libslic3r/Format/SL1.hpp
    src/libslic3r/Format/SL1_SVG.cpp
    src/libslic3r/Format/SL1_SVG.hpp
    src/libslic3r/GCode/Thumbnails.hpp
    src/libslic3r/GCode/ToolOrdering.hpp
    src/libslic3r/GCode/WipeTower.hpp
    src/libslic3r/GCode.cpp
    src/libslic3r/GCode.hpp
    src/libslic3r/GCode/AvoidCrossingPerimeters.cpp
    src/libslic3r/GCode/AvoidCrossingPerimeters.hpp
    src/libslic3r/GCode/ConflictChecker.cpp
    src/libslic3r/GCode/ConflictChecker.hpp
    src/libslic3r/GCode/CoolingBuffer.cpp
    src/libslic3r/GCode/CoolingBuffer.hpp
    src/libslic3r/GCode/CoolingBufferConfig.cpp
    src/libslic3r/GCode/CoolingBufferConfig.hpp
    src/libslic3r/GCode/ExtrusionOrder.cpp
    src/libslic3r/GCode/ExtrusionOrder.hpp
    src/libslic3r/GCode/ExtrusionProcessor.cpp
    src/libslic3r/GCode/ExtrusionProcessor.hpp
    src/libslic3r/GCode/FindReplace.cpp
    src/libslic3r/GCode/FindReplace.hpp
    src/libslic3r/GCode/GCodeWriter.cpp
    src/libslic3r/GCode/GCodeWriter.hpp
    src/libslic3r/GCode/GCodeWriterConfig.cpp
    src/libslic3r/GCode/GCodeWriterConfig.hpp
    src/libslic3r/GCode/LabelObjects.cpp
    src/libslic3r/GCode/LabelObjects.hpp
    src/libslic3r/GCode/ModelVisibility.cpp
    src/libslic3r/GCode/ModelVisibility.hpp
    src/libslic3r/GCode/PostProcessor.cpp
    src/libslic3r/GCode/PostProcessor.hpp
    src/libslic3r/GCode/PressureEqualizer.cpp
    src/libslic3r/GCode/PressureEqualizer.hpp
    src/libslic3r/GCode/PrintExtents.cpp
    src/libslic3r/GCode/PrintExtents.hpp
    src/libslic3r/GCode/RetractWhenCrossingPerimeters.cpp
    src/libslic3r/GCode/RetractWhenCrossingPerimeters.hpp
    src/libslic3r/GCode/SeamAligned.cpp
    src/libslic3r/GCode/SeamAligned.hpp
    src/libslic3r/GCode/SeamChoice.cpp
    src/libslic3r/GCode/SeamChoice.hpp
    src/libslic3r/GCode/SeamGeometry.cpp
    src/libslic3r/GCode/SeamGeometry.hpp
    src/libslic3r/GCode/SeamPainting.cpp
    src/libslic3r/GCode/SeamPainting.hpp
    src/libslic3r/GCode/SeamPerimeters.cpp
    src/libslic3r/GCode/SeamPerimeters.hpp
    src/libslic3r/GCode/SeamPlacer.cpp
    src/libslic3r/GCode/SeamPlacer.hpp
    src/libslic3r/GCode/SeamRandom.cpp
    src/libslic3r/GCode/SeamRandom.hpp
    src/libslic3r/GCode/SeamRear.cpp
    src/libslic3r/GCode/SeamRear.hpp
    src/libslic3r/GCode/SeamScarf.cpp
    src/libslic3r/GCode/SeamScarf.hpp
    src/libslic3r/GCode/SeamShells.cpp
    src/libslic3r/GCode/SeamShells.hpp
    src/libslic3r/GCode/SmoothPath.cpp
    src/libslic3r/GCode/SmoothPath.hpp
    src/libslic3r/GCode/SpiralVase.cpp
    src/libslic3r/GCode/SpiralVase.hpp
    src/libslic3r/GCode/Thumbnails.cpp
    src/libslic3r/GCode/ToolOrdering.cpp
    src/libslic3r/GCode/Travels.cpp
    src/libslic3r/GCode/Travels.hpp
    src/libslic3r/GCode/Wipe.cpp
    src/libslic3r/GCode/Wipe.hpp
    src/libslic3r/GCode/WipeTower.cpp
    src/libslic3r/GCode/WipeTowerIntegration.cpp
    src/libslic3r/GCode/WipeTowerIntegration.hpp
    src/libslic3r/Geometry.hpp
    src/libslic3r/Geometry/ArcWelder.cpp
    src/libslic3r/Geometry/ArcWelder.hpp
    src/libslic3r/Geometry/Bicubic.hpp
    src/libslic3r/Geometry/ConvexHull.hpp
    src/libslic3r/Geometry/Curves.hpp
    src/libslic3r/Geometry/MedialAxis.cpp
    src/libslic3r/Geometry/MedialAxis.hpp
    src/libslic3r/HwConfigUtils.hpp
    src/libslic3r/HwConfigUtils.cpp
    src/libslic3r/InfillAboveBridges.cpp
    src/libslic3r/InfillAboveBridges.hpp
    src/libslic3r/InitPrint.cpp
    src/libslic3r/InstanceTransformations.cpp
    src/libslic3r/InstanceTransformations.hpp
    src/libslic3r/JumpPointSearch.cpp
    src/libslic3r/JumpPointSearch.hpp
    src/libslic3r/KDTreeIndirect.hpp
    src/libslic3r/Layer.cpp
    src/libslic3r/Layer.hpp
    src/libslic3r/LayerRegion.cpp
    src/libslic3r/LayerRegion.hpp
    src/libslic3r/Line.hpp
    src/libslic3r/MarchingSquares.hpp
    src/libslic3r/MeshNormals.cpp
    src/libslic3r/MeshNormals.hpp
    src/libslic3r/Model.cpp
    src/libslic3r/Model.hpp
    src/libslic3r/ModelProcessing.cpp
    src/libslic3r/ModelProcessing.hpp
    src/libslic3r/ModelUtils.hpp
    src/libslic3r/ModelUtils.cpp
    src/libslic3r/MTUtils.hpp
    src/libslic3r/MultiMaterialSegmentation.cpp
    src/libslic3r/MultiMaterialSegmentation.hpp
    src/libslic3r/MultiPoint.cpp
    src/libslic3r/MultiPoint.hpp
    src/libslic3r/MutablePolygon.cpp
    src/libslic3r/MutablePolygon.hpp
    src/libslic3r/MutablePriorityQueue.hpp
    src/libslic3r/NormalUtils.cpp
    src/libslic3r/NormalUtils.hpp
    src/libslic3r/PerimeterGenerator.cpp
    src/libslic3r/PerimeterGenerator.hpp
    src/libslic3r/Point.cpp
    src/libslic3r/Point.hpp
    src/libslic3r/PointGrid.hpp
    src/libslic3r/Polygon.cpp
    src/libslic3r/Polygon.hpp
    src/libslic3r/Polyline.cpp
    src/libslic3r/Polyline.hpp
    src/libslic3r/PrePreview.cpp
    src/libslic3r/PrePreview.hpp
    src/libslic3r/PrincipalComponents2D.cpp
    src/libslic3r/PrincipalComponents2D.hpp
    src/libslic3r/Print.hpp
    src/libslic3r/Print.cpp
    src/libslic3r/PrintApply.cpp
    src/libslic3r/PrintBase.hpp
    src/libslic3r/PrintBase.cpp
    src/libslic3r/PrintState.hpp
    src/libslic3r/PrintObject.cpp
    src/libslic3r/PrintObjectSlice.cpp
    src/libslic3r/PrintRegion.cpp
    src/libslic3r/ShortEdgeCollapse.cpp
    src/libslic3r/ShortEdgeCollapse.hpp
    src/libslic3r/ShortestPath.cpp
    src/libslic3r/ShortestPath.hpp
    src/libslic3r/ShrinkageCompensation.hpp
    src/libslic3r/ShrinkageCompensation.cpp
    src/libslic3r/SLA/Hollowing.hpp
    src/libslic3r/SLA/JobController.hpp
    src/libslic3r/SLA/Pad.hpp
    src/libslic3r/SLA/SupportIslands/SampleConfig.hpp
    src/libslic3r/SLA/SupportPointGenerator.hpp
    src/libslic3r/SLA/SupportTree.hpp
    src/libslic3r/SLA/SupportTreeStrategies.hpp
    src/libslic3r/SLA/AGGRaster.hpp
    src/libslic3r/SLA/BranchingTreeSLA.cpp
    src/libslic3r/SLA/BranchingTreeSLA.hpp
    src/libslic3r/SLA/Clustering.cpp
    src/libslic3r/SLA/Clustering.hpp
    src/libslic3r/SLA/ConcaveHull.cpp
    src/libslic3r/SLA/ConcaveHull.hpp
    src/libslic3r/SLA/DefaultSupportTree.cpp
    src/libslic3r/SLA/DefaultSupportTree.hpp
    src/libslic3r/SLA/Hollowing.cpp
    src/libslic3r/SLA/Pad.cpp
    src/libslic3r/SLA/RasterBase.cpp
    src/libslic3r/SLA/RasterBase.hpp
    src/libslic3r/SLA/RasterToPolygons.cpp
    src/libslic3r/SLA/RasterToPolygons.hpp
    src/libslic3r/SLA/ReprojectPointsOnMesh.hpp
    src/libslic3r/SLA/SpatIndex.cpp
    src/libslic3r/SLA/SpatIndex.hpp
    src/libslic3r/SLA/SupportIslands/SampleConfigFactory.cpp
    src/libslic3r/SLA/SupportIslands/SampleConfigFactory.hpp
    src/libslic3r/SLA/SupportIslands/SupportIslandPoint.cpp
    src/libslic3r/SLA/SupportIslands/SupportIslandPoint.hpp
    src/libslic3r/SLA/SupportIslands/UniformSupportIsland.cpp
    src/libslic3r/SLA/SupportIslands/UniformSupportIsland.hpp
    src/libslic3r/SLA/SupportPointGenerator.cpp
    src/libslic3r/SLA/SupportTree.cpp
    src/libslic3r/SLA/SupportTreeBuilder.cpp
    src/libslic3r/SLA/SupportTreeBuilder.hpp
    src/libslic3r/SLA/SupportTreeMesher.cpp
    src/libslic3r/SLA/SupportTreeMesher.hpp
    src/libslic3r/SLA/SupportTreeUtils.hpp
    src/libslic3r/SLA/SupportTreeUtilsLegacy.hpp
    src/libslic3r/SLA/ZCorrection.cpp
    src/libslic3r/SLA/ZCorrection.hpp
    src/libslic3r/SLAPrint.hpp
    src/libslic3r/SLAPrint.cpp
    src/libslic3r/SLAPrintSteps.cpp
    src/libslic3r/SLAPrintSteps.hpp
    src/libslic3r/SlicesToTriangleMesh.cpp
    src/libslic3r/SlicesToTriangleMesh.hpp
    src/libslic3r/Slicing.cpp
    src/libslic3r/SlicingInput.cpp
    src/libslic3r/SlicingInput.hpp
    src/libslic3r/StepsInvalidation.cpp
    src/libslic3r/StepsInvalidation.hpp
    src/libslic3r/Subdivide.cpp
    src/libslic3r/Subdivide.hpp
    src/libslic3r/Support/OrganicSupport.cpp
    src/libslic3r/Support/OrganicSupport.hpp
    src/libslic3r/Support/SupportCommon.cpp
    src/libslic3r/Support/SupportCommon.hpp
    src/libslic3r/Support/SupportDebug.cpp
    src/libslic3r/Support/SupportDebug.hpp
    src/libslic3r/Support/SupportLayer.hpp
    src/libslic3r/Support/SupportMaterial.cpp
    src/libslic3r/Support/SupportMaterial.hpp
    src/libslic3r/Support/SupportParameters.cpp
    src/libslic3r/Support/SupportParameters.hpp
    src/libslic3r/Support/TreeModelVolumes.cpp
    src/libslic3r/Support/TreeModelVolumes.hpp
    src/libslic3r/Support/TreeSupport.cpp
    src/libslic3r/Support/TreeSupport.hpp
    src/libslic3r/Support/TreeSupportCommon.cpp
    src/libslic3r/Support/TreeSupportCommon.hpp
    src/libslic3r/SupportSpotsGenerator.hpp
    src/libslic3r/SupportSpotsGenerator.cpp
    src/libslic3r/Surface.cpp
    src/libslic3r/Surface.hpp
    src/libslic3r/SurfaceCollection.cpp
    src/libslic3r/SurfaceCollection.hpp
    src/libslic3r/Technologies.hpp
    src/libslic3r/Thread.cpp
    src/libslic3r/Thread.hpp
    src/libslic3r/TriangleMeshSlicer.cpp
    src/libslic3r/TriangleSetSampling.cpp
    src/libslic3r/TriangleSetSampling.hpp
    src/libslic3r/TriangulateWall.hpp
    src/libslic3r/Utils.hpp
    src/libslic3r/utils.cpp
    src/libslic3r/Utils/JsonUtils.cpp
    src/libslic3r/Utils/JsonUtils.hpp
    src/libslic3r/SlicingStatus.cpp
    src/libslic3r/I18N_private.cpp

    src/libslic3r/libslic3r.h

    ${OpenVDBUtils_SOURCES}
)

set(SLIC3R_PUBLIC_HEADERS
    include/libslic3r/CustomGCode.hpp
    include/libslic3r/ExtruderCandidates.hpp
    include/libslic3r/GeneratedSupportPoints.hpp
    include/libslic3r/IPrint.hpp
    include/libslic3r/InitPrint.hpp
    include/libslic3r/PrintSteps.hpp
    include/libslic3r/SerializedConfig.hpp
    include/libslic3r/SLAResult.hpp
    include/libslic3r/Slicing.hpp
    include/libslic3r/ThumbnailImageRequest.hpp
    include/libslic3r/TriangleMeshSlicer.hpp
    include/libslic3r/SlicingStatus.hpp
)

add_library(libslic3r STATIC ${SLIC3R_SOURCES} ${SLIC3R_PUBLIC_HEADERS})

set_target_properties(libslic3r PROPERTIES PREFIX "")

if (WIN32)
    target_compile_definitions(libslic3r PUBLIC NOMINMAX)
endif ()

foreach(_source IN ITEMS ${SLIC3R_SOURCES})
    get_filename_component(_source_path "${_source}" PATH)
    string(REPLACE "/" "\\" _group_path "${_source_path}")
    source_group("${_group_path}" FILES "${_source}")
endforeach()

# Create the source groups for source tree with root at CMAKE_CURRENT_SOURCE_DIR.
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCE_LIST_SLA})

target_compile_definitions(libslic3r PUBLIC -DUSE_TBB -DTBB_USE_CAPTURED_EXCEPTION=0)

target_include_directories(libslic3r PUBLIC include)
target_include_directories(libslic3r PRIVATE src)

find_package(JPEG REQUIRED)

target_link_libraries(libslic3r PRIVATE
    libexpat
    qhull
    TBB::tbb
    TBB::tbbmalloc
    ${CMAKE_DL_LIBS}
    JPEG::JPEG
    qoi
    fastfloat
)

if (NOT EMSCRIPTEN)
    target_link_libraries(libslic3r PRIVATE
            PNG::PNG
            ZLIB::ZLIB
            JPEG::JPEG
    )
else ()
    set(CMAKE_C_FLAGS "--use-port=libpng --use-port=zlib ${CMAKE_CXX_FLAGS}")
    set(CMAKE_CXX_FLAGS "--use-port=libpng --use-port=zlib ${CMAKE_CXX_FLAGS}")
endif ()

target_link_libraries(libslic3r PUBLIC
    admesh
    localesutils
    LibBGCode::bgcode_convert
    agg
    ankerl
    #libseqarrange
    slic3r-jthread
    slic3r-domain
    slic3r-gcode-reader
    slic3r-biz-cgal-algorithms
    libpgcode
    clipper
    clipper_z
    slic3r-biz-parser
)

if (APPLE)
    # TODO: we need to fix notarization with the separate shared library
    target_link_libraries(libslic3r PUBLIC OCCTWrapper)
endif ()

if (TARGET OpenVDB::openvdb)
    target_link_libraries(libslic3r PUBLIC OpenVDB::openvdb Blosc::blosc)
endif()

if(WIN32)
    target_link_libraries(libslic3r PUBLIC Psapi.lib)
endif()

if (SLIC3R_PCH)
    target_precompile_headers(libslic3r REUSE_FROM slic3r-domain)
endif()

#add_target_to_doxygen(libslic3r INPUT .)
