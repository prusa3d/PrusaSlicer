# Copyright Contributors to the OpenVDB Project
# SPDX-License-Identifier: MPL-2.0
#
#[=======================================================================[.rst:

FindOpenVDB
-----------

Find OpenVDB include dirs, libraries and settings

Use this module by invoking find_package with the form::

  find_package(OpenVDB
    [version] [EXACT]      # Minimum or EXACT version
    [REQUIRED]             # Fail with error if OpenVDB is not found
    [COMPONENTS <libs>...] # OpenVDB libraries by their canonical name
                           # e.g. "openvdb" for "libopenvdb",
                           # "pyopenvdb" for the python plugin
                           # "openvdb_ax" for the OpenVDB AX extension
                           # "openvdb_houdini" for the houdini plugin
                           # "nanovdb" for the nanovdb extension
    )

IMPORTED Targets
^^^^^^^^^^^^^^^^

``OpenVDB::openvdb``
  The core openvdb library target.
``OpenVDB::openvdb_je``
  The core openvdb library target with jemalloc.
``OpenVDB::pyopenvdb``
  The openvdb python library target.
``OpenVDB::openvdb_houdini``
  The openvdb houdini library target.
``OpenVDB::openvdb_ax``
  The openvdb_ax library target.
``OpenVDB::nanovdb``
  The nanovdb library target.

Result Variables
^^^^^^^^^^^^^^^^

This will define the following variables:

``OpenVDB_FOUND``
  True if the system has the OpenVDB library.
``OpenVDB_VERSION``
  The version of the OpenVDB library which was found.
``OpenVDB_INCLUDE_DIRS``
  Include directories needed to use OpenVDB.
``OpenVDB_LIBRARIES``
  Libraries needed to link to OpenVDB.
``OpenVDB_LIBRARY_DIRS``
  OpenVDB library directories.
``OpenVDB_DEFINITIONS``
  Definitions to use when compiling code that uses OpenVDB.
``OpenVDB_${COMPONENT}_FOUND``
  True if the system has the named OpenVDB component.
``OpenVDB_USES_BLOSC``
  True if the OpenVDB Library has been built with blosc support
``OpenVDB_USES_ZLIB``
  True if the OpenVDB Library has been built with zlib support
``OpenVDB_USES_LOG4CPLUS``
  True if the OpenVDB Library has been built with log4cplus support
``OpenVDB_USES_IMATH_HALF``
  True if the OpenVDB Library has been built with Imath half support
``OpenVDB_USES_DELAYED_LOADING``
  True if the OpenVDB Library has been built with delayed-loading
``OpenVDB_ABI``
  Set if this module was able to determine the ABI number the located
  OpenVDB Library was built against. Unset otherwise.

Cache Variables
^^^^^^^^^^^^^^^

The following cache variables may also be set:

``OpenVDB_INCLUDE_DIR``
  The directory containing ``openvdb/version.h``.
``OpenVDB_${COMPONENT}_INCLUDE_DIR``
  Individual component include directories for OpenVDB
``OpenVDB_${COMPONENT}_LIBRARY``
  Individual component libraries for OpenVDB

Hints
^^^^^

Instead of explicitly setting the cache variables, the following variables
may be provided to tell this module where to look.

``OpenVDB_ROOT``
  Preferred installation prefix.
``OPENVDB_INCLUDEDIR``
  Preferred include directory e.g. <prefix>/include
``OPENVDB_LIBRARYDIR``
  Preferred library directory e.g. <prefix>/lib
``OPENVDB_${COMPONENT}_ROOT``
  Preferred installation prefix of a specific component.
``OPENVDB_${COMPONENT}_INCLUDEDIR``
  Preferred include directory of a specific component e.g. <prefix>/include
``OPENVDB_${COMPONENT}_LIBRARYDIR``
  Preferred library directory of a specific component e.g. <prefix>/lib
``SYSTEM_LIBRARY_PATHS``
  Global list of library paths intended to be searched by and find_xxx call
``OPENVDB_USE_STATIC_LIBS``
  Only search for static openvdb libraries
``DISABLE_CMAKE_SEARCH_PATHS``
  Disable CMakes default search paths for find_xxx calls in this module

#]=======================================================================]

cmake_minimum_required(VERSION 3.18)
include(GNUInstallDirs)


# Include utility functions for version information
include(${CMAKE_CURRENT_LIST_DIR}/OpenVDBUtils.cmake)

mark_as_advanced(
  OpenVDB_INCLUDE_DIR
  OpenVDB_LIBRARY
)

set(_FIND_OPENVDB_ADDITIONAL_OPTIONS "")
if(DISABLE_CMAKE_SEARCH_PATHS)
  set(_FIND_OPENVDB_ADDITIONAL_OPTIONS NO_DEFAULT_PATH)
endif()

set(_OPENVDB_COMPONENT_LIST
  openvdb
  openvdb_je
  pyopenvdb
  openvdb_ax
  openvdb_houdini
  nanovdb
)

if(OpenVDB_FIND_COMPONENTS)
  set(OPENVDB_COMPONENTS_PROVIDED TRUE)
  set(_IGNORED_COMPONENTS "")
  foreach(COMPONENT ${OpenVDB_FIND_COMPONENTS})
    if(NOT ${COMPONENT} IN_LIST _OPENVDB_COMPONENT_LIST)
      list(APPEND _IGNORED_COMPONENTS ${COMPONENT})
    endif()
  endforeach()

  if(_IGNORED_COMPONENTS)
    message(STATUS "Ignoring unknown components of OpenVDB:")
    foreach(COMPONENT ${_IGNORED_COMPONENTS})
      message(STATUS "  ${COMPONENT}")
    endforeach()
    list(REMOVE_ITEM OpenVDB_FIND_COMPONENTS ${_IGNORED_COMPONENTS})
  endif()
else()
  set(OPENVDB_COMPONENTS_PROVIDED FALSE)
  set(OpenVDB_FIND_COMPONENTS openvdb)
endif()

# always make sure openvdb is picked up as a component i.e.
# find_package(OpenVDB COMPONENTS pyopenvdb) results in both
# openvdb and pyopenvdb targets. Also make sure it appears
# first in the component lists.
list(INSERT OpenVDB_FIND_COMPONENTS 0 openvdb)
list(REMOVE_DUPLICATES OpenVDB_FIND_COMPONENTS)

# Set _OPENVDB_ROOT based on a user provided root var. Xxx_ROOT and ENV{Xxx_ROOT}
# are prioritised over the legacy capitalized XXX_ROOT variables for matching
# CMake 3.12 behaviour
# @todo  deprecate -D and ENV OPENVDB_ROOT from CMake 3.12
if(OpenVDB_ROOT)
  set(_OPENVDB_ROOT ${OpenVDB_ROOT})
elseif(DEFINED ENV{OpenVDB_ROOT})
  set(_OPENVDB_ROOT $ENV{OpenVDB_ROOT})
elseif(OPENVDB_ROOT)
  set(_OPENVDB_ROOT ${OPENVDB_ROOT})
elseif(DEFINED ENV{OPENVDB_ROOT})
  set(_OPENVDB_ROOT $ENV{OPENVDB_ROOT})
endif()

# Additionally try and use pkconfig to find OpenVDB
if(USE_PKGCONFIG)
  if(NOT DEFINED PKG_CONFIG_FOUND)
    find_package(PkgConfig)
  endif()
  pkg_check_modules(PC_OpenVDB QUIET OpenVDB)
endif()

# This CMake module supports being called from external packages AND from
# within the OpenVDB repository for building openvdb components with the
# core library build disabled. Determine where we are being called from:
#
# (repo structure = <root>/cmake/FindOpenVDB.cmake)
# (inst structure = <root>/lib/cmake/OpenVDB/FindOpenVDB.cmake)

get_filename_component(_DIR_NAME ${CMAKE_CURRENT_LIST_DIR} NAME)

if(${_DIR_NAME} STREQUAL "cmake")
  # Called from root repo for openvdb components
elseif(${_DIR_NAME} STREQUAL "OpenVDB")
  # Set the install variable to track directories if this is being called from
  # an installed location and from another package. The expected installation
  # directory structure is:
  #  <root>/lib/cmake/OpenVDB/FindOpenVDB.cmake
  #  <root>/include
  #  <root>/bin
  get_filename_component(_IMPORT_PREFIX ${CMAKE_CURRENT_LIST_DIR} DIRECTORY)
  get_filename_component(_IMPORT_PREFIX ${_IMPORT_PREFIX} DIRECTORY)
  get_filename_component(_IMPORT_PREFIX ${_IMPORT_PREFIX} DIRECTORY)
  set(_OPENVDB_INSTALL ${_IMPORT_PREFIX})
  list(APPEND _OPENVDB_ROOT ${_OPENVDB_INSTALL})
endif()

unset(_DIR_NAME)
unset(_IMPORT_PREFIX)

# ------------------------------------------------------------------------
#  Search for OpenVDB include DIR
# ------------------------------------------------------------------------

set(_OPENVDB_INCLUDE_SEARCH_DIRS "")
list(APPEND _OPENVDB_INCLUDE_SEARCH_DIRS
  ${OPENVDB_INCLUDEDIR}
  ${_OPENVDB_ROOT}
  ${PC_OpenVDB_INCLUDE_DIRS}
  ${SYSTEM_LIBRARY_PATHS}
)

foreach(COMPONENT ${OpenVDB_FIND_COMPONENTS})
  # Add in extra component paths
  set(_VDB_COMPONENT_SEARCH_DIRS ${_OPENVDB_INCLUDE_SEARCH_DIRS})
  list(APPEND _VDB_COMPONENT_SEARCH_DIRS
    ${OPENVDB_${COMPONENT}_ROOT}
    ${OPENVDB_${COMPONENT}_INCLUDEDIR}
  )
  if(_VDB_COMPONENT_SEARCH_DIRS)
    list(REMOVE_DUPLICATES _VDB_COMPONENT_SEARCH_DIRS)
  endif()

  # Look for a standard header files.
  if(${COMPONENT} STREQUAL "openvdb")
    # Look for a standard OpenVDB header file.
    find_path(OpenVDB_${COMPONENT}_INCLUDE_DIR openvdb/version.h
      ${_FIND_OPENVDB_ADDITIONAL_OPTIONS}
      PATHS ${_VDB_COMPONENT_SEARCH_DIRS}
      PATH_SUFFIXES
        ${CMAKE_INSTALL_INCLUDEDIR}
        include
    )
  elseif(${COMPONENT} STREQUAL "pyopenvdb")
    find_path(OpenVDB_${COMPONENT}_INCLUDE_DIR pyopenvdb.h
      ${_FIND_OPENVDB_ADDITIONAL_OPTIONS}
      PATHS ${_VDB_COMPONENT_SEARCH_DIRS}
      PATH_SUFFIXES
        ${CMAKE_INSTALL_INCLUDEDIR}/openvdb/python
        ${CMAKE_INSTALL_INCLUDEDIR}/openvdb
        ${CMAKE_INSTALL_INCLUDEDIR}
        include
    )
  elseif(${COMPONENT} STREQUAL "openvdb_ax")
    # Look for a standard OpenVDB header file.
    find_path(OpenVDB_${COMPONENT}_INCLUDE_DIR compiler/Compiler.h
      ${_FIND_OPENVDB_ADDITIONAL_OPTIONS}
      PATHS ${_VDB_COMPONENT_SEARCH_DIRS}
      PATH_SUFFIXES
        ${CMAKE_INSTALL_INCLUDEDIR}/openvdb/openvdb_ax
        ${CMAKE_INSTALL_INCLUDEDIR}/openvdb_ax
        ${CMAKE_INSTALL_INCLUDEDIR}
        include
    )
  elseif(${COMPONENT} STREQUAL "openvdb_houdini")
    # @note  Expects both houdini_utils and openvdb_houdini folders
    #   to be located in the same place
    find_path(OpenVDB_${COMPONENT}_INCLUDE_DIR openvdb_houdini/SOP_NodeVDB.h
      ${_FIND_OPENVDB_ADDITIONAL_OPTIONS}
      PATHS ${_VDB_COMPONENT_SEARCH_DIRS}
      PATH_SUFFIXES
        ${CMAKE_INSTALL_INCLUDEDIR}/openvdb
        ${CMAKE_INSTALL_INCLUDEDIR}
        include
    )
  elseif(${COMPONENT} STREQUAL "nanovdb")
    # Look for NanoVDB.h
    find_path(OpenVDB_${COMPONENT}_INCLUDE_DIR NanoVDB.h
      ${_FIND_OPENVDB_ADDITIONAL_OPTIONS}
      PATHS ${_VDB_COMPONENT_SEARCH_DIRS}
      PATH_SUFFIXES
        ${CMAKE_INSTALL_INCLUDEDIR}/nanovdb
        ${CMAKE_INSTALL_INCLUDEDIR}
        include
    )
  endif()
  unset(_VDB_COMPONENT_SEARCH_DIRS)
endforeach()

set(OpenVDB_INCLUDE_DIR ${OpenVDB_openvdb_INCLUDE_DIR}
  CACHE PATH "The OpenVDB core include directory")

set(_OPENVDB_VERSION_HEADER "${OpenVDB_INCLUDE_DIR}/openvdb/version.h")
OPENVDB_VERSION_FROM_HEADER("${_OPENVDB_VERSION_HEADER}"
  VERSION OpenVDB_VERSION
  MAJOR   OpenVDB_MAJOR_VERSION
  MINOR   OpenVDB_MINOR_VERSION
  PATCH   OpenVDB_PATCH_VERSION
  ABI     OpenVDB_ABI_FROM_HEADER # will be OpenVDB_MAJOR_VERSION prior to 8.1.0
)

if(OpenVDB_VERSION VERSION_LESS 8.1.0)
  set(_OPENVDB_HAS_NEW_VERSION_HEADER FALSE)
  # ABI gets computed later
else()
  set(_OPENVDB_HAS_NEW_VERSION_HEADER TRUE)
  set(OpenVDB_ABI ${OpenVDB_ABI_FROM_HEADER})
endif()
unset(OpenVDB_ABI_FROM_HEADER)

# ------------------------------------------------------------------------
#  Search for OPENVDB lib DIR
# ------------------------------------------------------------------------

set(_OPENVDB_LIBRARYDIR_SEARCH_DIRS "")

# Append to _OPENVDB_LIBRARYDIR_SEARCH_DIRS in priority order

list(APPEND _OPENVDB_LIBRARYDIR_SEARCH_DIRS
  ${OPENVDB_LIBRARYDIR}
  ${_OPENVDB_ROOT}
  ${PC_OpenVDB_LIBRARY_DIRS}
  ${SYSTEM_LIBRARY_PATHS}
)

# Library suffix handling

set(_OPENVDB_ORIG_CMAKE_FIND_LIBRARY_SUFFIXES ${CMAKE_FIND_LIBRARY_SUFFIXES})

set(OPENVDB_PYTHON_PATH_SUFFIXES
  lib64/python
  lib64/python2.7
  lib64/python3
  lib/python
  lib/python2.7
  lib/python3
)

# Recurse through all the site-packages and dist-packages on the file system
file(GLOB PYTHON_SITE_PACKAGES ${CMAKE_INSTALL_FULL_LIBDIR}/python**/*)
foreach(_site_package_full_dir ${PYTHON_SITE_PACKAGES})
  string(REPLACE ${CMAKE_INSTALL_FULL_LIBDIR} "${CMAKE_INSTALL_LIBDIR}"
                 _site_package_dir ${_site_package_full_dir})
  list(APPEND OPENVDB_PYTHON_PATH_SUFFIXES ${_site_package_dir})
endforeach()

set(OPENVDB_LIB_PATH_SUFFIXES
  ${CMAKE_INSTALL_LIBDIR}
  lib64
  lib
)

list(REMOVE_DUPLICATES OPENVDB_PYTHON_PATH_SUFFIXES)
list(REMOVE_DUPLICATES OPENVDB_LIB_PATH_SUFFIXES)

# Static library setup
if(MSVC)
  if(OPENVDB_USE_STATIC_LIBS)
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".lib")
  endif()
else()
  if(OPENVDB_USE_STATIC_LIBS)
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
  endif()
endif()

set(OpenVDB_LIB_COMPONENTS "")
set(OpenVDB_DEBUG_SUFFIX "d" CACHE STRING "Suffix for the debug libraries")
get_property(_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND MSVC AND NOT _multi_config)
    set(_single_config_debug TRUE)
else()
    set(_single_config_debug FALSE)
endif()

foreach(COMPONENT ${OpenVDB_FIND_COMPONENTS})
  message("COMPONENT = " ${COMPONENT})
  set(LIB_NAME ${COMPONENT})

  # Add in extra component paths
  set(_VDB_COMPONENT_SEARCH_DIRS ${_OPENVDB_LIBRARYDIR_SEARCH_DIRS})
  list(APPEND _VDB_COMPONENT_SEARCH_DIRS
    ${OPENVDB_${COMPONENT}_ROOT}
    ${OPENVDB_${COMPONENT}_LIBRARYDIR}
  )

  if(${COMPONENT} STREQUAL "pyopenvdb")
    set(_OPENVDB_ORIG_CMAKE_FIND_LIBRARY_PREFIXES ${CMAKE_FIND_LIBRARY_PREFIXES})
    set(CMAKE_FIND_LIBRARY_PREFIXES ";lib") # find non-prefixed
    find_library(OpenVDB_${COMPONENT}_LIBRARY ${LIB_NAME}
      ${_FIND_OPENVDB_ADDITIONAL_OPTIONS}
      PATHS ${_VDB_COMPONENT_SEARCH_DIRS}
      PATH_SUFFIXES ${OPENVDB_PYTHON_PATH_SUFFIXES}
    )
    set(CMAKE_FIND_LIBRARY_PREFIXES ${_OPENVDB_ORIG_CMAKE_FIND_LIBRARY_PREFIXES})
  elseif(${COMPONENT} STREQUAL "openvdb_je")
    # alias to the result of openvdb which should be handled first
    set(OpenVDB_${COMPONENT}_LIBRARY ${OpenVDB_openvdb_LIBRARY})
  elseif(${COMPONENT} STREQUAL "nanovdb")
    # alias to the result of openvdb which should be handled first
    set(OpenVDB_${COMPONENT}_LIBRARY ${OpenVDB_openvdb_LIBRARY})
  else()

    if(WIN32)
        set(PrusaSlicer_OpenVDB_prefix lib)
    endif()

    find_library(OpenVDB_${COMPONENT}_LIBRARY ${PrusaSlicer_OpenVDB_prefix}${LIB_NAME}
      ${_FIND_OPENVDB_ADDITIONAL_OPTIONS}
      PATHS ${_VDB_COMPONENT_SEARCH_DIRS}
      PATH_SUFFIXES ${OPENVDB_LIB_PATH_SUFFIXES}
    )

    if (_multi_config OR _single_config_debug)
      find_library(OpenVDB_${COMPONENT}_LIBRARY_DEBUG ${PrusaSlicer_OpenVDB_prefix}${LIB_NAME}${OpenVDB_DEBUG_SUFFIX}
              ${_FIND_OPENVDB_ADDITIONAL_OPTIONS}
              PATHS ${_VDB_COMPONENT_SEARCH_DIRS}
              PATH_SUFFIXES ${OPENVDB_LIB_PATH_SUFFIXES}
      )
    endif(_multi_config OR _single_config_debug)

  endif()

  list(APPEND OpenVDB_LIB_COMPONENTS ${OpenVDB_${COMPONENT}_LIBRARY})
  if(${COMPONENT} STREQUAL "nanovdb")
    # nanovdb is headers-only, no lib component
    if(OpenVDB_${COMPONENT}_INCLUDE_DIR)
      set(OpenVDB_${COMPONENT}_FOUND TRUE)
    else()
      set(OpenVDB_${COMPONENT}_FOUND FALSE)
    endif()
  else()
    if(OpenVDB_${COMPONENT}_LIBRARY)
      set(OpenVDB_${COMPONENT}_FOUND TRUE)
    else()
      set(OpenVDB_${COMPONENT}_FOUND FALSE)
    endif()
  endif()
  unset(_VDB_COMPONENT_SEARCH_DIRS)
endforeach()

unset(OPENVDB_PYTHON_PATH_SUFFIXES)
unset(OPENVDB_LIB_PATH_SUFFIXES)

# Reset library suffix

set(CMAKE_FIND_LIBRARY_SUFFIXES ${_OPENVDB_ORIG_CMAKE_FIND_LIBRARY_SUFFIXES})
unset(_OPENVDB_ORIG_CMAKE_FIND_LIBRARY_SUFFIXES)

# ------------------------------------------------------------------------
#  Cache and set OPENVDB_FOUND
# ------------------------------------------------------------------------
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(OpenVDB
  FOUND_VAR OpenVDB_FOUND
  REQUIRED_VARS
    OpenVDB_INCLUDE_DIR
    OpenVDB_LIB_COMPONENTS
  VERSION_VAR OpenVDB_VERSION
  HANDLE_COMPONENTS
)

# ------------------------------------------------------------------------
#  Determine ABI number
# ------------------------------------------------------------------------

# Set the ABI number the library was built against. The old system,
# which didn't define the ABI in the build config, uses vdb_print

if(NOT _OPENVDB_HAS_NEW_VERSION_HEADER)
  if(_OPENVDB_INSTALL)
    OPENVDB_ABI_VERSION_FROM_PRINT(
      "${_OPENVDB_INSTALL}/bin/vdb_print"
      ABI OpenVDB_ABI
    )
  else()
    # Try and find vdb_print from the include path
    OPENVDB_ABI_VERSION_FROM_PRINT(
      "${OpenVDB_INCLUDE_DIR}/../bin/vdb_print"
      ABI OpenVDB_ABI
    )
  endif()
endif()

if(NOT OpenVDB_FIND_QUIETLY)
  if(NOT OpenVDB_ABI)
    message(WARNING "Unable to determine OpenVDB ABI version from OpenVDB "
      "installation. The library major version \"${OpenVDB_MAJOR_VERSION}\" "
      "will be inferred. If this is not correct, use "
      "add_definitions(-DOPENVDB_ABI_VERSION_NUMBER=N)"
    )
  else()
    message(STATUS "OpenVDB ABI Version: ${OpenVDB_ABI}")
  endif()
endif()

# ------------------------------------------------------------------------
#  Handle OpenVDB dependencies and interface settings
# ------------------------------------------------------------------------

# Handle openvdb_houdini first to configure search paths

if(openvdb_houdini IN_LIST OpenVDB_FIND_COMPONENTS)
  include(OpenVDBHoudiniSetup)
endif()

# Add standard dependencies

find_package(TBB REQUIRED COMPONENTS tbb)
find_package(Boost REQUIRED COMPONENTS iostreams)

# Add deps for pyopenvdb
# @todo track for numpy

if(pyopenvdb IN_LIST OpenVDB_FIND_COMPONENTS)
  find_package(Python REQUIRED)

  # Boost python handling - try and find both python and pythonXx (version suffixed).
  # Prioritize the version suffixed library, failing if neither exist.

  find_package(Boost ${MINIMUM_BOOST_VERSION}
    QUIET COMPONENTS python${PYTHON_VERSION_MAJOR}${PYTHON_VERSION_MINOR}
  )

  if(TARGET Boost::python${PYTHON_VERSION_MAJOR}${PYTHON_VERSION_MINOR})
    set(BOOST_PYTHON_LIB "python${PYTHON_VERSION_MAJOR}${PYTHON_VERSION_MINOR}")
    message(STATUS "Found boost_python${PYTHON_VERSION_MAJOR}${PYTHON_VERSION_MINOR}")
  else()
    find_package(Boost ${MINIMUM_BOOST_VERSION} QUIET COMPONENTS python)
    if(TARGET Boost::python)
      set(BOOST_PYTHON_LIB "python")
      message(STATUS "Found non-suffixed boost_python, assuming to be python version "
        "\"${PYTHON_VERSION_MAJOR}.${PYTHON_VERSION_MINOR}\" compatible"
      )
    else()
      message(FATAL_ERROR "Unable to find boost_python or "
        "boost_python${PYTHON_VERSION_MAJOR}${PYTHON_VERSION_MINOR}."
      )
    endif()
  endif()
endif()

# Add deps for openvdb_ax

if(openvdb_ax IN_LIST OpenVDB_FIND_COMPONENTS)
  find_package(LLVM REQUIRED)
  find_library(found_LLVM LLVM HINTS ${LLVM_LIBRARY_DIRS})

  if(found_LLVM)
    set(LLVM_LIBS "LLVM")
  else()
    llvm_map_components_to_libnames(_llvm_libs
      native core executionengine support mcjit passes objcarcopts)
    set(LLVM_LIBS "${_llvm_libs}")
  endif()

  if(NOT OpenVDB_FIND_QUIET)
    message(STATUS "Found LLVM: ${LLVM_DIR} (found version \"${LLVM_PACKAGE_VERSION}\")")
  endif()
endif()

# As the way we resolve optional libraries relies on library file names, use
# the configuration options from the main CMakeLists.txt to allow users
# to manually identify the requirements of OpenVDB builds if they know them.
set(OpenVDB_USES_BLOSC ${USE_BLOSC})
set(OpenVDB_USES_ZLIB ${USE_ZLIB})
set(OpenVDB_USES_LOG4CPLUS ${USE_LOG4CPLUS})
set(OpenVDB_USES_IMATH_HALF ${USE_IMATH_HALF})
set(OpenVDB_USES_DELAYED_LOADING ${OPENVDB_USE_DELAYED_LOADING})
set(OpenVDB_DEFINITIONS)

if(WIN32)
  if(OPENVDB_USE_STATIC_LIBS)
    list(APPEND OpenVDB_DEFINITIONS OPENVDB_STATICLIB)
  else()
    list(APPEND OpenVDB_DEFINITIONS OPENVDB_DLL)
  endif()
  # Newer version of OpenVDB define these in Platform.h, but they are also
  # provided here to maintain backwards compatibility with header include
  # others
  list(APPEND OpenVDB_DEFINITIONS _WIN32)
  list(APPEND OpenVDB_DEFINITIONS NOMINMAX)
endif()

if(MINGW)
  list(APPEND OpenVDB_DEFINITIONS _USE_MATH_DEFINES)
endif()

if(OpenVDB_ABI)
  # Newer version of OpenVDB defines this in version.h, but it is are also
  # provided here to maintain backwards compatibility with header include
  # others
  list(APPEND OpenVDB_DEFINITIONS OPENVDB_ABI_VERSION_NUMBER=${OpenVDB_ABI})
endif()

# Configure deps

if(_OPENVDB_HAS_NEW_VERSION_HEADER)
  OPENVDB_GET_VERSION_DEFINE(${_OPENVDB_VERSION_HEADER} "OPENVDB_USE_IMATH_HALF" OpenVDB_USES_IMATH_HALF)
  OPENVDB_GET_VERSION_DEFINE(${_OPENVDB_VERSION_HEADER} "OPENVDB_USE_BLOSC" OpenVDB_USES_BLOSC)
  OPENVDB_GET_VERSION_DEFINE(${_OPENVDB_VERSION_HEADER} "OPENVDB_USE_ZLIB" OpenVDB_USES_ZLIB)
  OPENVDB_GET_VERSION_DEFINE(${_OPENVDB_VERSION_HEADER} "OPENVDB_USE_DELAYED_LOADING" OpenVDB_USES_DELAYED_LOADING)
elseif(NOT OPENVDB_USE_STATIC_LIBS)
  # Use GetPrerequisites to see which libraries this OpenVDB lib has linked to
  # which we can query for optional deps. This basically runs ldd/otoll/objdump
  # etc to track deps. We could use a vdb_config binary tools here to improve
  # this process
  include(GetPrerequisites)

  set(_EXCLUDE_SYSTEM_PREREQUISITES 1)
  set(_RECURSE_PREREQUISITES 0)
  set(_OPENVDB_PREREQUISITE_LIST)

  get_prerequisites(${OpenVDB_openvdb_LIBRARY}
    _OPENVDB_PREREQUISITE_LIST
    ${_EXCLUDE_SYSTEM_PREREQUISITES}
    ${_RECURSE_PREREQUISITES}
    ""
    "${SYSTEM_LIBRARY_PATHS}"
  )

  unset(_EXCLUDE_SYSTEM_PREREQUISITES)
  unset(_RECURSE_PREREQUISITES)

  # Search for optional dependencies
  foreach(PREREQUISITE ${_OPENVDB_PREREQUISITE_LIST})
    set(_HAS_DEP)
    get_filename_component(PREREQUISITE ${PREREQUISITE} NAME)

    string(FIND ${PREREQUISITE} "blosc" _HAS_DEP)
    if(NOT ${_HAS_DEP} EQUAL -1)
      set(OpenVDB_USES_BLOSC ON)
    endif()

    string(FIND ${PREREQUISITE} "zlib" _HAS_DEP)
    if(NOT ${_HAS_DEP} EQUAL -1)
      set(OpenVDB_USES_ZLIB ON)
    endif()

    string(FIND ${PREREQUISITE} "log4cplus" _HAS_DEP)
    if(NOT ${_HAS_DEP} EQUAL -1)
      set(OpenVDB_USES_LOG4CPLUS ON)
    endif()

    string(FIND ${PREREQUISITE} "Half" _HAS_DEP)
    if(NOT ${_HAS_DEP} EQUAL -1)
      set(OpenVDB_USES_IMATH_HALF ON)
    endif()

    string(FIND ${PREREQUISITE} "boost_iostreams" _HAS_DEP)
    if(NOT ${_HAS_DEP} EQUAL -1)
      set(OpenVDB_USES_DELAYED_LOADING ON)
    endif()
  endforeach()

  unset(_OPENVDB_PREREQUISITE_LIST)
endif()

if(OpenVDB_USES_BLOSC)
  find_package(Blosc REQUIRED)
endif()

if(OpenVDB_USES_ZLIB)
  find_package(ZLIB REQUIRED)
endif()

if(OpenVDB_USES_LOG4CPLUS)
  find_package(Log4cplus REQUIRED)
endif()

if(OpenVDB_USES_IMATH_HALF)
  find_package(Imath REQUIRED CONFIG)
endif()

if(UNIX)
  find_package(Threads REQUIRED)
endif()

# Set deps. Note that the order here is important. If we're building against
# Houdini we must include Imath deps first to ensure the users chosen
# namespaced headers are correctly prioritized. Otherwise other include paths
# from shared installs (including houdini) may pull in the wrong headers

set(_OPENVDB_VISIBLE_DEPENDENCIES "")

if(OpenVDB_USES_DELAYED_LOADING)
  list(APPEND _OPENVDB_VISIBLE_DEPENDENCIES Boost::iostreams)
  list(APPEND OpenVDB_DEFINITIONS OPENVDB_USE_DELAYED_LOADING)
endif()

if(OpenVDB_USES_IMATH_HALF)
  list(APPEND _OPENVDB_VISIBLE_DEPENDENCIES Imath::Imath)
endif()

if(OpenVDB_USES_LOG4CPLUS)
  list(APPEND _OPENVDB_VISIBLE_DEPENDENCIES Log4cplus::log4cplus)
  list(APPEND OpenVDB_DEFINITIONS OPENVDB_USE_LOG4CPLUS)
endif()

list(APPEND _OPENVDB_VISIBLE_DEPENDENCIES
  TBB::tbb Blosc::blosc
)
if(UNIX)
  list(APPEND _OPENVDB_VISIBLE_DEPENDENCIES
    Threads::Threads
  )
endif()

set(_OPENVDB_HIDDEN_DEPENDENCIES)

if(NOT OPENVDB_USE_STATIC_LIBS)
  if(OpenVDB_USES_BLOSC)
    list(APPEND _OPENVDB_HIDDEN_DEPENDENCIES Blosc::blosc)
  endif()
  if(OpenVDB_USES_ZLIB)
    list(APPEND _OPENVDB_HIDDEN_DEPENDENCIES ZLIB::ZLIB)
  endif()
endif()

if(openvdb_je IN_LIST OpenVDB_FIND_COMPONENTS)
  find_package(Jemalloc REQUIRED)
endif()

# ------------------------------------------------------------------------
#  Configure imported targets
# ------------------------------------------------------------------------

set(OpenVDB_LIBRARIES ${OpenVDB_LIB_COMPONENTS})
set(OpenVDB_INCLUDE_DIRS ${OpenVDB_INCLUDE_DIR})

set(OpenVDB_LIBRARY_DIRS "")
foreach(LIB ${OpenVDB_LIB_COMPONENTS})
  get_filename_component(_OPENVDB_LIBDIR ${LIB} DIRECTORY)
  list(APPEND OpenVDB_LIBRARY_DIRS ${_OPENVDB_LIBDIR})
endforeach()
list(REMOVE_DUPLICATES OpenVDB_LIBRARY_DIRS)

# OpenVDB::openvdb

if(NOT TARGET OpenVDB::openvdb)
  set(OPENVDB_openvdb_LIB_TYPE UNKNOWN)
  if(OPENVDB_USE_STATIC_LIBS)
    set(OPENVDB_openvdb_LIB_TYPE STATIC)
  elseif(UNIX)
    get_filename_component(_OPENVDB_openvdb_EXT
      ${OpenVDB_openvdb_LIBRARY} EXT)
    if(_OPENVDB_openvdb_EXT STREQUAL ".a")
      set(OPENVDB_openvdb_LIB_TYPE STATIC)
    elseif(_OPENVDB_openvdb_EXT STREQUAL ".so" OR
           _OPENVDB_openvdb_EXT STREQUAL ".dylib")
      set(OPENVDB_openvdb_LIB_TYPE SHARED)
    endif()
  endif()

  add_library(OpenVDB::openvdb ${OPENVDB_openvdb_LIB_TYPE} IMPORTED)
  set_target_properties(OpenVDB::openvdb PROPERTIES
    IMPORTED_LOCATION "${OpenVDB_openvdb_LIBRARY}"
    INTERFACE_COMPILE_OPTIONS "${PC_OpenVDB_CFLAGS_OTHER}"
    INTERFACE_COMPILE_DEFINITIONS "${OpenVDB_DEFINITIONS}"
    INTERFACE_INCLUDE_DIRECTORIES "${OpenVDB_INCLUDE_DIR}"
    IMPORTED_LINK_DEPENDENT_LIBRARIES "${_OPENVDB_HIDDEN_DEPENDENCIES}" # non visible deps
    INTERFACE_LINK_LIBRARIES "${_OPENVDB_VISIBLE_DEPENDENCIES}" # visible deps (headers)
    INTERFACE_COMPILE_FEATURES cxx_std_17
  )
  if (_multi_config OR _single_config_debug)
    set_target_properties(OpenVDB::openvdb PROPERTIES
            IMPORTED_LOCATION_DEBUG "${OpenVDB_openvdb_LIBRARY_DEBUG}")
  endif (_multi_config OR _single_config_debug)
endif()

# OpenVDB::openvdb_je

if(OpenVDB_openvdb_je_LIBRARY)
  if(NOT TARGET OpenVDB::openvdb_je)
    add_library(OpenVDB::openvdb_je INTERFACE IMPORTED)
    target_link_libraries(OpenVDB::openvdb_je INTERFACE OpenVDB::openvdb)
    target_link_libraries(OpenVDB::openvdb_je INTERFACE Jemalloc::jemalloc)
  endif()
endif()

# OpenVDB::pyopenvdb

if(OpenVDB_pyopenvdb_LIBRARY)
  if(NOT TARGET OpenVDB::pyopenvdb)
    add_library(OpenVDB::pyopenvdb MODULE IMPORTED)
    set_target_properties(OpenVDB::pyopenvdb PROPERTIES
      IMPORTED_LOCATION "${OpenVDB_pyopenvdb_LIBRARY}"
      INTERFACE_INCLUDE_DIRECTORIES "${OpenVDB_pyopenvdb_INCLUDE_DIR};${PYTHON_INCLUDE_DIR}"
      INTERFACE_LINK_LIBRARIES "OpenVDB::openvdb;Boost::${BOOST_PYTHON_LIB};${PYTHON_LIBRARIES}"
      INTERFACE_COMPILE_FEATURES cxx_std_17
   )
  endif()
endif()

# OpenVDB::openvdb_houdini

if(OpenVDB_openvdb_houdini_LIBRARY)
  if(NOT TARGET OpenVDB::openvdb_houdini)
    add_library(OpenVDB::openvdb_houdini SHARED IMPORTED)
    set_target_properties(OpenVDB::openvdb_houdini PROPERTIES
      IMPORTED_LOCATION "${OpenVDB_openvdb_houdini_LIBRARY}"
      INTERFACE_INCLUDE_DIRECTORIES "${OpenVDB_openvdb_houdini_INCLUDE_DIR}"
      INTERFACE_LINK_LIBRARIES "OpenVDB::openvdb;Houdini"
      INTERFACE_COMPILE_FEATURES cxx_std_17
   )
  endif()
endif()

# OpenVDB::openvdb_ax

if(OpenVDB_openvdb_ax_LIBRARY)
  set(OPENVDB_openvdb_ax_LIB_TYPE UNKNOWN)
  if(OPENVDB_USE_STATIC_LIBS)
    set(OPENVDB_openvdb_ax_LIB_TYPE STATIC)
  elseif(UNIX)
    get_filename_component(_OPENVDB_openvdb_ax_EXT
      ${OpenVDB_openvdb_ax_LIBRARY} EXT)
    if(_OPENVDB_openvdb_ax_EXT STREQUAL ".a")
      set(OPENVDB_openvdb_ax_LIB_TYPE STATIC)
    elseif(_OPENVDB_openvdb_ax_EXT STREQUAL ".so" OR
           _OPENVDB_openvdb_ax_EXT STREQUAL ".dylib")
      set(OPENVDB_openvdb_ax_LIB_TYPE SHARED)
    endif()
  endif()


  if(NOT TARGET OpenVDB::openvdb_ax)
    add_library(OpenVDB::openvdb_ax UNKNOWN IMPORTED)
    set_target_properties(OpenVDB::openvdb_ax PROPERTIES
      IMPORTED_LOCATION "${OpenVDB_openvdb_ax_LIBRARY}"
      INTERFACE_INCLUDE_DIRECTORIES "${OpenVDB_openvdb_ax_INCLUDE_DIR}"
      INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${LLVM_INCLUDE_DIRS}"
      INTERFACE_LINK_LIBRARIES "OpenVDB::openvdb;${LLVM_LIBS}"
      INTERFACE_COMPILE_FEATURES cxx_std_17
    )
  endif()
endif()

# OpenVDB::nanovdb

if(OpenVDB_nanovdb_LIBRARY)
  if(NOT TARGET OpenVDB::nanovdb)
    add_library(OpenVDB::nanovdb INTERFACE IMPORTED)
    set_target_properties(OpenVDB::nanovdb PROPERTIES
      IMPORTED_LOCATION "${OpenVDB_nanovdb_LIBRARY}"
      INTERFACE_INCLUDE_DIRECTORIES "${OpenVDB_nanovdb_INCLUDE_DIR}"
      INTERFACE_LINK_LIBRARIES "OpenVDB::openvdb;"
      INTERFACE_COMPILE_FEATURES cxx_std_17
   )
  endif()
endif()

unset(_OPENVDB_VISIBLE_DEPENDENCIES)
unset(_OPENVDB_HIDDEN_DEPENDENCIES)
