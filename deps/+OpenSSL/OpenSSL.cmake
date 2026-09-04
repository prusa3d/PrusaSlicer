
include(ProcessorCount)
ProcessorCount(NPROC)

set(_conf_cmd "./config")
set(_cross_arch "")
set(_cross_comp_prefix_line "")
set(_apple_target_flags "")

if (WIN32)
    set(_exclude_from_all ON)
else ()
    set(_exclude_from_all OFF)
endif ()

if (APPLE)
    # OpenSSL's own ./config/./Configure has no notion of CMAKE_OSX_ARCHITECTURES - unlike
    # the CMake-based deps (add_cmake_project forwards it automatically), this is a raw
    # ExternalProject driving OpenSSL's Perl Configure script directly. Left to plain
    # "./config" with no target argument, it auto-detects the architecture of the machine
    # actually running the build (uname -m) rather than the one CMAKE_OSX_ARCHITECTURES
    # asks for, which silently produces a wrong-arch static library whenever the build
    # machine's native arch differs from the target (e.g. cross-building arm64 on an Intel
    # CI runner). Map the target explicitly instead.
    set(_conf_cmd "./Configure")
    set(_target_arch "${CMAKE_OSX_ARCHITECTURES}")
    if (NOT _target_arch)
        set(_target_arch "${CMAKE_SYSTEM_PROCESSOR}")
    endif ()

    if (_target_arch MATCHES "arm64")
        set(_cross_arch "darwin64-arm64-cc")
    elseif (_target_arch MATCHES "x86_64" OR _target_arch MATCHES "AMD64")
        set(_cross_arch "darwin64-x86_64-cc")
    else ()
        message(FATAL_ERROR "OpenSSL: unsupported target architecture: '${_target_arch}'")
    endif ()
    if (CMAKE_OSX_DEPLOYMENT_TARGET)
        list(APPEND _apple_target_flags "-mmacosx-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}")
    endif ()
    if (CMAKE_OSX_SYSROOT)
        list(APPEND _apple_target_flags "-isysroot" "${CMAKE_OSX_SYSROOT}")
    endif ()
elseif (CMAKE_CROSSCOMPILING)
    set(_conf_cmd "./Configure")
    set(_cross_comp_prefix_line "--cross-compile-prefix=${TOOLCHAIN_PREFIX}-")

    if (${CMAKE_SYSTEM_PROCESSOR} STREQUAL "aarch64" OR ${CMAKE_SYSTEM_PROCESSOR} STREQUAL "arm64")
        set(_cross_arch "linux-aarch64")
    elseif (${CMAKE_SYSTEM_PROCESSOR} STREQUAL "armhf") # For raspbian
        # TODO: verify
        set(_cross_arch "linux-armv4")
    endif ()
endif ()

ExternalProject_Add(dep_OpenSSL
    EXCLUDE_FROM_ALL ${_exclude_from_all}
    URL "https://github.com/openssl/openssl/releases/download/openssl-4.0.1/openssl-4.0.1.tar.gz"
    URL_HASH SHA256=2db3f3a0d6ea4b59e1f094ace2c8cd536dffb87cdc39084c5afa1e6f7f37dd09
    DOWNLOAD_DIR ${${PROJECT_NAME}_DEP_DOWNLOAD_DIR}/OpenSSL
    BUILD_IN_SOURCE ON
    CONFIGURE_COMMAND ${_conf_cmd} ${_cross_arch}
        "--prefix=${${PROJECT_NAME}_DEP_INSTALL_PREFIX}"
        ${_cross_comp_prefix_line}
        no-shared
        -Wa,--noexecstack
        ${_apple_target_flags}
    BUILD_COMMAND make depend && make "-j${NPROC}"
    INSTALL_COMMAND make install_sw
)