set(_win_flags "")
if (MSVC)
    set(_win_flags "-DCMAKE_C_FLAGS=/D_CRT_DECLARE_NONSTDC_NAMES=0")
endif()

add_cmake_project(libdwarf
    URL "https://github.com/davea42/libdwarf-code/releases/download/v0.11.1/libdwarf-0.11.1.tar.xz"
    URL_HASH SHA256=b5be211b1bd0c1ee41b871b543c73cbff5822f76994f6b160fc70d01d1b5a1bf
    PATCH_COMMAND ${PATCH_CMD} ${CMAKE_CURRENT_LIST_DIR}/libdwarf.patch
    CMAKE_ARGS
        -DBUILD_DWARFDUMP=FALSE
        ${_win_flags}
)

set(DEP_libdwarf_DEPENDS zstd)
