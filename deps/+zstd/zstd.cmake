add_cmake_project(zstd
    URL "https://github.com/facebook/zstd/releases/download/v1.6.0/zstd-1.6.0.tar.gz"
    URL_HASH SHA256=2ba599ae2a257976278e7e71e998433a387bfd18aa7b78678229fd5d84f5e9b8
    CONFIGURE_COMMAND ${CMAKE_COMMAND}
        ${DEP_CMAKE_OPTS}
        -DCMAKE_INSTALL_PREFIX:PATH=${${PROJECT_NAME}_DEP_INSTALL_PREFIX}
        -DZSTD_BUILD_PROGRAMS=OFF
        -DZSTD_BUILD_CONTRIB=OFF
        -DZSTD_BUILD_TESTS=OFF
        -DZSTD_BUILD_STATIC=ON
        -DZSTD_BUILD_SHARED=OFF
        -DZSTD_LEGACY_SUPPORT=OFF
        -DCMAKE_POLICY_DEFAULT_CMP0074=NEW
        -G ${CMAKE_GENERATOR}
        ${CMAKE_CURRENT_BINARY_DIR}/dep_zstd-prefix/src/dep_zstd/build/cmake
)
