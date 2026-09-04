add_cmake_project(Trumpeloeil
        URL "https://github.com/rollbear/trompeloeil/archive/refs/tags/v49.zip"
        URL_HASH SHA256=53f3339c6f3d48817c911e7a0894f91d3afb3bc1f2ee89a9b779a44c3f2d3a7c
        EMSCRIPTEN_EXCLUDED TRUE
#        CMAKE_ARGS
#            -DCATCH_BUILD_TESTING:BOOL=OFF
#            -DCMAKE_CXX_STANDARD=17
)