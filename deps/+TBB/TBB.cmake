add_cmake_project(
    TBB
    URL "https://github.com/uxlfoundation/oneTBB/archive/refs/tags/v2022.2.0.zip"
    URL_HASH SHA256=5470ccd4d127c6d680e46fee016531e452dc653d8eb1a40bd78ff087600fa582
    CMAKE_ARGS          
        -DTBB_BUILD_SHARED=${BUILD_SHARED_LIBS}
        -DTBB_TEST=OFF
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        -DCMAKE_DEBUG_POSTFIX=_debug
)


