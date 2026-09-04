project(slic3r-biz-arrange)

add_library(slic3r-biz-arrange
    include/Slic3r/Biz/Arrange/Kernels/IKernel.hpp
    include/Slic3r/Biz/Arrange/Kernels/CompactifyKernel.hpp
    src/Slic3r/Biz/Arrange/Kernels/CompactifyKernel.cpp
    include/Slic3r/Biz/Arrange/Kernels/GravityKernel.hpp
    src/Slic3r/Biz/Arrange/Kernels/GravityKernel.cpp
    include/Slic3r/Biz/Arrange/Kernels/RectangleOverfitKernelWrapper.hpp
    src/Slic3r/Biz/Arrange/Kernels/RectangleOverfitKernelWrapper.cpp
    include/Slic3r/Biz/Arrange/Kernels/WipeTowerPositioningKernel.hpp
    src/Slic3r/Biz/Arrange/Kernels/WipeTowerPositioningKernel.cpp
    include/Slic3r/Biz/Arrange/Kernels/SVGDebugOutputKernelWrapper.hpp
    src/Slic3r/Biz/Arrange/Kernels/SVGDebugOutputKernelWrapper.cpp
    include/Slic3r/Biz/Arrange/Kernels/TMArrangeKernel.hpp
    src/Slic3r/Biz/Arrange/Kernels/TMArrangeKernel.cpp

    src/Slic3r/Biz/Arrange/Kernels/KernelUtils.hpp
    src/Slic3r/Biz/Arrange/Kernels/KernelUtils.cpp

    include/Slic3r/Biz/Arrange/ArrangeItem.hpp
    src/Slic3r/Biz/Arrange/ArrangeItem.cpp
    src/Slic3r/Biz/Arrange/MinAreaBoundingBox.hpp
    src/Slic3r/Biz/Arrange/MinAreaBoundingBox.cpp
    include/Slic3r/Biz/Arrange/Bed.hpp
    src/Slic3r/Biz/Arrange/Bed.cpp
    include/Slic3r/Biz/Arrange/Packer.hpp
    src/Slic3r/Biz/Arrange/Packer.cpp
    include/Slic3r/Biz/Arrange/PackingContext.hpp
    src/Slic3r/Biz/Arrange/PackingContext.cpp
    include/Slic3r/Biz/Arrange/Utils.hpp
    src/Slic3r/Biz/Arrange/Utils.cpp
    include/Slic3r/Biz/Arrange/Arrange.hpp
    src/Slic3r/Biz/Arrange/Arrange.cpp

    src/Slic3r/Biz/Arrange/EdgeCache.hpp
    src/Slic3r/Biz/Arrange/EdgeCache.cpp
    src/Slic3r/Biz/Arrange/CircularEdgeIterator.hpp
    src/Slic3r/Biz/Arrange/NFP.hpp
    src/Slic3r/Biz/Arrange/NFP.cpp
    src/Slic3r/Biz/Arrange/Tesselate.hpp
    src/Slic3r/Biz/Arrange/Tesselate.cpp
)
target_include_directories(slic3r-biz-arrange PUBLIC include)
target_include_directories(slic3r-biz-arrange PRIVATE src)

target_link_libraries(slic3r-biz-arrange PRIVATE int128 libnest2d)
target_link_libraries(slic3r-biz-arrange PUBLIC slic3r-domain libslic3r)

if (SLIC3R_PCH)
    target_precompile_headers(slic3r-biz-arrange REUSE_FROM slic3r-domain)
endif()

if(SLIC3R_BUILD_TESTS)
    add_executable(slic3r-biz-arrange-tests
        test/Slic3r/Biz/Arrange/PackerTests.cpp
        test/Slic3r/Biz/Arrange/BedTests.cpp
        test/Slic3r/Biz/Arrange/ArrangeTests.cpp

    )
    target_include_directories(slic3r-biz-arrange-tests PRIVATE test)
    target_link_libraries(slic3r-biz-arrange-tests PRIVATE slic3r-biz-arrange Catch2::Catch2WithMain)

    add_test(NAME slic3r-biz-arrange-tests
        COMMAND slic3r-biz-arrange-tests ${CATCH_ALL_ARGS}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )

    if (WIN32)
        prusaslicer_copy_dlls(slic3r-biz-arrange-tests)
    endif ()
endif ()
