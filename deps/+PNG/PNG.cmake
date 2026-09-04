if (MSVC OR APPLE OR EMSCRIPTEN)
    if (APPLE)
        # Only disable NEON extension for Apple ARM builds, leave it enabled for Raspberry PI.
        set(_disable_neon_extension "-DPNG_ARM_NEON:STRING=off")
    else ()
        set(_disable_neon_extension "")
    endif ()

    add_cmake_project(PNG
        URL https://github.com/pnggroup/libpng/archive/refs/tags/v1.6.58.zip
        URL_HASH SHA256=ad8fc23d75a76f352989bbec9e905bdfe8f2d2e77b32e4f2070a4bb1849802ee
        EMSCRIPTEN_PORT libpng
#    EMSCRIPTEN_CMAKE_ARGS
#        -DCMAKE_CXX_FLAGS=-pthread
#        -DCMAKE_C_FLAGS=-pthread
#        -DM_LIBRARY=
    CMAKE_ARGS
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5
        -DPNG_SHARED=OFF
        -DPNG_STATIC=ON
        -DPNG_PREFIX=prusaslicer_
        -DPNG_TESTS=OFF
        -DPNG_EXECUTABLES=OFF
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5
        ${_disable_neon_extension}
)

    set(DEP_PNG_DEPENDS ZLIB)
endif()
