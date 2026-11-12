include(GNUInstallDirs)
add_cmake_project(
        SDL
        URL https://github.com/libsdl-org/SDL/releases/download/release-2.32.4/SDL2-2.32.4.zip
        URL_HASH SHA256=030bd2768653bf11cefef06aaa99e61e850326871d283b81d324fb7242b6c702
        CMAKE_ARGS
            -DINCLUDE_INSTALL_DIR=${CMAKE_INSTALL_INCLUDEDIR}
            -DBUILD_SHARED_LIBS=ON
            -DCMAKE_BUILD_SHARED_LIBS=ON
        EMSCRIPTEN_PORT sdl2
)
