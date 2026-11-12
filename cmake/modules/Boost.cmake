
add_definitions(-DBOOST_ALL_NO_LIB -DBOOST_USE_WINAPI_VERSION=0x601 -DBOOST_SYSTEM_USE_UTF8 )

# Find and configure boost
if(SLIC3R_STATIC)
    # Use static boost libraries.
    set(Boost_USE_STATIC_LIBS ON)
    # Use boost libraries linked statically to the C++ runtime.
    # set(Boost_USE_STATIC_RUNTIME ON)
endif()
#set(Boost_DEBUG ON)
# set(Boost_COMPILER "-mgw81")
# boost::process was introduced first in version 1.64.0,
# boost::beast::detail::base64 was introduced first in version 1.66.0
set(MINIMUM_BOOST_VERSION "1.86.0")
set(_boost_components "system;filesystem;thread;locale;regex;chrono;atomic;date_time;iostreams;nowide")
if(EMSCRIPTEN)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --use-port=sdl2 --use-port=zlib -fwasm-exceptions")
    #find_package(Boost ${MINIMUM_BOOST_VERSION} REQUIRED )
else()
    find_package(Boost ${MINIMUM_BOOST_VERSION} REQUIRED COMPONENTS ${_boost_components})
endif()

add_library(boost_libs INTERFACE)
add_library(boost_headeronly INTERFACE)

if (APPLE)
    # BOOST_ASIO_DISABLE_KQUEUE : prevents a Boost ASIO bug on OS X: https://svn.boost.org/trac/boost/ticket/5339
    target_compile_definitions(boost_headeronly INTERFACE BOOST_ASIO_DISABLE_KQUEUE)
endif()
if(NOT SLIC3R_STATIC)
    target_compile_definitions(boost_headeronly INTERFACE BOOST_LOG_DYN_LINK)
endif()

# BOOST_ALL_NO_LIB: Avoid the automatic linking of Boost libraries on Windows.
# Rather rely on explicit linking.
if (MSVC)
target_compile_definitions(boost_headeronly INTERFACE
    BOOST_ALL_NO_LIB
    BOOST_USE_WINAPI_VERSION=0x601
    BOOST_SYSTEM_USE_UTF8
)
endif()

target_link_libraries(boost_headeronly INTERFACE Boost::boost)

# Only from cmake 3.12
# list(TRANSFORM _boost_components PREPEND Boost:: OUTPUT_VARIABLE _boost_targets)
set(_boost_targets "")
foreach(comp ${_boost_components})
    list(APPEND _boost_targets "Boost::${comp}")
endforeach()

target_link_libraries(boost_libs INTERFACE
    boost_headeronly # includes the custom compile definitions as well
    ${_boost_targets}
    )
slic3r_remap_configs("${_boost_targets}" RelWithDebInfo Release)
