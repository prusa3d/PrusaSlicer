if(BUILD_SHARED_LIBS)
    set(_build_shared ON)
    set(_build_static OFF)
else()
    set(_build_shared OFF)
    set(_build_static ON)
endif()

if(MSVC)
    set(LIB_EXT ".lib")
else()
    set(LIB_EXT ".a")
endif()

add_cmake_project(Pahocpp
    # 1.3.13
    URL https://github.com/eclipse/paho.mqtt.cpp/archive/5e0d1bf37b4826d680ec066ec42afd133851a681.zip
    URL_HASH SHA256=bdd104b8c07d908743b919229e946026c51dc0bf3e157888f43a9490832bb97b
    CMAKE_ARGS
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5
        -DPAHO_WITH_SSL=TRUE
        -DPAHO_BUILD_SHARED=${_build_shared}
        -DPAHO_BUILD_STATIC=${_build_static}
        -DPAHO_BUILD_DEB_PACKAGE=FALSE
        -DPAHO_BUILD_DOCUMENTATION=FALSE
        -DPAHO_BUILD_SAMPLES=FALSE
        -DPAHO_BUILD_TESTS=FALSE
        -DPAHO_WITH_MQTT_C=FALSE
        -DPAHO_MQTT_C_LIBRARIES=${${PROJECT_NAME}_DEP_INSTALL_PREFIX}/lib/paho-mqtt3as-static${LIB_EXT}
        -DCMAKE_PREFIX_PATH=${${PROJECT_NAME}_DEP_INSTALL_PREFIX}
)

set(DEP_Pahocpp_DEPENDS Paho)

        