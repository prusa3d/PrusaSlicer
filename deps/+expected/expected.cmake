add_cmake_project(expected
        URL https://github.com/TartanLlama/expected/archive/refs/tags/v1.1.0.zip
        URL_HASH SHA256=4b2a347cf5450e99f7624247f7d78f86f3adb5e6acd33ce307094e9507615b78
        CMAKE_ARGS
            -DEXPECTED_BUILD_TESTS=OFF
)
