# Reads the 3D mouse on Linux through the spacenavd daemon. Without it only hidapi is used.
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(_install_dir ${${PROJECT_NAME}_DEP_INSTALL_PREFIX})

    ExternalProject_Add(dep_libspnav
        URL https://github.com/FreeSpacenav/libspnav/releases/download/v1.2/libspnav-1.2.tar.gz
        URL_HASH SHA256=093747e7e03b232e08ff77f1ad7f48552c06ac5236316a5012db4269951c39db
        DOWNLOAD_DIR ${${PROJECT_NAME}_DEP_DOWNLOAD_DIR}/libspnav
        BUILD_IN_SOURCE ON
        # X11 support is not needed and the static library would require linking X11.
        CONFIGURE_COMMAND ./configure --prefix=${_install_dir} --disable-x11 --disable-debug
        BUILD_COMMAND make -j "CC=${CMAKE_C_COMPILER}"
        # Its configure cannot disable the shared library, the dependencies are linked statically.
        INSTALL_COMMAND sh -c "make install && rm -f '${_install_dir}'/lib/libspnav.so*"
    )
endif ()
