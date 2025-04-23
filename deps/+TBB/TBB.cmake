add_cmake_project(
    TBB
    URL "https://github.com/oneapi-src/oneTBB/archive/refs/tags/v2021.5.0.zip"
    URL_HASH SHA256=83ea786c964a384dd72534f9854b419716f412f9d43c0be88d41874763e7bb47
    CMAKE_ARGS          
        -DTBB_BUILD_SHARED=${BUILD_SHARED_LIBS}
        -DTBB_TEST=OFF
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        -DCMAKE_DEBUG_POSTFIX=_debug
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5
)


