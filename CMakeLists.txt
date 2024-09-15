#/|/ Copyright (c) Prusa Research 2017 - 2023 Tomáš Mészáros @tamasmeszaros, Vojtěch Bubník @bubnikv, Lukáš Matěna @lukasmatena, Filip Sykala @Jony01, Oleksandra Iushchenko @YuSanka, Lukáš Hejl @hejllukas, David Kocík @kocikdav, Enrico Turri @enricoturri1966, Vojtěch Král @vojtechkral
#/|/ Copyright (c) 2023 Ben Greiner
#/|/ Copyright (c) 2021 D-mo @dimitry-ishenko
#/|/ Copyright (c) 2020 Pascal de Bruijn @pmjdebruijn
#/|/ Copyright (c) 2019 Sam Segers
#/|/ Copyright (c) 2019 Colin Gilgenbach @hexane360
#/|/ Copyright (c) 2018 Dan Kortschak
#/|/
#/|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
#/|/
cmake_minimum_required(VERSION 3.13)
project(PrusaSlicer)

include("version.inc")
include(GNUInstallDirs)
include(CMakeDependentOption)

set(SLIC3R_RESOURCES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/resources")
file(TO_NATIVE_PATH "${SLIC3R_RESOURCES_DIR}" SLIC3R_RESOURCES_DIR_WIN)

if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "No build type selected, default to Release")
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type (default Release)" FORCE)
endif()

if(DEFINED ENV{SLIC3R_STATIC})
        set(SLIC3R_STATIC_INITIAL $ENV{SLIC3R_STATIC})
else()
        if (MSVC OR MINGW OR APPLE)
                set(SLIC3R_STATIC_INITIAL 1)
        else()
                set(SLIC3R_STATIC_INITIAL 0)
        endif()
endif()

option(SLIC3R_STATIC 			"Compile PrusaSlicer with static libraries (Boost, TBB, glew)" ${SLIC3R_STATIC_INITIAL})
option(SLIC3R_GUI    			"Compile PrusaSlicer with GUI components (OpenGL, wxWidgets)" 1)
option(SLIC3R_FHS               "Assume PrusaSlicer is to be installed in a FHS directory structure" 0)
option(SLIC3R_PCH               "Use precompiled headers" 1)
option(SLIC3R_MSVC_COMPILE_PARALLEL "Compile on Visual Studio in parallel" 1)
option(SLIC3R_MSVC_PDB          "Generate PDB files on MSVC in Release mode" 1)
option(SLIC3R_ASAN              "Enable ASan on Clang and GCC" 0)
option(SLIC3R_UBSAN             "Enable UBSan on Clang and GCC" 0)
option(SLIC3R_ENABLE_FORMAT_STEP "Enable compilation of STEP file support" ON)
option(SLIC3R_LOG_TO_FILE       "Enable logging into file")
option(SLIC3R_REPO_URL          "Preset repo URL")

# SLIC3R_OPENGL_ES can be enabled only if SLIC3R_GUI is enabled.
CMAKE_DEPENDENT_OPTION(SLIC3R_OPENGL_ES "Compile PrusaSlicer targeting OpenGL ES" OFF "SLIC3R_GUI" OFF)

# If SLIC3R_FHS is 1 -> SLIC3R_DESKTOP_INTEGRATION is always 0, otherwise variable.
CMAKE_DEPENDENT_OPTION(SLIC3R_DESKTOP_INTEGRATION "Allow perfoming desktop integration during runtime" 1 "NOT SLIC3R_FHS" 0)

set(OPENVDB_FIND_MODULE_PATH "" CACHE PATH "Path to OpenVDB installation's find modules.")

set(SLIC3R_GTK "2" CACHE STRING "GTK version to use with wxWidgets on Linux")

set(IS_CROSS_COMPILE FALSE)

if (SLIC3R_STATIC)
    # Prefer config scripts over find modules. This is helpful when building with
    # the static dependencies. Many libraries have their own export scripts
    # while having a Find<PkgName> module in standard cmake installation.
    # (e.g. CURL)
    set(CMAKE_FIND_PACKAGE_PREFER_CONFIG ON)
endif ()

# Dependency build management
option(${PROJECT_NAME}_BUILD_DEPS "Build dependencies before the project" OFF)
option(${PROJECT_NAME}_DEPS_OUTPUT_QUIET "Don't print build output for dependencies" OFF)
set(${PROJECT_NAME}_DEPS_PRESET "default" CACHE STRING "Preset of the dependencies when ${PROJECT_NAME}_BUILD_DEPS is ON")
set(${PROJECT_NAME}_DEPS_BUILD_DIR "" CACHE PATH "Binary dir of the dependencies build when ${PROJECT_NAME}_BUILD_DEPS is ON")
if (${PROJECT_NAME}_BUILD_DEPS)
    include(deps/autobuild.cmake)
endif ()

if (APPLE)
    set(CMAKE_FIND_FRAMEWORK LAST)
    set(CMAKE_FIND_APPBUNDLE LAST)
    list(FIND CMAKE_OSX_ARCHITECTURES ${CMAKE_SYSTEM_PROCESSOR} _arch_idx)
    if (CMAKE_OSX_ARCHITECTURES AND _arch_idx LESS 0)
        set(IS_CROSS_COMPILE TRUE)
    endif ()
endif ()

option(SLIC3R_BUILD_SANDBOXES   "Build development sandboxes" OFF)
option(SLIC3R_BUILD_TESTS       "Build unit tests" ON)

if (IS_CROSS_COMPILE)
    message("Detected cross compilation setup. Tests and encoding checks will be forcedly disabled!")
    set(SLIC3R_BUILD_TESTS OFF CACHE BOOL "" FORCE)
endif ()

# Print out the SLIC3R_* cache options
get_cmake_property(_cache_vars CACHE_VARIABLES)
list (SORT _cache_vars)
foreach (_cache_var ${_cache_vars})
    if("${_cache_var}" MATCHES "^SLIC3R_")
        message(STATUS "${_cache_var}: ${${_cache_var}}")
    endif ()
endforeach()

if (SLIC3R_LOG_TO_FILE)
    add_definitions(-DSLIC3R_LOG_TO_FILE)
endif ()
if (SLIC3R_REPO_URL)
    add_definitions(-DSLIC3R_REPO_URL="${SLIC3R_REPO_URL}")
endif()
if (SLIC3R_GUI)
    add_definitions(-DSLIC3R_GUI)
endif ()

if (SLIC3R_OPENGL_ES)
    add_definitions(-DSLIC3R_OPENGL_ES)
endif()

if(SLIC3R_DESKTOP_INTEGRATION)
    add_definitions(-DSLIC3R_DESKTOP_INTEGRATION)
endif ()

if (MSVC AND CMAKE_CXX_COMPILER_ID STREQUAL Clang)
    set(IS_CLANG_CL TRUE)

    # clang-cl can interpret SYSTEM header paths if -imsvc is used
    set(CMAKE_INCLUDE_SYSTEM_FLAG_CXX "-imsvc")

    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall \
        -Wno-old-style-cast -Wno-reserved-id-macro -Wno-c++98-compat-pedantic")
else ()
    set(IS_CLANG_CL FALSE)
endif ()

if (MSVC)
    if (SLIC3R_MSVC_COMPILE_PARALLEL AND NOT IS_CLANG_CL)
           add_compile_options(/MP)
    endif ()
    # /bigobj (Increase Number of Sections in .Obj file)
    # error C3859: virtual memory range for PCH exceeded; please recompile with a command line option of '-Zm90' or greater
    # Generate symbols at every build target, even for the release.
    add_compile_options(-bigobj -Zm520 /Zi)
    # Disable STL4007: Many result_type typedefs and all argument_type, first_argument_type, and second_argument_type typedefs are deprecated in C++17.
    #FIXME Remove this line after eigen library adapts to the new C++17 adaptor rules.
    add_compile_options(-D_SILENCE_CXX17_ADAPTOR_TYPEDEFS_DEPRECATION_WARNING)
    # Disable warnings on conversion from unsigned to signed (possible loss of data)
    # C4244: 'conversion' conversion from 'type1' to 'type2', possible loss of data. An integer type is converted to a smaller integer type.
    # C4267: The compiler detected a conversion from size_t to a smaller type.
    add_compile_options(/wd4244 /wd4267)
    # Enforce strict C++ conformance, so our code that compiles on MSVC also compiles on GCC and clang.
    add_compile_options(/permissive-)
endif ()

if (MINGW)
    add_compile_options(-Wa,-mbig-obj)
endif ()

if (NOT MSVC)
    # ARMs (Raspberry PI) use an unsigned char by default. Let's make it consistent for PrusaSlicer on all platforms.
    add_compile_options(-fsigned-char)
endif ()

# Display and check CMAKE_PREFIX_PATH
message(STATUS "SLIC3R_STATIC: ${SLIC3R_STATIC}")
if (NOT "${CMAKE_PREFIX_PATH}" STREQUAL "")
    message(STATUS "CMAKE_PREFIX_PATH: ${CMAKE_PREFIX_PATH} (from cache or command line)")
    set(PREFIX_PATH_CHECK ${CMAKE_PREFIX_PATH})
elseif (NOT "$ENV{CMAKE_PREFIX_PATH}" STREQUAL "")
    message(STATUS "CMAKE_PREFIX_PATH: $ENV{CMAKE_PREFIX_PATH} (from environment)")
    set(PREFIX_PATH_CHECK $ENV{CMAKE_PREFIX_PATH})
else ()
    message(STATUS "CMAKE_PREFIX_PATH: (default)")
endif ()

foreach (DIR ${PREFIX_PATH_CHECK})
    if (NOT EXISTS "${DIR}")
        message(WARNING "CMAKE_PREFIX_PATH element doesn't exist: ${DIR}")
    endif ()
endforeach ()

# Add our own cmake module path.
list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake/modules/)

enable_testing ()

# Enable C++17 language standard.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT WIN32)
    # Add DEBUG flags to debug builds.
    add_compile_options("$<$<CONFIG:DEBUG>:-DDEBUG>")
endif()

# WIN10SDK_PATH is used to point CMake to the WIN10 SDK installation directory.
# We pick it from environment if it is not defined in another way
if(WIN32)
    if(NOT DEFINED WIN10SDK_PATH)
        if(DEFINED ENV{WIN10SDK_PATH})
                set(WIN10SDK_PATH "$ENV{WIN10SDK_PATH}")
        endif()
    endif()
    if(DEFINED WIN10SDK_PATH)
        if (EXISTS "${WIN10SDK_PATH}/include/winrt/windows.graphics.printing3d.h")
            set(WIN10SDK_INCLUDE_PATH "${WIN10SDK_PATH}/Include")
        else()
            message("WIN10SDK_PATH is invalid: ${WIN10SDK_PATH}")
            message("${WIN10SDK_PATH}/include/winrt/windows.graphics.printing3d.h was not found")
            message("STL fixing by WinSDK will not be compiled")
            unset(WIN10SDK_PATH)
        endif()
    else()
        # Try to use the default Windows 10 SDK path.
        set(WIN10SDK_INCLUDE_PATH "$ENV{WindowsSdkDir}/Include/$ENV{WindowsSDKVersion}")
        if (NOT EXISTS "${WIN10SDK_INCLUDE_PATH}/winrt/windows.graphics.printing3d.h")
            message("${WIN10SDK_INCLUDE_PATH}/winrt/windows.graphics.printing3d.h was not found")
            message("STL fixing by WinSDK will not be compiled")
            unset(WIN10SDK_INCLUDE_PATH)
        endif()
    endif()
    if(WIN10SDK_INCLUDE_PATH)
        message("Building with Win10 STL fixing service support")
        add_definitions(-DHAS_WIN10SDK)
        include_directories("${WIN10SDK_INCLUDE_PATH}")
    else()
        message("Building without Win10 STL fixing service support")
    endif()
endif()

if (APPLE)
    message("OS X SDK Path: ${CMAKE_OSX_SYSROOT}")
    if (CMAKE_OSX_DEPLOYMENT_TARGET)
        message("OS X Deployment Target: ${CMAKE_OSX_DEPLOYMENT_TARGET}")
    else ()
        message("OS X Deployment Target: (default)")
    endif ()
endif ()

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(PkgConfig REQUIRED)

    if (CMAKE_VERSION VERSION_LESS "3.1")
        # Workaround for an old CMake, which does not understand CMAKE_CXX_STANDARD.
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
    endif()

    # Boost on Raspberry-Pi does not link to pthreads.
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads REQUIRED)

    find_package(DBus REQUIRED)
endif()

if (CMAKE_COMPILER_IS_GNUCC OR CMAKE_COMPILER_IS_GNUXX)
    # Adding -fext-numeric-literals to enable GCC extensions on definitions of quad float literals, which are required by Boost.
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fext-numeric-literals" )
endif()

if (NOT MSVC AND ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" OR "${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang"))
    if (NOT MINGW)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall" )
    endif ()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-reorder" )

    # On GCC and Clang, no return from a non-void function is a warning only. Here, we make it an error.
    add_compile_options(-Werror=return-type)

    # removes LOTS of extraneous Eigen warnings (GCC only supports it since 6.1)
    # https://eigen.tuxfamily.org/bz/show_bug.cgi?id=1221
    if("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang" OR CMAKE_CXX_COMPILER_VERSION VERSION_GREATER 6.0)
        add_compile_options(-Wno-ignored-attributes) # Tamas: Eigen include dirs are marked as SYSTEM
    endif()

    # Clang reports legacy OpenGL calls as deprecated. Turn off the warning for now
    # to reduce the clutter, we know about this one. It should be reenabled after
    # we finally get rid of the deprecated code.
    if("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
        add_compile_options(-Wno-deprecated-declarations)
    endif()

    # Clang reports misleading indentation for some IF blocks because of mixing tabs with spaces.
    if("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
        add_compile_options(-Wno-misleading-indentation)
    endif()

    #GCC generates loads of -Wunknown-pragmas when compiling igl. The fix is not easy due to a bug in gcc, see
    # https://gcc.gnu.org/bugzilla/show_bug.cgi?id=66943 or
    # https://gcc.gnu.org/bugzilla/show_bug.cgi?id=53431
    # We will turn the warning of for GCC for now:
    if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
        add_compile_options(-Wno-unknown-pragmas)
    endif()

endif()

if (SLIC3R_ASAN)
    # ASAN should be available on MSVC starting with Visual Studio 2019 16.9
    # https://devblogs.microsoft.com/cppblog/address-sanitizer-for-msvc-now-generally-available/
    add_compile_options(-fsanitize=address)

    if (NOT MSVC)
        add_compile_options(-fno-omit-frame-pointer)
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")
        set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fsanitize=address")
        set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} -fsanitize=address")
    endif ()

    if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -lasan")
    endif ()
endif ()

if (SLIC3R_UBSAN)
    # Stacktrace for every report is enabled by default. It can be disabled by running PrusaSlicer with "UBSAN_OPTIONS=print_stacktrace=0".

    # Define macro SLIC3R_UBSAN to allow detection in the source code if this sanitizer is enabled.
    add_compile_definitions(SLIC3R_UBSAN)

    # Clang supports much more useful checks than GCC, so when Clang is detected, another checks will be enabled.
    # List of what GCC is checking: https://gcc.gnu.org/onlinedocs/gcc/Instrumentation-Options.html
    # List of what Clang is checking: https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html
    if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
        set(_ubsan_flags "-fsanitize=undefined,integer")
    else ()
        set(_ubsan_flags "-fsanitize=undefined")
    endif ()

    add_compile_options(${_ubsan_flags} -fno-omit-frame-pointer)

    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${_ubsan_flags}")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${_ubsan_flags}")
    set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} ${_ubsan_flags}")

    if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -lubsan")
    endif ()
endif ()

if (APPLE)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Werror=partial-availability -Werror=unguarded-availability -Werror=unguarded-availability-new")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Werror=partial-availability -Werror=unguarded-availability -Werror=unguarded-availability-new")
endif ()

set(LIBDIR_BIN ${CMAKE_CURRENT_BINARY_DIR}/src)
# For generated header files
include_directories(${LIBDIR_BIN}/platform)

if(WIN32)
    add_definitions(-D_USE_MATH_DEFINES -D_WIN32 -D_CRT_SECURE_NO_WARNINGS -D_SCL_SECURE_NO_WARNINGS)
    if(MSVC)
        # BOOST_ALL_NO_LIB: Avoid the automatic linking of Boost libraries on Windows. Rather rely on explicit linking.
        add_definitions(-DBOOST_ALL_NO_LIB -DBOOST_USE_WINAPI_VERSION=0x601 -DBOOST_SYSTEM_USE_UTF8 )
        # Force the source code encoding to UTF-8. See PrusaSlicer GH pull request #5583
        add_compile_options("$<$<C_COMPILER_ID:MSVC>:/utf-8>")
        add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
    endif(MSVC)
endif(WIN32)

add_definitions(-DwxUSE_UNICODE -D_UNICODE -DUNICODE -DWXINTL_NO_GETTEXT_MACRO)

# Disable unsafe implicit wxString to const char* / std::string and vice versa. This implicit conversion breaks the UTF-8 encoding quite often.
add_definitions(-DwxNO_UNSAFE_WXSTRING_CONV)

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
set(MINIMUM_BOOST_VERSION "1.66.0")
set(_boost_components "system;filesystem;thread;log;locale;regex;chrono;atomic;date_time;iostreams;nowide")
find_package(Boost ${MINIMUM_BOOST_VERSION} REQUIRED COMPONENTS ${_boost_components})

find_package(Eigen3 3.3.7 REQUIRED)

add_library(boost_libs INTERFACE)
add_library(boost_headeronly INTERFACE)

if (APPLE)
    # BOOST_ASIO_DISABLE_KQUEUE : prevents a Boost ASIO bug on OS X: https://svn.boost.org/trac/boost/ticket/5339
    target_compile_definitions(boost_headeronly INTERFACE BOOST_ASIO_DISABLE_KQUEUE)
endif()

if(NOT SLIC3R_STATIC)
    target_compile_definitions(boost_headeronly INTERFACE BOOST_LOG_DYN_LINK)
endif()

function(slic3r_remap_configs targets from_Cfg to_Cfg)
    if(MSVC)
        string(TOUPPER ${from_Cfg} from_CFG)
    
        foreach(tgt ${targets})
            if(TARGET ${tgt})
                set_target_properties(${tgt} PROPERTIES MAP_IMPORTED_CONFIG_${from_CFG} ${to_Cfg})
            endif()
        endforeach()
    endif()
endfunction()

if(TARGET Boost::system)
    message(STATUS "Boost::boost exists")
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
else()
    target_include_directories(boost_headeronly INTERFACE ${Boost_INCLUDE_DIRS})
    target_link_libraries(boost_libs INTERFACE boost_headeronly ${Boost_LIBRARIES})
endif()



# Find and configure intel-tbb
if(SLIC3R_STATIC)
    set(TBB_STATIC 1)
endif()
set(TBB_DEBUG 1)
find_package(TBB REQUIRED)
slic3r_remap_configs(TBB::tbb RelWithDebInfo Release)
slic3r_remap_configs(TBB::tbbmalloc RelWithDebInfo Release)
# include_directories(${TBB_INCLUDE_DIRS})
# add_definitions(${TBB_DEFINITIONS})
# if(MSVC)
#     # Suppress implicit linking of the TBB libraries by the Visual Studio compiler.
#     add_definitions(-D__TBB_NO_IMPLICIT_LINKAGE)
# endif()
# The Intel TBB library will use the std::exception_ptr feature of C++11.
# add_definitions(-DTBB_USE_CAPTURED_EXCEPTION=0)

find_package(CURL REQUIRED)

add_library(libcurl INTERFACE)
target_link_libraries(libcurl INTERFACE CURL::libcurl)

# Fixing curl's cmake config script bugs
if (NOT WIN32)
    # Required by libcurl
    find_package(ZLIB REQUIRED)
    target_link_libraries(libcurl INTERFACE ZLIB::ZLIB)
else()
    target_link_libraries(libcurl INTERFACE crypt32)
endif()

## OPTIONAL packages

# Find expat. We have our overriden FindEXPAT which exports libexpat target
# no matter what.
find_package(EXPAT REQUIRED)

add_library(libexpat INTERFACE)

if (TARGET EXPAT::EXPAT ) # found by a newer Find script
    target_link_libraries(libexpat INTERFACE EXPAT::EXPAT)
elseif(TARGET expat::expat) # found by a config script
    target_link_libraries(libexpat INTERFACE expat::expat)
else() # found by an older Find script
    target_link_libraries(libexpat INTERFACE ${EXPAT_LIBRARIES})
endif ()

find_package(PNG REQUIRED)

set(OpenGL_GL_PREFERENCE "LEGACY")
find_package(OpenGL REQUIRED)

# Find glew or use bundled version
if (SLIC3R_STATIC AND NOT SLIC3R_STATIC_EXCLUDE_GLEW)
    set(GLEW_USE_STATIC_LIBS ON)
    set(GLEW_VERBOSE ON)
endif()

find_package(GLEW REQUIRED)

# Find the Cereal serialization library
find_package(cereal REQUIRED)
add_library(libcereal INTERFACE)
if (NOT TARGET cereal::cereal)
    target_link_libraries(libcereal INTERFACE cereal)
else()
    target_link_libraries(libcereal INTERFACE cereal::cereal)
endif()

# l10n
set(L10N_DIR "${SLIC3R_RESOURCES_DIR}/localization")
add_custom_target(gettext_make_pot
    COMMAND xgettext --keyword=L --keyword=_L --keyword=_u8L --keyword=L_CONTEXT:1,2c --keyword=_L_PLURAL:1,2 --add-comments=TRN --from-code=UTF-8 --debug --boost
        -f "${L10N_DIR}/list.txt"
        -o "${L10N_DIR}/PrusaSlicer.pot"
    COMMAND hintsToPot ${SLIC3R_RESOURCES_DIR} ${L10N_DIR}
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    COMMENT "Generate pot file from strings in the source tree"
)

add_custom_target(gettext_merge_community_po_with_pot
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    COMMENT "Merge community po with new generated pot file"
)
file(GLOB L10N_PO_FILES "${L10N_DIR}/*/PrusaSlicer*.po")
# list of names of directories, which are licalized by PS internally
list(APPEND PS_L10N_DIRS "cs" "de" "es" "fr" "it" "ja" "pl")
foreach(po_file ${L10N_PO_FILES})
    GET_FILENAME_COMPONENT(po_dir "${po_file}" DIRECTORY)
    GET_FILENAME_COMPONENT(po_dir_name "${po_dir}" NAME)
    list(FIND PS_L10N_DIRS ${po_dir_name} found_dir_id)
    # found_dir_id==-1 means that po_dir_name wasn't found in PS_L10N_DIRS
    if(found_dir_id LESS 0)
        add_custom_command(
            TARGET gettext_merge_community_po_with_pot PRE_BUILD
            COMMAND msgmerge -N -o ${po_file} ${po_file} "${L10N_DIR}/PrusaSlicer.pot"
            # delete obsolete lines from resulting PO to avoid conflicts after a merging of it with wxWidgets.po
            COMMAND msgattrib --no-obsolete -o ${po_file} ${po_file}
            DEPENDS ${po_file}
        )
    endif()
endforeach()

add_custom_target(gettext_concat_wx_po_with_po
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    COMMENT "Concatenate and merge wxWidgets localization po with PrusaSlicer po file"
)
file(GLOB L10N_PO_FILES "${L10N_DIR}/*/PrusaSlicer*.po")
foreach(po_file ${L10N_PO_FILES})
    GET_FILENAME_COMPONENT(po_dir "${po_file}" DIRECTORY)
    GET_FILENAME_COMPONENT(po_dir_name "${po_dir}" NAME)
    SET(wx_po_file "${L10N_DIR}/wx_locale/${po_dir_name}.po")
    #SET(po_new_file "${po_dir}/PrusaSlicer_.po")
    add_custom_command(
        TARGET gettext_concat_wx_po_with_po PRE_BUILD
        COMMAND msgcat --use-first -o ${po_file} ${po_file} ${wx_po_file}
        # delete obsolete lines from resulting PO
        COMMAND msgattrib --no-obsolete -o ${po_file} ${po_file}
        DEPENDS ${po_file}
    )
endforeach()

add_custom_target(gettext_po_to_mo
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    COMMENT "Generate localization mo files (binary) from po files (texts)"
)
file(GLOB L10N_PO_FILES "${L10N_DIR}/*/PrusaSlicer*.po")
foreach(po_file ${L10N_PO_FILES})
    GET_FILENAME_COMPONENT(po_dir "${po_file}" DIRECTORY)
    SET(mo_file "${po_dir}/PrusaSlicer.mo")
    add_custom_command(
        TARGET gettext_po_to_mo PRE_BUILD
        COMMAND msgfmt ARGS --check-format -o ${mo_file} ${po_file}
        #COMMAND msgfmt ARGS --check-compatibility -o ${mo_file} ${po_file}
        DEPENDS ${po_file}
    )
endforeach()

find_package(NLopt 1.4 REQUIRED)
slic3r_remap_configs(NLopt::nlopt RelWithDebInfo Release)

if(SLIC3R_STATIC)
    set(OPENVDB_USE_STATIC_LIBS ON)
    set(USE_BLOSC TRUE)
endif ()

find_package(OpenVDB 5.0 COMPONENTS openvdb)
if(OpenVDB_FOUND)
    slic3r_remap_configs(IlmBase::Half RelWithDebInfo Release)
    slic3r_remap_configs(Blosc::blosc RelWithDebInfo Release)
else ()
    message(FATAL_ERROR "OpenVDB could not be found with the bundled find module. "
                   "You can try to specify the find module location of your "
                   "OpenVDB installation with the OPENVDB_FIND_MODULE_PATH cache variable.")
endif ()

set(TOP_LEVEL_PROJECT_DIR ${PROJECT_SOURCE_DIR})
function(prusaslicer_copy_dlls target)
    if ("${CMAKE_SIZEOF_VOID_P}" STREQUAL "8")
        set(_bits 64)
    elseif ("${CMAKE_SIZEOF_VOID_P}" STREQUAL "4")
        set(_bits 32)
    endif ()
    
    get_property(_is_multi GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
    get_target_property(_alt_out_dir ${target} RUNTIME_OUTPUT_DIRECTORY)

    if (_alt_out_dir)
        set (_out_dir "${_alt_out_dir}")
    elseif (_is_multi)
        set (_out_dir "$<TARGET_PROPERTY:${target},BINARY_DIR>/$<CONFIG>")
    else ()
        set (_out_dir "$<TARGET_PROPERTY:${target},BINARY_DIR>")
    endif ()
    
    # This has to be a separate target due to the windows command line lenght limits    
    add_custom_command(TARGET ${target} POST_BUILD 
        COMMAND ${CMAKE_COMMAND} -E copy ${TOP_LEVEL_PROJECT_DIR}/deps/+GMP/gmp/lib/win${_bits}/libgmp-10.dll ${_out_dir}
        COMMENT "Copy gmp runtime to build tree"
        VERBATIM)
    
    add_custom_command(TARGET ${target} POST_BUILD 
        COMMAND ${CMAKE_COMMAND} -E copy ${TOP_LEVEL_PROJECT_DIR}/deps/+MPFR/mpfr/lib/win${_bits}/libmpfr-4.dll ${_out_dir}
        COMMENT "Copy mpfr runtime to build tree"
        VERBATIM)
endfunction()

add_subdirectory(build-utils)
add_subdirectory(bundled_deps)
# libslic3r, PrusaSlicer GUI and the PrusaSlicer executable.
add_subdirectory(src)
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT PrusaSlicer_app_console)

add_dependencies(gettext_make_pot hintsToPot)

if(SLIC3R_BUILD_SANDBOXES)
    add_subdirectory(sandboxes)
endif()

if(SLIC3R_BUILD_TESTS)
    add_subdirectory(tests)
endif()


# Resources install target, configure fhs.hpp on UNIX
if (WIN32)
    install(DIRECTORY "${SLIC3R_RESOURCES_DIR}/" DESTINATION "${CMAKE_INSTALL_PREFIX}/resources")
elseif (SLIC3R_FHS)
    # CMAKE_INSTALL_FULL_DATAROOTDIR: read-only architecture-independent data root (share)
    set(SLIC3R_FHS_RESOURCES "${CMAKE_INSTALL_FULL_DATAROOTDIR}/PrusaSlicer")
    install(DIRECTORY ${SLIC3R_RESOURCES_DIR}/ DESTINATION ${SLIC3R_FHS_RESOURCES}
        PATTERN "*/udev" EXCLUDE
    )
    install(FILES src/platform/unix/PrusaSlicer.desktop DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications)
    install(FILES src/platform/unix/PrusaGcodeviewer.desktop DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications)
    foreach(SIZE 32 128 192)
        install(FILES ${SLIC3R_RESOURCES_DIR}/icons/PrusaSlicer_${SIZE}px.png
            DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/${SIZE}x${SIZE}/apps RENAME PrusaSlicer.png
        )
        install(FILES ${SLIC3R_RESOURCES_DIR}/icons/PrusaSlicer-gcodeviewer_${SIZE}px.png
            DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/${SIZE}x${SIZE}/apps RENAME PrusaSlicer-gcodeviewer.png
        )
    endforeach()
    install(DIRECTORY ${SLIC3R_RESOURCES_DIR}/udev/ DESTINATION lib/udev/rules.d)
else ()
    install(FILES src/platform/unix/PrusaSlicer.desktop DESTINATION ${CMAKE_INSTALL_PREFIX}/resources/applications)
    install(FILES src/platform/unix/PrusaGcodeviewer.desktop DESTINATION ${CMAKE_INSTALL_PREFIX}/resources/applications)
    install(DIRECTORY "${SLIC3R_RESOURCES_DIR}/" DESTINATION "${CMAKE_INSTALL_PREFIX}/resources")
endif ()

configure_file(src/platform/unix/fhs.hpp.in ${LIBDIR_BIN}/platform/unix/fhs.hpp)
