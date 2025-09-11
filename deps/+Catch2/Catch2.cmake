add_cmake_project(Catch2
    URL "https://github.com/catchorg/Catch2/archive/refs/tags/v3.8.0.zip"
    URL_HASH SHA256=bffd2c45a84e5a4b0c17e695798e8d2f65931cbaf5c7556d40388d1d8d04eb83
    CMAKE_ARGS
        -DCATCH_BUILD_TESTING:BOOL=OFF
        -DCMAKE_CXX_STANDARD=17
)