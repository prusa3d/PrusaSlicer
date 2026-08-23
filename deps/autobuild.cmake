if (NOT ${PROJECT_NAME}_DEPS_PRESET)
    set (${PROJECT_NAME}_DEPS_PRESET "default")
endif ()

set (_output_quiet "")
if (${PROJECT_NAME}_DEPS_OUTPUT_QUIET)
    set (_output_quiet OUTPUT_QUIET)
endif ()

message(STATUS "Building the dependencies with preset ${${PROJECT_NAME}_DEPS_PRESET}")

set(_gen_arg "")
if (CMAKE_GENERATOR)
    set (_gen_arg "-G${CMAKE_GENERATOR}")
endif ()

set(_build_args "")

if (CMAKE_C_COMPILER)
    list(APPEND _build_args "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}")
endif ()

if (CMAKE_CXX_COMPILER)
    list(APPEND _build_args "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}")
endif ()

if (CMAKE_C_COMPILER_LAUNCHER)
    list(APPEND _build_args "-DCMAKE_C_COMPILER_LAUNCHER=${CMAKE_C_COMPILER_LAUNCHER}")
endif ()

if (CMAKE_CXX_COMPILER_LAUNCHER)
    list(APPEND _build_args "-DCMAKE_CXX_COMPILER_LAUNCHER=${CMAKE_CXX_COMPILER_LAUNCHER}")
endif ()

if (DEFINED DEP_DEBUG)
    list(APPEND _build_args "-DDEP_DEBUG=${DEP_DEBUG}")
endif ()

if (CMAKE_TOOLCHAIN_FILE)
    list(APPEND _build_args "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}")
endif ()

if (CMAKE_OSX_DEPLOYMENT_TARGET)
    list(APPEND _build_args "-DCMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}")
endif ()

if (CMAKE_OSX_SYSROOT)
    list(APPEND _build_args "-DCMAKE_OSX_SYSROOT=${CMAKE_OSX_SYSROOT}")
endif ()

if (CMAKE_POLICY_VERSION_MINIMUM)
    list(APPEND _build_args "-DCMAKE_POLICY_VERSION_MINIMUM=${CMAKE_POLICY_VERSION_MINIMUM}")
endif ()

set(_build_dir "${CMAKE_CURRENT_LIST_DIR}/build-${${PROJECT_NAME}_DEPS_PRESET}")
if (${PROJECT_NAME}_DEPS_BUILD_DIR)
    set(_build_dir "${${PROJECT_NAME}_DEPS_BUILD_DIR}")
endif ()

message(STATUS "build dir = ${_build_dir}")

execute_process(
    COMMAND ${CMAKE_COMMAND} --preset ${${PROJECT_NAME}_DEPS_PRESET} "${_gen_arg}" -B ${_build_dir} ${_build_args}
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
    ${_output_quiet}
    ERROR_VARIABLE _deps_configure_output
    RESULT_VARIABLE _deps_configure_result
)

if (NOT _deps_configure_result EQUAL 0)
    message(FATAL_ERROR "Dependency configure failed with output:\n${_deps_configure_output}")
else ()
    set(_deps_build_command ${CMAKE_COMMAND} --build .)
    if (CMAKE_CONFIGURATION_TYPES)
        set(_deps_build_config "${CMAKE_BUILD_TYPE}")
        if (NOT _deps_build_config)
            list(FIND CMAKE_CONFIGURATION_TYPES "Release" _release_idx)
            if (NOT _release_idx EQUAL -1)
                set(_deps_build_config "Release")
            else ()
                list(GET CMAKE_CONFIGURATION_TYPES 0 _deps_build_config)
            endif ()
        endif ()
        message(STATUS "deps build config = ${_deps_build_config}")
        list(APPEND _deps_build_command --config ${_deps_build_config})
    endif ()
    execute_process(
        COMMAND ${_deps_build_command}
        WORKING_DIRECTORY ${_build_dir}
        ${_output_quiet}
        ERROR_VARIABLE _deps_build_output
        RESULT_VARIABLE _deps_build_result
    )
    if (NOT _deps_build_result EQUAL 0)
        message(FATAL_ERROR "Dependency build failed with output:\n${_deps_build_output}")
    endif ()
endif ()

list(APPEND CMAKE_PREFIX_PATH ${_build_dir}/destdir/usr/local)
set(CMAKE_PREFIX_PATH "${CMAKE_PREFIX_PATH}" CACHE STRING "")
