set(_patch_cmd PATCH_COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt.patched build/cmake/CMakeLists.txt)

add_cmake_project(
  GLEW
  URL https://sourceforge.net/projects/glew/files/glew/2.2.0/glew-2.2.0.zip
  URL_HASH SHA256=a9046a913774395a095edcc0b0ac2d81c3aacca61787b39839b941e9be14e0d4
  PATCH_COMMAND "${_patch_cmd}"
  SOURCE_SUBDIR build/cmake
  CMAKE_ARGS
    -DBUILD_UTILS=OFF
)