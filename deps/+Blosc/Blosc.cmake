if(BUILD_SHARED_LIBS)
    set(_build_shared ON)
    set(_build_static OFF)
else()
    set(_build_shared OFF)
    set(_build_static ON)
endif()

add_cmake_project(Blosc
    URL https://github.com/Blosc/c-blosc/archive/8724c06e3da90f10986a253814af18ca081d8de0.zip
    URL_HASH SHA256=53986fd04210b3d94124b7967c857f9766353e576a69595a9393999e0712c035
    CMAKE_ARGS
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        -DBUILD_SHARED=${_build_shared} 
        -DBUILD_STATIC=${_build_static}
        -DBUILD_TESTS=OFF 
        -DBUILD_BENCHMARKS=OFF 
        -DPREFER_EXTERNAL_ZLIB=ON
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5
)

set(DEP_Blosc_DEPENDS ZLIB)
