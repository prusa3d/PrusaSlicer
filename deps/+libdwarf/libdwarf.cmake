set(_win_flags "")
if (MSVC)
    set(_win_flags "-DCMAKE_C_FLAGS=/D_CRT_DECLARE_NONSTDC_NAMES=0")
endif()

add_cmake_project(libdwarf
    URL "https://github.com/davea42/libdwarf-code/releases/download/v2.3.3/libdwarf-2.3.3.tar.xz"
    URL_HASH SHA256=bde13d1c49be6f2467326a6e0b3919247471455d16eefc3c6be26c7d4baca36a
    PATCH_COMMAND ${PATCH_CMD} ${CMAKE_CURRENT_LIST_DIR}/libdwarf.patch
    CMAKE_ARGS
        -DBUILD_DWARFDUMP=FALSE
        ${_win_flags}
)

set(DEP_libdwarf_DEPENDS zstd)
