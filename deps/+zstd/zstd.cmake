add_cmake_project(zstd
    URL "https://github.com/facebook/zstd/releases/download/v1.5.6/zstd-1.5.6.tar.gz"
    URL_HASH SHA256=8c29e06cf42aacc1eafc4077ae2ec6c6fcb96a626157e0593d5e82a34fd403c1
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
