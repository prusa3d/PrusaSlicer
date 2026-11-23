add_cmake_project(CLI11
    URL "https://github.com/CLIUtils/CLI11/archive/refs/tags/v2.7.2.zip"
    URL_HASH SHA256=a3ae013321de0bb9195913044a0dcda76cae69bdf34f0cd947998343246498e9
    CMAKE_ARGS
        -DCLI11_BUILD_TESTS=OFF
        -DCLI11_BUILD_EXAMPLES=OFF
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5
)
