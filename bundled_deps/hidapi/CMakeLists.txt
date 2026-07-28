if (WIN32)
    set(HIDAPI_IMPL win/hid.c)

    add_library(hidapi STATIC ${HIDAPI_IMPL})
    target_include_directories(hidapi PUBLIC include)

elseif (APPLE)
    set(HIDAPI_IMPL mac/hid.c)

    add_library(hidapi STATIC ${HIDAPI_IMPL})
    target_include_directories(hidapi PUBLIC include)

else ()
    find_package(PkgConfig REQUIRED)

    pkg_check_modules(HIDAPI IMPORTED_TARGET hidapi-hidraw)
    if (NOT HIDAPI_FOUND)
        pkg_check_modules(HIDAPI REQUIRED IMPORTED_TARGET hidapi-libusb)
    endif()

    if (NOT TARGET PkgConfig::HIDAPI)
        message(FATAL_ERROR "hidapi not found (tried hidraw and libusb)")
    endif()

    add_library(hidapi INTERFACE)
    target_link_libraries(hidapi INTERFACE PkgConfig::HIDAPI)
endif()
