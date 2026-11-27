add_cmake_project(expected
        URL https://github.com/TartanLlama/expected/archive/refs/tags/v1.3.1.zip
        URL_HASH SHA256=68bbfd81a6d312c4518b5a1831f465fa03811355af9aa9c7403348545d1d2a56
        CMAKE_ARGS
            -DEXPECTED_BUILD_TESTS=OFF
)
