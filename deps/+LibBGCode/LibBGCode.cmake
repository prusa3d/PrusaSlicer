set(LibBGCode_SOURCE_DIR "" CACHE PATH "Optionally specify local LibBGCode source directory")

set(_source_dir_line
    URL https://github.com/prusa3d/libbgcode/archive/5041c093b33e2748e76d6b326f2251310823f3df.zip
    URL_HASH SHA256=c323aa196a82d75f08a5b114c95f2d1a019e84b555a196e55d8ea52e5787284c)

if (LibBGCode_SOURCE_DIR)
    set(_source_dir_line "SOURCE_DIR;${LibBGCode_SOURCE_DIR};BUILD_ALWAYS;ON")
endif ()

# add_cmake_project(LibBGCode_deps
#     ${_source_dir_line}
#     SOURCE_SUBDIR deps
#     BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/
#     CMAKE_ARGS
#         -DLibBGCode_Deps_DEP_DOWNLOAD_DIR:PATH=${${PROJECT_NAME}_DEP_DOWNLOAD_DIR}
#         -DDEP_CMAKE_OPTS:STRING=-DCMAKE_POSITION_INDEPENDENT_CODE:BOOL=ON
#         -DLibBGCode_Deps_SELECT_ALL:BOOL=OFF
#         -DLibBGCode_Deps_SELECT_heatshrink:BOOL=ON
#         -DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}
#         -DLibBGCode_Deps_DEP_INSTALL_PREFIX=${${PROJECT_NAME}_DEP_INSTALL_PREFIX}
# )

add_cmake_project(LibBGCode
    ${_source_dir_line}
    CMAKE_ARGS
        -DLibBGCode_BUILD_TESTS:BOOL=OFF
        -DLibBGCode_BUILD_CMD_TOOL:BOOL=OFF
)

# set(DEP_LibBGCode_deps_DEPENDS ZLIB Boost)
# set(DEP_LibBGCode_DEPENDS LibBGCode_deps)
set(DEP_LibBGCode_DEPENDS ZLIB Boost heatshrink)
