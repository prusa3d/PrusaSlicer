cmake_minimum_required(VERSION 3.13)
project(libpgcode)

add_library(libpgcode
    include/Slic3r/Biz/libpgcode/LineView.hpp
    include/Slic3r/Biz/libpgcode/PostProcessorConfig.hpp
    include/Slic3r/Biz/libpgcode/Processor.hpp
    include/Slic3r/Biz/libpgcode/ProcessorConfig.hpp
    include/Slic3r/Biz/libpgcode/ProcessorResult.hpp
    include/Slic3r/Biz/libpgcode/Types.hpp
    include/Slic3r/Biz/libpgcode/Utils.hpp

    src/Slic3r/Biz/libpgcode/LineView.cpp
    src/Slic3r/Biz/libpgcode/OptionsZCorrector.hpp
    src/Slic3r/Biz/libpgcode/OptionsZCorrector.cpp
    src/Slic3r/Biz/libpgcode/PostProcessorConfig.cpp
    src/Slic3r/Biz/libpgcode/Processor.cpp
    src/Slic3r/Biz/libpgcode/ProcessorConfig.cpp
    src/Slic3r/Biz/libpgcode/ProcessorImpl.hpp
    src/Slic3r/Biz/libpgcode/ProcessorImpl.cpp
    src/Slic3r/Biz/libpgcode/ProcessorResult.cpp
    src/Slic3r/Biz/libpgcode/TimeBlock.hpp
    src/Slic3r/Biz/libpgcode/TimeBlock.cpp
    src/Slic3r/Biz/libpgcode/TimeMachine.hpp
    src/Slic3r/Biz/libpgcode/TimeMachine.cpp
    src/Slic3r/Biz/libpgcode/TimeProcessor.hpp
    src/Slic3r/Biz/libpgcode/TimeProcessor.cpp
    src/Slic3r/Biz/libpgcode/Types.cpp
    src/Slic3r/Biz/libpgcode/UsedFilaments.hpp
    src/Slic3r/Biz/libpgcode/UsedFilaments.cpp
    src/Slic3r/Biz/libpgcode/Utils.cpp
)

set_target_properties(libpgcode PROPERTIES PREFIX "")

target_include_directories(libpgcode PUBLIC include)
target_include_directories(libpgcode PRIVATE src)

if (NOT EMSCRIPTEN)
    target_link_libraries(libpgcode PUBLIC boost_headeronly)
endif ()
target_link_libraries(libpgcode PUBLIC slic3r-domain slic3r-gcode-reader slic3r-base)
target_link_libraries(libpgcode PRIVATE localesutils)

