add_cmake_project(cpptrace
    URL "https://github.com/jeremy-rifkin/cpptrace/archive/refs/tags/v1.0.4.zip"
    URL_HASH SHA256=3fca735735cd74d646833f86112223f8aceb965055c8f96686a41d85948b50ba
    PATCH_COMMAND ${PATCH_CMD} ${CMAKE_CURRENT_LIST_DIR}/cpptrace.patch
    CMAKE_ARGS
        -DCPPTRACE_USE_EXTERNAL_ZSTD=ON
        -DCPPTRACE_USE_EXTERNAL_LIBDWARF=ON
)

set(DEP_cpptrace_DEPENDS zstd libdwarf)
