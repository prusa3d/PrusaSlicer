include(GNUInstallDirs)

set(_qhull_static_libs "-DBUILD_STATIC_LIBS:BOOL=ON")
set(_qhull_shared_libs "-DBUILD_SHARED_LIBS:BOOL=OFF")
if (BUILD_SHARED_LIBS)
    set(_qhull_static_libs "-DBUILD_STATIC_LIBS:BOOL=OFF")
    set(_qhull_shared_libs "-DBUILD_SHARED_LIBS:BOOL=ON")
endif ()

add_cmake_project(Qhull
    URL "https://github.com/qhull/qhull/archive/v8.1-alpha6.zip"
    URL_HASH SHA256=d79b73774236f82e4940ce74c8b6cbb6ef3c72ef053d01d1bbfb19ab65dbfc22
    CMAKE_ARGS 
        -DINCLUDE_INSTALL_DIR=${CMAKE_INSTALL_INCLUDEDIR}
        -DBUILD_APPLICATIONS:BOOL=OFF
        ${_qhull_shared_libs}
        ${_qhull_static_libs}
        -DQHULL_ENABLE_TESTING:BOOL=OFF
)
