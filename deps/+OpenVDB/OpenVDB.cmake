if(BUILD_SHARED_LIBS)
    set(_build_shared ON)
    set(_build_static OFF)
else()
    set(_build_shared OFF)
    set(_build_static ON)
endif()

set (_openvdb_vdbprint ON)
if (${CMAKE_SYSTEM_PROCESSOR} MATCHES "arm" OR NOT ${CMAKE_BUILD_TYPE} STREQUAL Release)
    # Build fails on raspberry pi due to missing link directive to latomic
    # Let's hope it will be fixed soon.
    set (_openvdb_vdbprint OFF)
endif ()

add_cmake_project(OpenVDB
    # 11.0.0 patched
    URL https://github.com/AcademySoftwareFoundation/openvdb/archive/refs/tags/v11.0.0.zip
    URL_HASH SHA256=db7e1aacd0a634195574b2e6a43d268d063830628501fd0a94a99bf252d01fb6
    PATCH_COMMAND ${PATCH_CMD} ${CMAKE_CURRENT_LIST_DIR}/openvdb.patch
    CMAKE_ARGS
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        -DOPENVDB_BUILD_PYTHON_MODULE=OFF
        -DOPENVDB_BUILD_BINARIES=OFF
        -DUSE_BLOSC=ON
        -DOPENVDB_CORE_SHARED=${_build_shared}
        -DOPENVDB_CORE_STATIC=${_build_static}
        -DOPENVDB_ENABLE_RPATH:BOOL=OFF
        -DTBB_STATIC=${_build_static}
        -DOPENVDB_BUILD_VDB_PRINT=${_openvdb_vdbprint}
        -DUSE_CCACHE=OFF
        -DOPENVDB_ABI_VERSION_NUMBER=11
        -DOPENVDB_INSTALL_CMAKE_MODULES=OFF
        -DUSE_EXPLICIT_INSTANTIATION=OFF # https://github.com/AcademySoftwareFoundation/openvdb/issues/1624
        -DOPENVDB_BUILD_VDB_PRINT=OFF
    EMSCRIPTEN_CMAKE_ARGS
        "-DCMAKE_CXX_FLAGS=-s USE_PTHREADS=1 -s WASM=1 -s SHARED_MEMORY ${CMAKE_CXX_FLAGS}"
        "-DCMAKE_EXE_LINKER_FLAGS=-s USE_PTHREADS=1 -s WASM=1 -s SHARED_MEMORY ${CMAKE_EXE_LINKER_FLAGS}"
        "-DCMAKE_SHARED_LINKER_FLAGS=-s USE_PTHREADS=1 -s WASM=1 -s SHARED_MEMORY ${CMAKE_SHARED_LINKER_FLAGS}"
        #"-DCMAKE_STATIC_LINKER_FLAGS=-sUSE_PTHREADS=1 -sWASM=1 -sSHARED_MEMORY ${CMAKE_STATIC_LINKER_FLAGS}"
        -DCMAKE_FIND_ROOT_PATH=/
        -DTHREADS_PREFER_PTHREAD_FLAG=ON
)

#if (EMSCRIPTEN)
#    add_library(Threads::Threads INTERFACE IMPORTED)
#endif ()

set(DEP_OpenVDB_DEPENDS TBB Blosc OpenEXR Boost)
