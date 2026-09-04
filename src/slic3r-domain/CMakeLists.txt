project(slic3r-domain)

find_package(NanoSVG REQUIRED)

add_library(slic3r-domain STATIC
    src/Slic3r/Domain/Project.cpp
    include/Slic3r/Domain/Project.hpp
    include/Slic3r/Domain/BedContainer.hpp
    src/Slic3r/Domain/BedContainer.cpp
    src/Slic3r/Domain/Workbench.cpp
    include/Slic3r/Domain/Workbench.hpp
    src/Slic3r/Domain/ConfigContainer.cpp
    include/Slic3r/Domain/ConfigContainer.hpp
    include/Slic3r/Domain/ProjectDirectoryStorage.hpp

    include/Slic3r/Domain/CustomGCode.hpp
    include/Slic3r/Domain/Line.hpp
    include/Slic3r/Domain/MultiPoint.hpp
    include/Slic3r/Domain/Polygon.hpp
    include/Slic3r/Domain/Polyline.hpp
    src/Slic3r/Domain/Line.cpp
    src/Slic3r/Domain/MultiPoint.cpp
    src/Slic3r/Domain/Polygon.cpp
    src/Slic3r/Domain/Polyline.cpp
    include/Slic3r/Domain/BoundingBox.hpp
    src/Slic3r/Domain/BoundingBox.cpp
    include/Slic3r/Domain/Point.hpp
    src/Slic3r/Domain/Point.cpp

    include/Slic3r/Domain/ExPolygon.hpp
    src/Slic3r/Domain/ExPolygon.cpp
    include/Slic3r/Domain/PrinterTechnology.hpp
    include/Slic3r/Domain/PrintStatistics.hpp
    include/Slic3r/Domain/SLA/PrintStatistics.hpp

    include/Slic3r/Domain/Expr/ExprAst.hpp
    src/Slic3r/Domain/Expr/ExprAst.cpp
    include/Slic3r/Domain/Expr/RegEx.hpp

    include/Slic3r/Domain/JsonValue.hpp

    include/Slic3r/Domain/Preset/PresetTree.hpp
    include/Slic3r/Domain/Preset/SourceLocatedExpr.hpp
    include/Slic3r/Domain/Preset/Types.hpp
    include/Slic3r/Domain/Preset/HwConfig.hpp
    include/Slic3r/Domain/Preset/EvaluatedPreset.hpp
    include/Slic3r/Domain/Preset/SelectedPreset.hpp
    include/Slic3r/Domain/Preset/Bundle.hpp
    src/Slic3r/Domain/Preset/HwConfig.cpp
    src/Slic3r/Domain/Preset/PresetTree.cpp
    src/Slic3r/Domain/Preset/EvaluatedPreset.cpp
    src/Slic3r/Domain/Preset/SelectedPreset.cpp
    src/Slic3r/Domain/Preset/Bundle.cpp

    include/Slic3r/Domain/FacetsAnnotation.hpp
    src/Slic3r/Domain/FacetsAnnotation.cpp

    include/Slic3r/Domain/ObjectID.hpp
    src/Slic3r/Domain/ObjectID.cpp

    include/Slic3r/Domain/TriangleMesh.hpp
    src/Slic3r/Domain/TriangleMesh.cpp

    include/Slic3r/Domain/TriangleSelector.hpp
    src/Slic3r/Domain/TriangleSelector.cpp

    include/Slic3r/Domain/SLA/DrainHole.hpp
    src/Slic3r/Domain/SLA/DrainHole.cpp

    include/Slic3r/Domain/SLA/SupportPoint.hpp
    src/Slic3r/Domain/SLA/SupportPoint.cpp

    include/Slic3r/Domain/TextConfiguration.hpp
    src/Slic3r/Domain/TextConfiguration.cpp
    include/Slic3r/Domain/FontFile.hpp

    include/Slic3r/Domain/ExPolygonsIndex.hpp
    src/Slic3r/Domain/ExPolygonsIndex.cpp

    include/Slic3r/Domain/EmbossShape.hpp

    include/Slic3r/Domain/SelectionId.hpp

    include/Slic3r/Domain/ElementRef.hpp
    src/Slic3r/Domain/ElementId.cpp

    include/Slic3r/Domain/Bed.hpp
    src/Slic3r/Domain/Bed.cpp

    include/Slic3r/Domain/BedRef.hpp

    include/Slic3r/Domain/BedInstance.hpp
    src/Slic3r/Domain/BedInstance.cpp

    include/Slic3r/Domain/Transformation.hpp
    src/Slic3r/Domain/Transformation.cpp

    include/Slic3r/Domain/ITransformable.hpp

    include/Slic3r/Domain/ConfigValue.hpp
    src/Slic3r/Domain/ConfigValue.cpp

    include/Slic3r/Domain/ConfigDef.hpp
    src/Slic3r/Domain/ConfigDef.cpp

    include/Slic3r/Domain/Config.hpp
    src/Slic3r/Domain/Config.cpp

    include/Slic3r/Domain/ConfigDefsFDM.hpp
    src/Slic3r/Domain/ConfigDefsFDM.cpp

    include/Slic3r/Domain/ConfigBoxesFDM.hpp
    src/Slic3r/Domain/ConfigBoxesFDM.cpp

    include/Slic3r/Domain/FullConfigFDM.hpp
    src/Slic3r/Domain/FullConfigFDM.cpp

    include/Slic3r/Domain/ConfigDefsSLA.hpp
    src/Slic3r/Domain/ConfigDefsSLA.cpp

    include/Slic3r/Domain/ConfigBoxesSLA.hpp
    src/Slic3r/Domain/ConfigBoxesSLA.cpp

    include/Slic3r/Domain/FullConfigSLA.hpp
    src/Slic3r/Domain/FullConfigSLA.cpp

    include/Slic3r/Domain/ConfigPhysical.hpp
    src/Slic3r/Domain/ConfigPhysical.cpp

    include/Slic3r/Domain/ConfigCommon.hpp
    src/Slic3r/Domain/ConfigCommon.cpp

    include/Slic3r/Domain/CutConnector.hpp
    src/Slic3r/Domain/CutConnector.cpp

    include/Slic3r/Domain/LayerHeightProfile.hpp
    src/Slic3r/Domain/LayerHeightProfile.cpp

    include/Slic3r/Domain/Model.hpp
    src/Slic3r/Domain/Model.cpp

    include/Slic3r/Domain/ModelObject.hpp
    src/Slic3r/Domain/ModelObject.cpp

    include/Slic3r/Domain/ModelVolume.hpp
    src/Slic3r/Domain/ModelVolume.cpp

    include/Slic3r/Domain/ModelInstance.hpp
    src/Slic3r/Domain/ModelInstance.cpp

    include/Slic3r/Domain/Forward.hpp

    include/Slic3r/Domain/Color.hpp
    src/Slic3r/Domain/Color.cpp

    include/Slic3r/Domain/enum_bitmask.hpp

    include/Slic3r/Domain/ConfigPack.hpp
    src/Slic3r/Domain/ConfigPack.cpp

    include/Slic3r/Domain/ProjectMetadata.hpp
    src/Slic3r/Domain/ProjectMetadata.cpp

    include/Slic3r/Domain/GCodeMetadata.hpp
    src/Slic3r/Domain/GCodeMetadata.cpp

    include/Slic3r/Domain/Image.hpp

    include/Slic3r/Domain/PixelFormat.hpp
    src/Slic3r/Domain/PixelFormat.cpp

    include/Slic3r/Domain/Size.hpp
    src/Slic3r/Domain/Size.cpp

    include/Slic3r/Domain/JobStatus.hpp

    include/Slic3r/Domain/ConfigDefUtils.hpp
    src/Slic3r/Domain/ConfigDefUtils.cpp
    include/Slic3r/Domain/Preset/ParsedExpr.hpp
    include/Slic3r/Domain/IConfigPackFDMViewer.hpp
    include/Slic3r/Domain/Preset/SelectedPresetConfigPack.hpp
    src/Slic3r/Domain/Preset/SelectedPresetConfigPack.cpp

    include/Slic3r/Domain/VirtualExtruder.hpp
    src/Slic3r/Domain/VirtualExtruder.cpp
)

target_include_directories(slic3r-domain PUBLIC include)
target_include_directories(slic3r-domain PRIVATE src)


target_link_libraries(slic3r-domain PUBLIC
    admesh # TODO, remove dependency on admesh by refactoring triangle mesh
    NanoSVG::nanosvg # TODO remove dependency on nanosvg by refactoring EmbossShape
    NanoSVG::nanosvgrast
    boost_headeronly # TODO remove boost voronoi traits
    Boost::filesystem # Bed.cpp file checks
)

target_link_libraries(slic3r-domain PUBLIC slic3r-base TBB::tbbmalloc)

if (APPLE AND CMAKE_OSX_DEPLOYMENT_TARGET VERSION_LESS "10.14")
    # This flag prevents the need for minimum SDK version 10.14. Only the
    # Intel build targets an OS older than that (currently 10.13); the ARM
    # build targets 11.0 and doesn't need it.
    # Set here (PUBLIC) instead of in libslic3r so it also applies to the
    # precompiled header below, which every target reusing it must match exactly.
    target_compile_options(slic3r-domain PUBLIC "-fno-aligned-allocation")
endif ()

if (SLIC3R_PCH)
    target_precompile_headers(slic3r-domain PRIVATE pch.hpp)
endif()

if(SLIC3R_BUILD_TESTS)
    add_executable(slic3r-domain-tests
        test/Slic3r/Domain/TestUtils.hpp
        test/Slic3r/Domain/TestUtils.cpp
        test/Slic3r/Domain/ConfigValueTests.cpp
        test/Slic3r/Domain/ConfigTests.cpp
        test/Slic3r/Domain/FDMConfigTests.cpp
        test/Slic3r/Domain/Preset/PresetTreeTests.cpp
        test/Slic3r/Domain/Preset/HwConfigTests.cpp
        test/Slic3r/Domain/SizeTests.cpp
    )
    target_include_directories(slic3r-domain-tests PRIVATE test)

    target_link_libraries(slic3r-domain-tests PRIVATE slic3r-domain Catch2::Catch2WithMain)

    add_test(NAME slic3r-domain-tests
        COMMAND slic3r-domain-tests ${CATCH_ALL_ARGS}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
endif()
