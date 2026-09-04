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

option(SLIC3R_STATIC "Compile PrusaSlicer with static libraries (Boost, TBB, glew)" ON)
option(SLIC3R_FHS "Assume PrusaSlicer is to be installed in a FHS directory structure" OFF)

if (DEFINED SLIC3R_PCH AND SLIC3R_PCH AND MSVC)
    message(FATAL_ERROR "SLIC3R_PCH was explicitly requested, but they are currently not "
        "supported on MSVC. Two issues to solve: we must require at least CMake 4.2.1, otherwise "
        "we get a C2859 build failure, and we must turn on IPO for slic3r-shared (currently turned off "
        "because we are hitting a 4GB limit with debug symbols - it requires splitting the library)")
endif ()
option(SLIC3R_PCH "Use precompiled headers" ON)
if (SLIC3R_PCH AND MSVC)
    message(STATUS "Disabling SLIC3R_PCH: Currently not supported on MSVC")
    set(SLIC3R_PCH OFF)
endif ()
option(SLIC3R_ASAN "Enable ASan on Clang and GCC" 0)
option(SLIC3R_UBSAN "Enable UBSan on Clang and GCC" 0)
option(SLIC3R_REPO_URL "Preset repo URL")
option(SLIC3R_RELEASE_DEBUG_SYMBOLS "Build release build with debug symbols" ON)
option(SLIC3R_ENABLE_PROFILING "Enable Tracy profiling instrumentation" OFF)

if (WIN32)
    set(SLIC3R_ENABLE_WIN10_MESH_REPAIR_DEFAULT ON)
else()
    set(SLIC3R_ENABLE_WIN10_MESH_REPAIR_DEFAULT OFF)
endif()
option(SLIC3R_ENABLE_WIN10_MESH_REPAIR "Enable mesh repair via the Windows WinRT Printing3D API" ${SLIC3R_ENABLE_WIN10_MESH_REPAIR_DEFAULT})
if (SLIC3R_ENABLE_WIN10_MESH_REPAIR AND NOT WIN32)
    message(FATAL_ERROR "SLIC3R_ENABLE_WIN10_MESH_REPAIR is Windows-only option, but the current platform is not Windows")
endif()

if (NOT EMSCRIPTEN)
    option(SLIC3R_ENABLE_FORMAT_STEP "Support STEP loading (requires OpenCASCADE)" ON)
    option(SLIC3R_BUILD_TESTS "Build unit tests" ON)
    option(SLIC3R_GUI "Compile PrusaSlicer with GUI components (OpenGL, wxWidgets)" ON)
    option(SLIC3R_OPENGL_ES "Use OpenGL ES" OFF)
    if (SLIC3R_GUI)
        option(SLIC3R_ENABLE_WEBKIT "Enables linking against WebKitGTK on Linux, Webkit on MacOs or WebView2 on Windows" ON)
    else()
        set(SLIC3R_ENABLE_WEBKIT OFF)
    endif()
    set(SLIC3R_SENTRY_DSN "" CACHE STRING "Sentry DSN (leave empty to disable)")
    if (SLIC3R_SENTRY_DSN)
        message(STATUS "Enabling sentry")
    else ()
        message(STATUS "Disabling sentry")
    endif ()
else ()
    # These options are fixed for Emscripten and not user-configurable.
    set(SLIC3R_ENABLE_FORMAT_STEP OFF)
    set(SLIC3R_BUILD_TESTS OFF)
    set(SLIC3R_GUI ON)
    set(SLIC3R_OPENGL_ES ON)
    set(SLIC3R_ENABLE_WEBKIT OFF)
    set(SLIC3R_SENTRY_DSN "")
    message(STATUS "Disabling sentry")
endif ()

set(SLIC3R_YAML ryml CACHE STRING "YAML library to use (allowed: libfyaml, yaml-cpp, ryml)")
option(SLIC3R_DEBUG_PRESET_CACHE "In Debug build cache loading of preset bundle" ON)

set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake/modules/)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "No build type selected, default to Release")
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type (default Release)" FORCE)
endif()
if (APPLE AND CMAKE_BUILD_TYPE STREQUAL "Release" AND SLIC3R_RELEASE_DEBUG_SYMBOLS)
    set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} -g)
endif ()
if (SLIC3R_STATIC)
    # Prefer config scripts over find modules. This is helpful when building with
    # the static dependencies. Many libraries have their own export scripts
    # while having a Find<PkgName> module in standard cmake installation.
    # (e.g. CURL)
    set(CMAKE_FIND_PACKAGE_PREFER_CONFIG ON)
endif ()
set(SLIC3R_RESOURCES_DIR ${CMAKE_SOURCE_DIR}/resources)

include("version.inc")
include(GNUInstallDirs)
include(CMakeDependentOption)
include(Utils)

if (UNIX AND NOT APPLE)
    configure_file(
        ${CMAKE_SOURCE_DIR}/src/platform/unix/com.prusa3d.PrusaSlicer.desktop.in
        ${CMAKE_BINARY_DIR}/com.prusa3d.PrusaSlicer.desktop
        @ONLY
    )
endif ()


set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if (APPLE)
    set(CMAKE_FIND_FRAMEWORK LAST)
    set(CMAKE_FIND_APPBUNDLE LAST)
    list(FIND CMAKE_OSX_ARCHITECTURES ${CMAKE_SYSTEM_PROCESSOR} _arch_idx)
    if (CMAKE_OSX_ARCHITECTURES AND _arch_idx LESS 0)
        message("Detected cross compilation setup. Tests and encoding checks are forcedly disabled!")
        set(SLIC3R_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    endif ()
    add_compile_options(-Werror=partial-availability -Werror=unguarded-availability -Werror=unguarded-availability-new)
endif ()
include(${PROJECT_SOURCE_DIR}/cmake/modules/OSXDeploymentTarget.cmake)
if(WIN32)
    add_compile_definitions(
        _USE_MATH_DEFINES
        _WIN32
        _CRT_SECURE_NO_WARNINGS
        _SCL_SECURE_NO_WARNINGS
    )
endif()

if (SLIC3R_ENABLE_WIN10_MESH_REPAIR)
    include(WinSDK)
    if (NOT WIN10SDK_INCLUDE_PATH)
        message(FATAL_ERROR
            "SLIC3R_ENABLE_WIN10_MESH_REPAIR is ON, but the Windows WinRT headers "
            "(winrt/windows.graphics.printing3d.h) could not be found.\n"
            "To point CMake at a specific SDK root manually, set "
            "-DWIN10SDK_PATH=<path> when running CMake."
        )
    endif()
    message(STATUS "Win10 mesh repair: enabled, using WinRT headers from ${WIN10SDK_INCLUDE_PATH}")
else()
    message(STATUS "Win10 mesh repair: disabled")
endif()

add_compile_definitions(
    wxUSE_UNICODE
    _UNICODE
    UNICODE
    WXINTL_NO_GETTEXT_MACRO

    # Disable unsafe implicit wxString to const char* / std::string and vice versa. This implicit conversion breaks the UTF-8 encoding quite often.
    wxNO_UNSAFE_WXSTRING_CONV
)

if (MSVC)
    # TEMPORARY!
    add_compile_options(/wd4996)  # Disable deprecation warnings

    add_compile_options(/MP)
    # /bigobj (Increase Number of Sections in .Obj file)
    # error C3859: virtual memory range for PCH exceeded; please recompile with a command line option of '-Zm90' or greater
    add_compile_options(-bigobj -Zm520)
    if (SLIC3R_RELEASE_DEBUG_SYMBOLS)
        # Generate pdbs.
        add_compile_options("$<$<CONFIG:RELEASE>:/Zi>")
    endif()
    # Disable STL4007: Many result_type typedefs and all argument_type, first_argument_type, and second_argument_type typedefs are deprecated in C++17.
    #FIXME Remove this line after eigen library adapts to the new C++17 adaptor rules.
    add_compile_options(-D_SILENCE_CXX17_ADAPTOR_TYPEDEFS_DEPRECATION_WARNING)
    # Disable warnings on conversion from unsigned to signed (possible loss of data)
    # C4244: 'conversion' conversion from 'type1' to 'type2', possible loss of data. An integer type is converted to a smaller integer type.
    # C4267: The compiler detected a conversion from size_t to a smaller type.
    add_compile_options(/wd4244 /wd4267)
    # Enforce strict C++ conformance, so our code that compiles on MSVC also compiles on GCC and clang.
    add_compile_options(/permissive-)

    # Force the source code encoding to UTF-8. See PrusaSlicer GH pull request #5583
    add_compile_options("$<$<C_COMPILER_ID:MSVC>:/utf-8>")
    add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
endif ()

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    add_compile_options(
        # Nikita 31.7.2025: I'm turning this off, the compilation works fine and this option has made clang-tidy unusable, because
        # compile_commands.json contained this option which is not recognized by Clang
        # Previous Text:
        # Adding -fext-numeric-literals to enable GCC extensions on definitions of quad float literals, which are required by Boost.
        # -fext-numeric-literals

        #GCC generates loads of -Wunknown-pragmas when compiling igl. The fix is not easy due to a bug in gcc, see
        # https://gcc.gnu.org/bugzilla/show_bug.cgi?id=66943 or
        # https://gcc.gnu.org/bugzilla/show_bug.cgi?id=53431
        # We will turn the warning of for GCC for now:
        -Wno-unknown-pragmas

        # GCC 13+ warns when a type is used in a SFINAE/type-trait context (e.g. std::is_assignable,
        # pulled in transitively by <algorithm>/<type_traits>) while only forward-declared, and is
        # later fully defined in the same translation unit. This codebase relies heavily on
        # forward-declaring UI-owner types across many headers for compile-time firewalling.
        # None of these types are ever used by value in a container/trait that would make the
        # incomplete-type SFINAE result observable, so this is noise, not a bug.
        -Wno-sfinae-incomplete
    )
endif()

# check if our compiler supports -fno-link-libatomic
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("-fno-link-libatomic" CXX_SUPPORTS_FNO_LINK_LIBATOMIC)
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CXX_SUPPORTS_FNO_LINK_LIBATOMIC)
    # do not auto-append -latomic_asneeded (which usually comes from webkitgtk)
    # we are already linking atomic dynamically so it would result in static + dynamic link
    add_link_options(-fno-link-libatomic)
endif()

if("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
    add_compile_options(
        # Clang reports misleading indentation for some IF blocks because of mixing tabs with spaces.
        -Wno-misleading-indentation
    )
endif()

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" OR "${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")

    add_compile_options(
        # Add DEBUG flags to debug builds.
        "$<$<CONFIG:DEBUG>:-DDEBUG>"
        #TEMPORARY
        -Wno-deprecated-declarations # Disable deprecation warnings

        $<$<COMPILE_LANGUAGE:CXX>:-Wno-reorder>

        # On GCC and Clang, no return from a non-void function is a warning only. Here, we make it an error.
        -Werror=return-type

        # removes LOTS of extraneous Eigen warnings (GCC only supports it since 6.1)
        # https://eigen.tuxfamily.org/bz/show_bug.cgi?id=1221
        -Wno-ignored-attributes

        # ARMs (Raspberry PI) use an unsigned char by default. Let's make it consistent for PrusaSlicer on all platforms.
        -fsigned-char
    )

    if (NOT MINGW)
        add_compile_options(-Wall)
    else()
        add_compile_options(-Wa,-mbig-obj)
    endif ()
endif()

if (EMSCRIPTEN)
    set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} -sUSE_PTHREADS)
    set(CMAKE_EXE_LINKER_FLAGS ${CMAKE_EXE_LINKER_FLAGS} -sUSE_PTHREADS)
    set(CMAKE_SHARED_LINKER_FLAGS ${CMAKE_SHARED_LINKER_FLAGS} -sUSE_PTHREADS)

#    set(CMAKE_CXX_FLAGS "--use-port=libpng --use-port=libjpeg ${CMAKE_CXX_FLAGS}")
#    set(CMAKE_C_FLAGS "--use-port=libpng --use-port=libjpeg ${CMAKE_CXX_FLAGS}")
    set(CMAKE_CXX_FLAGS "--use-port=libpng -pthread ${CMAKE_CXX_FLAGS}")
    set(CMAKE_C_FLAGS "--use-port=libpng -pthread ${CMAKE_CXX_FLAGS}")

    set(SIMPLES_MAIN_SOURCE
            "int main() { return 0; }\n\n"
    )
    try_compile(_res SOURCE_FROM_VAR main.cpp SIMPLEST_MAIN_SOURCE)
    if (NOT _res)
        message(ERROR "Prefetching Emscripten ports of libpng and libjpeg fails")
    endif ()
endif()

enable_testing()

include(DoxygenTarget)
include(LuaDocsTarget)
include(Sanitizers)

if (NOT SLIC3R_ASAN AND NOT SLIC3R_UBSAN)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT _ipo_supported OUTPUT _ipo_output LANGUAGES CXX C)
    if (_ipo_supported)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
        message(STATUS "LTO/IPO enabled for Release builds")
    else()
        message(STATUS "LTO/IPO not supported by this toolchain: ${_ipo_output}")
    endif()
endif()

include(Localization)
include(GlobalDependencies)

include(CheckAndPrintSummary)

add_subdirectory(bundled_deps)
add_subdirectory(src)

if(SLIC3R_BUILD_TESTS)
    add_subdirectory(tests)
endif()
