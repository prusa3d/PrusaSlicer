if(BUILD_SHARED_LIBS)
    set(_build_shared ON)
    set(_build_static OFF)
else()
    set(_build_shared OFF)
    set(_build_static ON)
endif()

add_cmake_project(Paho
    # 1.3.13
    URL https://github.com/eclipse/paho.mqtt.c/archive/07a875788d8cc6f5833b12581d6e3e349b34d719.zip
    URL_HASH SHA256=4548f8d52b12e0917daa9a502e991840469bc39b3a87b23b6f70bd3283f1ef7c
    CMAKE_ARGS
        -DPAHO_WITH_SSL=FALSE
        -DPAHO_WITH_LIBUUID=FALSE
        -DPAHO_BUILD_SHARED=${_build_shared}
        -DPAHO_BUILD_STATIC=${_build_static}
        -DPAHO_BUILD_DOCUMENTATION=FALSE
        -DPAHO_BUILD_SAMPLES=FALSE
        -DPAHO_BUILD_DEB_PACKAGE=FALSE
        -DPAHO_ENABLE_TESTING=FALSE
        -DPAHO_ENABLE_CPACK=FALSE
        -DPAHO_HIGH_PERFORMANCE=FALSE
        -DPAHO_USE_SELECT=FALSE
)
