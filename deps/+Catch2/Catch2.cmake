add_cmake_project(Catch2
    URL "https://github.com/catchorg/Catch2/archive/refs/tags/v3.16.1.zip"
    URL_HASH SHA256=7509086e230246c393e82a4065b0c2b3b934c3f9e0572b7ab4927a74cf50efc0
    EMSCRIPTEN_EXCLUDED TRUE
    CMAKE_ARGS
        -DCATCH_BUILD_TESTING:BOOL=OFF
        -DCMAKE_CXX_STANDARD=20
)
