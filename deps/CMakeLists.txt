#/|/ Copyright (c) Prusa Research 2018 - 2022 Lukáš Matěna @lukasmatena, Tomáš Mészáros @tamasmeszaros, Filip Sykala @Jony01, Vojtěch Bubník @bubnikv, Vojtěch Král @vojtechkral
#/|/
#/|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
#/|/
#
# This CMake project downloads, configures and builds PrusaSlicer dependencies on Unix and Windows.
#
# When using this script, it's recommended to perform an out-of-source build using CMake.
#
# All the dependencies are installed in a `destdir` directory in the root of the build directory,
# in a traditional Unix-style prefix structure. The destdir can be used directly by CMake
# when building PrusaSlicer - to do this, set the CMAKE_PREFIX_PATH to ${destdir}/usr/local.
# Warning: On UNIX/Linux, you also need to set -DSLIC3R_STATIC=1 when building PrusaSlicer.
#
# For better clarity of console output, it's recommended to _not_ use a parallelized build
# for the top-level command, ie. use `make -j 1` or `ninja -j 1` to force single-threaded top-level
# build. This doesn't degrade performance as individual dependencies are built in parallel fashion
# if supported by the dependency.
#
# On Windows, architecture (64 vs 32 bits) is judged based on the compiler variant.
# To build dependencies for either 64 or 32 bit OS, use the respective compiler command line.
#
# WARNING: On UNIX platforms wxWidgets hardcode the destdir path into its `wx-conffig` utility,
# therefore, unfortunatelly, the installation cannot be copied/moved elsewhere without re-installing wxWidgets.
#
cmake_minimum_required(VERSION 3.12)
project(PrusaSlicer_deps)

# Redefine BUILD_SHARED_LIBS with default being OFF
option(BUILD_SHARED_LIBS "Build shared libraries instead of static (experimental)" OFF)

# List libraries to be excluded from build
set(${PROJECT_NAME}_PACKAGE_EXCLUDES "" CACHE STRING "Exclude packages matching this regex pattern")

# Support legacy parameter DESTDIR
if (DESTDIR)
  set(${PROJECT_NAME}_DEP_INSTALL_PREFIX ${DESTDIR}/usr/local CACHE PATH "Destination directory" FORCE)
endif ()

# Support legacy parameter DEP_DOWNLOAD_DIR
if (DEP_DOWNLOAD_DIR)
  set(${PROJECT_NAME}_DEP_DOWNLOAD_DIR ${DEP_DOWNLOAD_DIR} CACHE PATH "Path for downloaded source packages." FORCE)
endif ()

# Slightly controversial
set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/../cmake/modules)

if (MSVC)
  option(DEP_DEBUG "Build in debug version of packages automatically" ON)
endif ()

if (CMAKE_VERSION VERSION_GREATER_EQUAL 3.24)
    cmake_policy(SET CMP0135 NEW)
endif ()

include(${PROJECT_SOURCE_DIR}/../cmake/modules/AddCMakeProject.cmake)

macro(list_projects result curdir)
  file(GLOB children RELATIVE ${curdir} ${curdir}/*)
  set(dirlist "")
  foreach(child ${children})
    if(IS_DIRECTORY ${curdir}/${child})
      string(REGEX MATCH "^\\+([a-zA-Z0-9_]+)" is_package_dir ${child})
      if(is_package_dir AND EXISTS ${curdir}/${child}/${CMAKE_MATCH_1}.cmake) 
        list(APPEND dirlist ${CMAKE_MATCH_1})
      endif()
    endif()
  endforeach()
  set(${result} ${dirlist})
endmacro()

function(dep_message mode msg)
  if (NOT DEP_MESSAGES_WRITTEN)
    message(${mode} "${msg}")
  endif()
endfunction ()

# Always ON options:
if (MSVC)
  if ("${CMAKE_SIZEOF_VOID_P}" STREQUAL "8")
    dep_message(STATUS "Detected 64-bit compiler => building 64-bit deps bundle")
    set(DEPS_BITS 64)
  elseif ("${CMAKE_SIZEOF_VOID_P}" STREQUAL "4")
    dep_message(STATUS "Detected 32-bit compiler => building 32-bit deps bundle")
    set(DEPS_BITS 32)
  else ()
    dep_message(FATAL_ERROR "Unable to detect architecture!")
  endif ()
else ()
    set(DEP_CMAKE_OPTS "-DCMAKE_POSITION_INDEPENDENT_CODE=ON")
endif ()

if (APPLE)
    if (CMAKE_OSX_DEPLOYMENT_TARGET)
        set(DEP_OSX_TARGET "${CMAKE_OSX_DEPLOYMENT_TARGET}")
        dep_message(STATUS "OS X Deployment Target: ${DEP_OSX_TARGET}")
    else ()
        # Attempt to infer the SDK version from the CMAKE_OSX_SYSROOT,
        # this is done because wxWidgets need the min version explicitly set
        string(REGEX MATCH "[0-9]+[.][0-9]+[.]sdk$" DEP_OSX_TARGET "${CMAKE_OSX_SYSROOT}")
        string(REGEX MATCH "^[0-9]+[.][0-9]+" DEP_OSX_TARGET "${DEP_OSX_TARGET}")

        if (NOT DEP_OSX_TARGET)
            message(FATAL_ERROR "Could not determine OS X SDK version. Please use -DCMAKE_OSX_DEPLOYMENT_TARGET=<version>")
        endif ()

        dep_message(STATUS "OS X Deployment Target (inferred from SDK): ${DEP_OSX_TARGET}")
    endif ()

    # This ensures dependencies don't use SDK features which are not available in the version specified by Deployment target
    # That can happen when one uses a recent SDK but specifies an older Deployment target
    set(DEP_WERRORS_SDK "-Werror=partial-availability -Werror=unguarded-availability -Werror=unguarded-availability-new")
    
    set(DEP_CMAKE_OPTS
        "-DCMAKE_POSITION_INDEPENDENT_CODE=ON"
        "-DCMAKE_OSX_SYSROOT=${CMAKE_OSX_SYSROOT}"
        "-DCMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}"
        "-DCMAKE_OSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES}"
        "-DCMAKE_CXX_FLAGS=${DEP_WERRORS_SDK}"
        "-DCMAKE_C_FLAGS=${DEP_WERRORS_SDK}"
        "-DCMAKE_FIND_FRAMEWORK=LAST"
        "-DCMAKE_FIND_APPBUNDLE=LAST"
    )
endif ()

list_projects(FOUND_PACKAGES ${CMAKE_CURRENT_LIST_DIR})

dep_message(STATUS "Found external package definitions: ${FOUND_PACKAGES}")

set(${PROJECT_NAME}_PLATFORM_PACKAGES "" CACHE STRING "Select packages which are provided by the platform" )
set(SYSTEM_PROVIDED_PACKAGES OpenGL)

if (UNIX)
    # On UNIX systems (including Apple) ZLIB should be available
    list(APPEND SYSTEM_PROVIDED_PACKAGES ZLIB)
    if (APPLE)
        # Deal with CURL on Apple (See issue #5984 on GH):
        # Mac SDK should include CURL from at least version 10.12
        list(APPEND SYSTEM_PROVIDED_PACKAGES CURL)
    endif ()
endif ()

list(APPEND SYSTEM_PROVIDED_PACKAGES ${${PROJECT_NAME}_PLATFORM_PACKAGES})
list(REMOVE_DUPLICATES SYSTEM_PROVIDED_PACKAGES)

include(CMakeDependentOption)
option(${PROJECT_NAME}_SELECT_ALL "Choose all external projects to be built." ON)

find_package(Git REQUIRED)

# The default command line for patching. Only works for newer 
set(PATCH_CMD ${GIT_EXECUTABLE} apply --verbose --ignore-space-change --whitespace=fix)

# all required package targets that have existing definitions will be gathered here
set(DEPS_TO_BUILD "")
set(_build_list "")
set(_build_list_toplevel "")
set(_checked_list "")

# function to check if a package ought to be provided by the platform can really be found
function (check_system_package pkg checked_list)
  if (NOT ${pkg} IN_LIST ${checked_list})
    find_package(${pkg})
    if (NOT ${pkg}_FOUND)
      dep_message(WARNING "No ${pkg} found in system altough marked as system provided. This might cause trouble building the dependencies on this platform")
    endif ()
    list(APPEND ${checked_list} ${pkg})
    set (${checked_list} ${${checked_list}} PARENT_SCOPE)
  endif ()
endfunction()

# Go through all the found package definition folders and filter them according to the provided cache options
set(SUPPORTED_PACKAGES "")
foreach (pkg ${FOUND_PACKAGES})
    cmake_dependent_option(${PROJECT_NAME}_SELECT_${pkg} "Select package ${pkg} to be built." OFF  "NOT ${PROJECT_NAME}_SELECT_ALL" OFF)
    if (NOT ${PROJECT_NAME}_PACKAGE_EXCLUDES MATCHES ${pkg} AND (${PROJECT_NAME}_SELECT_ALL OR ${PROJECT_NAME}_SELECT_${pkg}))
        include(+${pkg}/${pkg}.cmake)

        list(APPEND SUPPORTED_PACKAGES ${pkg})

        if (${pkg} IN_LIST SYSTEM_PROVIDED_PACKAGES)
          check_system_package(${pkg} _checked_list)
        elseif (TARGET dep_${pkg})
          get_target_property(_is_excluded_from_all dep_${pkg} EXCLUDE_FROM_ALL)
          if (NOT _is_excluded_from_all)
            list(APPEND DEPS_TO_BUILD ${pkg})
          endif ()
        endif ()
    endif ()
endforeach()

# This ugly append ensures that WebView2 was appended no matter what EXCLUDE_FROM_ALL is.
# (Webview2 is not added by add_cmake_project)
if (MSVC)
  list(APPEND DEPS_TO_BUILD WebView2)
endif()

# Establish dependency graph
foreach (pkg ${SUPPORTED_PACKAGES})
  if (${pkg} IN_LIST DEPS_TO_BUILD)
    list(APPEND _build_list dep_${pkg})
    list(APPEND _build_list_toplevel dep_${pkg})
  endif ()
  foreach(deppkg ${DEP_${pkg}_DEPENDS})
    if (${deppkg} IN_LIST SYSTEM_PROVIDED_PACKAGES)
      check_system_package(${deppkg} _checked_list)
    elseif(TARGET dep_${deppkg})
      dep_message(STATUS "Mapping dep_${deppkg} => dep_${pkg}")
      add_dependencies(dep_${pkg} dep_${deppkg})
      list(APPEND _build_list dep_${deppkg})
    endif ()
  endforeach()
endforeach()

list(REMOVE_DUPLICATES _build_list)
dep_message(STATUS "Building dep targets (${CMAKE_BUILD_TYPE}): ${_build_list}")
add_custom_target(deps ALL DEPENDS ${_build_list_toplevel})

# Support legacy option DEP_DEBUG on MSVC to build debug libraries in the same cmake run as for CMAKE_BUILD_TYPE:
if (DEP_DEBUG AND NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
  # MSVC has this nice feature to not be able to link release mode libs to Debug mode
  # projects

  # Exclude the libraries which have no problem to link to Debug builds in
  # Release mode (mostly C libraries)
  set(DEP_DEBUG_EXCLUDES GMP MPFR OpenSSL NanoSVG TIFF JPEG ZLIB heatshrink)
  if (UNIX)
    # Making a separate debug build on Unix of wx is a nightmare
    list(APPEND DEP_DEBUG_EXCLUDES wxWidgets)
  endif ()
  
  # Create the list of targets needed in debug mode
  set(_build_list_dbg "")
  set(_build_list_filt ${_build_list})
  list(JOIN  DEP_DEBUG_EXCLUDES "|" _excl_regexp)
  list(FILTER _build_list_filt EXCLUDE REGEX "${_excl_regexp}")

  foreach (t ${_build_list_filt})
    list(APPEND _build_list_dbg ${t}_debug)
  endforeach()

  # Create a subdirectory for the Debug build within the current binary dir:
  file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/_d)
  execute_process(
    COMMAND ${CMAKE_COMMAND} ${CMAKE_CURRENT_SOURCE_DIR} -G${CMAKE_GENERATOR} 
      -DCMAKE_BUILD_TYPE=Debug
      -DDEP_WX_GTK3=${DEP_WX_GTK3}
      -D${PROJECT_NAME}_DEP_DOWNLOAD_DIR=${${PROJECT_NAME}_DEP_DOWNLOAD_DIR}
      -D${PROJECT_NAME}_DEP_INSTALL_PREFIX=${${PROJECT_NAME}_DEP_INSTALL_PREFIX}
      -D${PROJECT_NAME}_PACKAGE_EXCLUDES="${_excl_regexp}"
      -D${PROJECT_NAME}_SELECT_ALL=${${PROJECT_NAME}_SELECT_ALL}
      -D${PROJECT_NAME}_DEP_BUILD_VERBOSE=${${PROJECT_NAME}_DEP_BUILD_VERBOSE}
      -DCMAKE_DEBUG_POSTFIX=d
      #TODO: forward per-package selector variables
      -DDEP_MESSAGES_WRITTEN=ON
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/_d 
    OUTPUT_QUIET
  )

  dep_message(STATUS "Building dep targets (Debug): ${_build_list_dbg}")

  # Each lib will have a dep_<package>_debug target to build only the debug counterpart
  # Not part of ALL (problem with parallelization)
  foreach(pkgtgt ${_build_list_filt})
    add_custom_target(${pkgtgt}_debug
      COMMAND ${CMAKE_COMMAND} --build . --target ${pkgtgt}
      WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/_d
      USES_TERMINAL
    )
  endforeach()

  # Can be used to build all the debug libs
  string(JOIN " " _build_list_filt_targets "${_build_list_filt}")
  add_custom_target(deps_debug ALL 
    COMMAND ${CMAKE_COMMAND} --build . --target ${_build_list_filt_targets}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/_d
    USES_TERMINAL
  )

  # The Release must be built before, as there are libs in this debug session which need
  # the release versions of the excluded libs
  add_dependencies(deps_debug deps)

endif ()

set(DEP_MESSAGES_WRITTEN ON CACHE BOOL "")

install(CODE "message(STATUS \"Built packages succesfully.\")")
