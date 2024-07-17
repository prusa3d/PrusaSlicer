
set(_context_abi_line "")
set(_context_arch_line "")
set(_context_implementation_line "-DBOOST_CONTEXT_IMPLEMENTATION:STRING=fcontext")
if (APPLE AND CMAKE_OSX_ARCHITECTURES)
    if (CMAKE_OSX_ARCHITECTURES MATCHES "x86")
        set(_context_abi_line "-DBOOST_CONTEXT_ABI:STRING=sysv")
    elseif (CMAKE_OSX_ARCHITECTURES MATCHES "arm")
        set (_context_abi_line "-DBOOST_CONTEXT_ABI:STRING=aapcs")
    endif ()
    set(_context_arch_line "-DBOOST_CONTEXT_ARCHITECTURE:STRING=${CMAKE_OSX_ARCHITECTURES}")
endif ()

if (MSVC AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(ARM64|aarch64)$")
    set(_context_implementation_line "-DBOOST_CONTEXT_IMPLEMENTATION=winfib")
endif()

add_cmake_project(Boost
    URL "https://github.com/boostorg/boost/releases/download/boost-1.83.0/boost-1.83.0.zip"
    URL_HASH SHA256=9effa3d7f9d92b8e33e2b41d82f4358f97ff7c588d5918720339f2b254d914c6
    LIST_SEPARATOR |
    CMAKE_ARGS
        -DBOOST_EXCLUDE_LIBRARIES:STRING=contract|fiber|numpy|stacktrace|wave|test
        -DBOOST_LOCALE_ENABLE_ICU:BOOL=OFF # do not link to libicu, breaks compatibility between distros
        -DBUILD_TESTING:BOOL=OFF
        "${_context_abi_line}"
        "${_context_arch_line}"
        "${_context_implementation_line}"
)

set(DEP_Boost_DEPENDS ZLIB)
