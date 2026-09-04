cmake_minimum_required(VERSION 3.13)
project(libvgcode)

add_library(libvgcode
    include/Slic3r/App/libvgcode/ColorRange.hpp
    include/Slic3r/App/libvgcode/GCodeNodeTag.hpp
    include/Slic3r/App/libvgcode/SlaObjectNodeTag.hpp
    include/Slic3r/App/libvgcode/Types.hpp
    include/Slic3r/App/libvgcode/AbstractViewer.hpp
    include/Slic3r/App/libvgcode/FdmViewer.hpp
    include/Slic3r/App/libvgcode/FdmViewerInputData.hpp
    include/Slic3r/App/libvgcode/SlaViewer.hpp
    include/Slic3r/App/libvgcode/Bitset.hpp
    include/Slic3r/App/libvgcode/CogMarker.hpp
    include/Slic3r/App/libvgcode/Extruders.hpp
    include/Slic3r/App/libvgcode/ExtrusionRoles.hpp
    include/Slic3r/App/libvgcode/Layers.hpp
    include/Slic3r/App/libvgcode/ObjExport.hpp
    include/Slic3r/App/libvgcode/OptionTemplate.hpp
    include/Slic3r/App/libvgcode/Range.hpp
    include/Slic3r/App/libvgcode/SegmentTemplate.hpp
    include/Slic3r/App/libvgcode/Settings.hpp
    include/Slic3r/App/libvgcode/ToolMarker.hpp
    include/Slic3r/App/libvgcode/Utils.hpp
    include/Slic3r/App/libvgcode/ViewRange.hpp

    src/Slic3r/App/libvgcode/ColorRange.cpp
    src/Slic3r/App/libvgcode/CogMarker.cpp
    src/Slic3r/App/libvgcode/Extruders.cpp
    src/Slic3r/App/libvgcode/ExtrusionRoles.cpp
    src/Slic3r/App/libvgcode/Layers.cpp
    src/Slic3r/App/libvgcode/ObjExport.cpp
    src/Slic3r/App/libvgcode/OptionTemplate.cpp
    src/Slic3r/App/libvgcode/Range.cpp
    src/Slic3r/App/libvgcode/SegmentTemplate.cpp
    src/Slic3r/App/libvgcode/ToolMarker.cpp
    src/Slic3r/App/libvgcode/Utils.cpp
    src/Slic3r/App/libvgcode/AbstractViewer.cpp
    src/Slic3r/App/libvgcode/FdmViewer.cpp
    src/Slic3r/App/libvgcode/SlaViewer.cpp
    src/Slic3r/App/libvgcode/ViewRange.cpp
)

set_target_properties(libvgcode PROPERTIES PREFIX "")

target_include_directories(libvgcode PUBLIC include)
target_include_directories(libvgcode PRIVATE src)

target_link_libraries(libvgcode PUBLIC libslic3r libpgcode slic3r-render slic3r-shared)
