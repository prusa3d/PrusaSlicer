add_cmake_project(rangev3
        URL https://github.com/ericniebler/range-v3/archive/refs/tags/0.12.0.tar.gz
        URL_HASH SHA256=015adb2300a98edfceaf0725beec3337f542af4915cec4d0b89fa0886f4ba9cb
        CMAKE_ARGS
            -DRANGE_V3_DOCS=OFF
            -DRANGE_V3_TESTS=OFF
            -DRANGE_V3_EXAMPLES=OFF
            -DRANGE_V3_PERF=OFF
)
