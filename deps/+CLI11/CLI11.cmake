add_cmake_project(CLI11
    URL "https://github.com/CLIUtils/CLI11/archive/refs/tags/v2.5.0.zip"
    URL_HASH SHA256=887270cae374a0b9e22b39647f9fc4bc742587fb26d6a221da2d2bbcf3109b0b
    CMAKE_ARGS
        -DCLI11_BUILD_TESTS=OFF
        -DCLI11_BUILD_EXAMPLES=OFF
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5
)
