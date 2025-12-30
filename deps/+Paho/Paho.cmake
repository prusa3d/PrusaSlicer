set(DEP_Paho_DEPENDS OpenSSL)

if(BUILD_SHARED_LIBS)
    set(_paho_build_shared ON)
    set(_paho_build_static OFF)
else()
    set(_paho_build_shared OFF)
    set(_paho_build_static ON)
endif()

add_cmake_project(Paho
    # 1.3.13
    URL https://github.com/eclipse/paho.mqtt.c/archive/07a875788d8cc6f5833b12581d6e3e349b34d719.zip
    URL_HASH SHA256=4548f8d52b12e0917daa9a502e991840469bc39b3a87b23b6f70bd3283f1ef7c
    
    CMAKE_ARGS
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        -DPAHO_WITH_SSL=ON
        -DCMAKE_PREFIX_PATH=${${PROJECT_NAME}_DEP_INSTALL_PREFIX}
        -DOPENSSL_ROOT_DIR=${${PROJECT_NAME}_DEP_INSTALL_PREFIX}
        -DOPENSSL_USE_STATIC_LIBS=TRUE
        -DPAHO_BUILD_SHARED=${_paho_build_shared}
        -DPAHO_BUILD_STATIC=${_paho_build_static}
        -DPAHO_WITH_LIBUUID=OFF
        -DPAHO_BUILD_DOCUMENTATION=OFF
        -DPAHO_BUILD_SAMPLES=OFF
        -DPAHO_BUILD_DEB_PACKAGE=OFF
        -DPAHO_ENABLE_TESTING=OFF
        -DPAHO_ENABLE_CPACK=OFF
        -DPAHO_HIGH_PERFORMANCE=OFF
        -DPAHO_USE_SELECT=OFF
)


