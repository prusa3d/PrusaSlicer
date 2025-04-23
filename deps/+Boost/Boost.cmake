
set(_context_abi_line "")
set(_context_arch_line "")
if (APPLE AND CMAKE_OSX_ARCHITECTURES)
    if (CMAKE_OSX_ARCHITECTURES MATCHES "x86")
        set(_context_abi_line "-DBOOST_CONTEXT_ABI:STRING=sysv")
    elseif (CMAKE_OSX_ARCHITECTURES MATCHES "arm")
        set (_context_abi_line "-DBOOST_CONTEXT_ABI:STRING=aapcs")
    endif ()
    set(_context_arch_line "-DBOOST_CONTEXT_ARCHITECTURE:STRING=${CMAKE_OSX_ARCHITECTURES}")
endif ()

add_cmake_project(Boost
    URL "https://github.com/boostorg/boost/releases/download/boost-1.87.0/boost-1.87.0-cmake.zip"
    URL_HASH SHA256=03530dec778bc1b85b070f0b077f3b01fd417133509bb19fe7c142e47777a87b
    LIST_SEPARATOR |
    CMAKE_ARGS
        -DBOOST_EXCLUDE_LIBRARIES:STRING=contract|fiber|numpy|stacktrace|wave|test
        -DBOOST_LOCALE_ENABLE_ICU:BOOL=OFF # do not link to libicu, breaks compatibility between distros
        -DBUILD_TESTING:BOOL=OFF
        -DBOOST_IOSTREAMS_ENABLE_ZSTD:BOOL=OFF
        "${_context_abi_line}"
        "${_context_arch_line}"
)

set(DEP_Boost_DEPENDS ZLIB)
