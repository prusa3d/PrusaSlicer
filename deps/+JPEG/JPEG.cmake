add_cmake_project(JPEG
    URL https://github.com/libjpeg-turbo/libjpeg-turbo/archive/refs/tags/3.0.1.zip
    URL_HASH SHA256=d6d99e693366bc03897677650e8b2dfa76b5d6c54e2c9e70c03f0af821b0a52f
    CMAKE_ARGS
        -DENABLE_SHARED=OFF
        -DENABLE_STATIC=ON
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5
)

set(DEP_JPEG_DEPENDS ZLIB)
