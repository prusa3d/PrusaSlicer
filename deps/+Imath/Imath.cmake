add_cmake_project(Imath
        URL "https://github.com/AcademySoftwareFoundation/Imath/archive/refs/tags/v3.1.12.zip"
        URL_HASH SHA256=82d8f31c46e73dba92525bea29c4fe077f6a7d3b978d5067a15030413710bf46
        CMAKE_ARGS
        -DLIBDEFLATE_BUILD_SHARED_LIB=OFF
        -DLIBDEFLATE_BUILD_GZIP=OFF
)
