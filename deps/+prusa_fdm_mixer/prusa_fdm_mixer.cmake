add_cmake_project(prusa_fdm_mixer
    URL https://github.com/prusa3d/prusa-fdm-mixer/archive/09d372aeccb4f7b9a0efbe59d99d70dba196814a.zip
    URL_HASH SHA256=795B3DEC5FE64E59054F648D43E527C476A52BC38FE19CF3AE3BE62F817DE660
    SOURCE_SUBDIR cpp
    PATCH_COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt ./cpp/CMakeLists.txt &&
                  ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_LIST_DIR}/Config.cmake.in ./cpp/Config.cmake.in
)
