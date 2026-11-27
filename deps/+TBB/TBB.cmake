# Force -Wno-error=stringop-overflow if GCC compiler is used
# otherwise oneTBB compilation fails on GCC 14+
set(_tbb_cxx_flags "")
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(_tbb_cxx_flags "-Wno-error=stringop-overflow")
endif()

add_cmake_project(
    TBB
    URL "https://github.com/oneapi-src/oneTBB/archive/refs/tags/v2023.2.0.zip"
    URL_HASH SHA256=6a7cf5f1873afd6efb82af3de40ae08f4898cd4fe7de5d7bf96cd24a6546cc9b
    CMAKE_ARGS
        -DTBB_BUILD_SHARED=${BUILD_SHARED_LIBS}
        -DTBB_TEST=OFF
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        -DCMAKE_DEBUG_POSTFIX=_debug
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5
        -DCMAKE_CXX_FLAGS=${_tbb_cxx_flags}
)
