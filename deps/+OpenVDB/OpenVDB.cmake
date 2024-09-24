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
    # 8.2 patched
    URL https://github.com/prusa3d/openvdb/archive/a68fd58d0e2b85f01adeb8b13d7555183ab10aa5.zip
    URL_HASH SHA256=f353e7b99bd0cbfc27ac9082de51acf32a8bc0b3e21ff9661ecca6f205ec1d81
    CMAKE_ARGS
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON 
        -DOPENVDB_BUILD_PYTHON_MODULE=OFF
        -DUSE_BLOSC=ON
        -DOPENVDB_CORE_SHARED=${_build_shared} 
        -DOPENVDB_CORE_STATIC=${_build_static}
        -DOPENVDB_ENABLE_RPATH:BOOL=OFF
        -DTBB_STATIC=${_build_static}
        -DOPENVDB_BUILD_VDB_PRINT=${_openvdb_vdbprint}
        -DDISABLE_DEPENDENCY_VERSION_CHECKS=ON # Centos6 has old zlib
)

set(DEP_OpenVDB_DEPENDS TBB Blosc OpenEXR Boost)
