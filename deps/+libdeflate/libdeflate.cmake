add_cmake_project(libdeflate
        URL "https://github.com/ebiggers/libdeflate/archive/refs/tags/v1.26.zip"
        URL_HASH SHA256=cc64b9177e8c7eed2d3960a3dd9b0aa061ddc4c43c9067af1209e208e4a8144f
        CMAKE_ARGS
        -DLIBDEFLATE_BUILD_SHARED_LIB=OFF
        -DLIBDEFLATE_BUILD_GZIP=OFF
)
