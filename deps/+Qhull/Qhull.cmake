include(GNUInstallDirs)

set(_qhull_static_libs "-DBUILD_STATIC_LIBS:BOOL=ON")
set(_qhull_shared_libs "-DBUILD_SHARED_LIBS:BOOL=OFF")
if (BUILD_SHARED_LIBS)
    set(_qhull_static_libs "-DBUILD_STATIC_LIBS:BOOL=OFF")
    set(_qhull_shared_libs "-DBUILD_SHARED_LIBS:BOOL=ON")
endif ()

add_cmake_project(Qhull
    URL "https://github.com/qhull/qhull/archive/refs/tags/v8.1-alpha3.zip"
    URL_HASH SHA256=7bd9b5ffae01e69c2ead52f9a9b688af6c65f9a1da05da0a170fa20d81404c06
    CMAKE_ARGS 
        -DINCLUDE_INSTALL_DIR=${CMAKE_INSTALL_INCLUDEDIR}
        -DBUILD_APPLICATIONS:BOOL=OFF
        ${_qhull_shared_libs}
        ${_qhull_static_libs}
        -DQHULL_ENABLE_TESTING:BOOL=OFF
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5
)
