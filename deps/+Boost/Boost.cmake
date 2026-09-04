
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
    URL "https://github.com/boostorg/boost/releases/download/boost-1.86.0/boost-1.86.0-cmake.zip"
    URL_HASH SHA256=a66084ec52c9dfa838b3b225a29f29869b2d11e201b3f9e4b989431d95b671c2
    #EMSCRIPTEN_PORT boost_headers
    LIST_SEPARATOR |
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
