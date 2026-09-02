
add_cmake_project(OpenCSG
    EXCLUDE_FROM_ALL ON # No need to build this lib by default. Only used in experiment in sandboxes/opencsg
    URL https://github.com/floriankirsch/OpenCSG/archive/refs/tags/opencsg-1-8-1-release.zip
    URL_HASH SHA256=405ead7642b052d8ea0a7425d9f8f55dede093c5d3d4af067e94e43c43f5fa79
    PATCH_COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt.in ./CMakeLists.txt
)

set(DEP_OpenCSG_DEPENDS GLEW ZLIB)
