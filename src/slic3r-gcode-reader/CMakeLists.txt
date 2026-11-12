project(slic3r-gcode-reader)

add_library(slic3r-gcode-reader
    include/Slic3r/Biz/GCodeReader/GCodeReader.hpp
    src/Slic3r/Biz/GCodeReader/GCodeReader.cpp
    include/Slic3r/Biz/GCodeReader/Utils.hpp
    src/Slic3r/Biz/GCodeReader/Utils.cpp
)

target_include_directories(slic3r-gcode-reader PUBLIC include)
target_include_directories(slic3r-gcode-reader PRIVATE src)

target_link_libraries(slic3r-gcode-reader PUBLIC slic3r-domain)
target_link_libraries(slic3r-gcode-reader PRIVATE boost_headeronly fastfloat localesutils)
