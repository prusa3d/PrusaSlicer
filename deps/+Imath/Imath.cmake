add_cmake_project(Imath
        URL "https://github.com/AcademySoftwareFoundation/Imath/archive/refs/tags/v3.2.3.zip"
        URL_HASH SHA256=a2b711354fc249a274b4813fcd7a4a55ded9401e65c642144f4f8d51710f2f06
        CMAKE_ARGS
        -DLIBDEFLATE_BUILD_SHARED_LIB=OFF
        -DLIBDEFLATE_BUILD_GZIP=OFF
)
