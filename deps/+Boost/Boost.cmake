
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

set(_excluded_libs contract|fiber|numpy|stacktrace|wave|test|log)
if (EMSCRIPTEN)
    set(_excluded_libs ${_excluded_libs}|context|coroutine|asio)
    set(CMAKE_CXX_FLAGS "-s USE_PTHREADS=1 ${CMAKE_CXX_FLAGS}")
endif ()

add_cmake_project(Boost
    URL "https://github.com/boostorg/boost/releases/download/boost-1.92.0/boost-1.92.0-cmake.zip"
    URL_HASH SHA256=f47ee00d5870f006ef95a7be307be6553a769428d48ee98f20e856b9c3dfd3a2
    #EMSCRIPTEN_PORT boost_headers
    LIST_SEPARATOR |
    PATCH_COMMAND ${PATCH_CMD} ${CMAKE_CURRENT_LIST_DIR}/locale.patch
    CMAKE_ARGS
        -DBOOST_EXCLUDE_LIBRARIES:STRING=${_excluded_libs}
        -DBOOST_LOCALE_ENABLE_ICU:BOOL=OFF # do not link to libicu, breaks compatibility between distros
        -DBUILD_TESTING:BOOL=OFF
        -DBOOST_IOSTREAMS_ENABLE_ZSTD:BOOL=OFF
        -DBOOST_USE_WINAPI_VERSION=0x601
        "-DCMAKE_CXX_FLAGS=${CMAKE_CXX_FLAGS}"
        "${_context_abi_line}"
        "${_context_arch_line}"
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5
)

set(DEP_Boost_DEPENDS ZLIB)
