project(slic3r-arrange)
cmake_minimum_required(VERSION 3.13)

add_library(slic3r-arrange
    include/arrange/Beds.hpp
    include/arrange/ArrangeItemTraits.hpp
    include/arrange/PackingContext.hpp
    include/arrange/NFP/NFPArrangeItemTraits.hpp
    include/arrange/NFP/NFP.hpp
    include/arrange/ArrangeBase.hpp
    include/arrange/DataStoreTraits.hpp
    include/arrange/ArrangeFirstFit.hpp
    include/arrange/NFP/PackStrategyNFP.hpp
    include/arrange/NFP/Kernels/TMArrangeKernel.hpp
    include/arrange/NFP/Kernels/GravityKernel.hpp
    include/arrange/NFP/RectangleOverfitPackingStrategy.hpp
    include/arrange/NFP/EdgeCache.hpp
    include/arrange/NFP/Kernels/KernelTraits.hpp
    include/arrange/NFP/NFPConcave_Tesselate.hpp
    include/arrange/NFP/Kernels/KernelUtils.hpp
    include/arrange/NFP/Kernels/CompactifyKernel.hpp
    include/arrange/NFP/Kernels/RectangleOverfitKernelWrapper.hpp
    include/arrange/NFP/Kernels/SVGDebugOutputKernelWrapper.hpp

    src/Beds.cpp
    src/NFP/NFP.cpp
    src/NFP/NFPConcave_Tesselate.cpp
    src/NFP/EdgeCache.cpp
    src/NFP/CircularEdgeIterator.hpp
)

target_include_directories(slic3r-arrange PRIVATE src)
target_include_directories(slic3r-arrange PUBLIC include)
target_link_libraries(slic3r-arrange PUBLIC libslic3r)
