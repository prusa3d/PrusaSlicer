add_cmake_project(ZLIB
  URL https://github.com/madler/zlib/releases/download/v1.3.1/zlib131.zip
  URL_HASH SHA256=72af66d44fcc14c22013b46b814d5d2514673dda3d115e64b690c1ad636e7b17
  PATCH_COMMAND ${PATCH_CMD} ${CMAKE_CURRENT_LIST_DIR}/0001-Respect-BUILD_SHARED_LIBS.patch
  CMAKE_ARGS
    -DSKIP_INSTALL_FILES=ON         # Prevent installation of man pages et al.
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
)

