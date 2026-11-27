add_cmake_project(OpenEXR
    URL https://github.com/AcademySoftwareFoundation/openexr/releases/download/v3.5.5/openexr-3.5.5.tar.gz
    URL_HASH SHA256=66699e84a31fb787461f0fdbeff7507479ae4096119d3a12670f9baedb8288eb
    CMAKE_ARGS
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        -DBUILD_TESTING=OFF
        -DPYILMBASE_ENABLE:BOOL=OFF
        -DOPENEXR_VIEWERS_ENABLE:BOOL=OFF
        -DOPENEXR_BUILD_UTILS:BOOL=OFF
        -DOPENEXR_BUILD_TOOLS:BOOL=OFF
        -DOPENEXR_BUILD_EXAMPLES:BOOL=OFF
        -DBUILD_WEBSITE:BOOL=OFF
        -DOPENEXR_IS_SUBPROJECT:BOOL=ON
    EMSCRIPTEN_CMAKE_ARGS
        -DCMAKE_CXX_FLAGS=--use-port=zlib
        -DCMAKE_C_FLAGS=--use-port=zlib
)

set(DEP_OpenEXR_DEPENDS ZLIB libdeflate Imath)
