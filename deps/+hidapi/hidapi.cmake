# Reads the 3D mouse on Windows. Linux uses the system library, macOS the 3Dconnexion driver.
if (WIN32)
    add_cmake_project(hidapi
        URL https://github.com/libusb/hidapi/archive/refs/tags/hidapi-0.15.0.zip
        URL_HASH SHA256=dea0c29849e7cac86a4561bf36ba0d97a755a16dc99b9e75714f4af7ea2af2f1
        CMAKE_ARGS
            -DHIDAPI_WITH_TESTS=OFF
            -DHIDAPI_BUILD_HIDTEST=OFF
            -DHIDAPI_BUILD_PP_DATA_DUMP=OFF
    )
endif ()
