add_cmake_project(
  GLEW
  URL https://sourceforge.net/projects/glew/files/glew/2.2.0/glew-2.2.0.zip
  URL_HASH SHA256=a9046a913774395a095edcc0b0ac2d81c3aacca61787b39839b941e9be14e0d4
  SOURCE_SUBDIR build/cmake
  PATCH_COMMAND COMMAND ${PATCH_CMD} ${CMAKE_CURRENT_LIST_DIR}/GLEW.patch
  CMAKE_ARGS
    -DBUILD_UTILS=OFF
)