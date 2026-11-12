add_cmake_project(OpenEXR
    URL https://github.com/AcademySoftwareFoundation/openexr/releases/download/v3.2.10/openexr-3.2.10.tar.gz
    URL_HASH SHA256=9a61920ae2056b6f0f44e73e7001fa3fac45f266b65c29401cc9d33dd9d41050
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
