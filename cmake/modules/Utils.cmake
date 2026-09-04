function(slic3r_link_resources _target _resources_dir)
    file(TO_NATIVE_PATH "${_resources_dir}" _resources_dir_native)

    if (WIN32)
        if (CMAKE_CONFIGURATION_TYPES)
            foreach (CONF ${CMAKE_CONFIGURATION_TYPES})
                file(TO_NATIVE_PATH "${CMAKE_CURRENT_BINARY_DIR}/${CONF}" WIN_CONF_OUTPUT_DIR)
                file(TO_NATIVE_PATH "${CMAKE_CURRENT_BINARY_DIR}/${CONF}/resources" WIN_RESOURCES_SYMLINK)
                add_custom_command(TARGET ${_target} POST_BUILD
                        COMMAND if exist "${WIN_CONF_OUTPUT_DIR}" "("
                        if not exist "${WIN_RESOURCES_SYMLINK}" "("
                        mklink /J "${WIN_RESOURCES_SYMLINK}" "${_resources_dir_native}"
                        ")"
                        ")"
                        COMMENT "Symlinking the resources directory into the build tree"
                        VERBATIM
                )
            endforeach ()
        else ()
            file(TO_NATIVE_PATH "${CMAKE_CURRENT_BINARY_DIR}/resources" WIN_RESOURCES_SYMLINK)
            add_custom_command(TARGET ${_target} POST_BUILD
                    COMMAND if not exist "${WIN_RESOURCES_SYMLINK}" "(" mklink /J "${WIN_RESOURCES_SYMLINK}" "${_resources_dir_native}" ")"
                    COMMENT "Symlinking the resources directory into the build tree"
                    VERBATIM
            )
        endif ()

    elseif(EMSCRIPTEN)
        find_package(Boost REQUIRED COMPONENTS filesystem)
        target_link_libraries(${_target} PUBLIC Boost::filesystem)
        message(STATUS "Preload file Flags: " --preload-file "${_resources_dir_native}@/resources")

        set(CMAKE_EXE_LINKER_FLAGS "--preload-file ${_resources_dir_native}@/resources -pthread ${CMAKE_EXE_LINKER_FLAGS}")
        set(CMAKE_SHARED_LINKER_FLAGS "--preload-file ${_resources_dir_native}@/resources -pthread ${CMAKE_SHARED_LINKER_FLAGS}")
    else ()
        if (XCODE)
            # Because of Debug/Release/etc. configurations (similar to MSVC) the slic3r binary is located in an extra level
            set(BIN_RESOURCES_DIR "${CMAKE_CURRENT_BINARY_DIR}/resources")
        else ()
            set(BIN_RESOURCES_DIR "${CMAKE_CURRENT_BINARY_DIR}/../resources")
        endif ()
        add_custom_command(TARGET ${_target} POST_BUILD
                COMMAND ln -sfn "${_resources_dir_native}" "${BIN_RESOURCES_DIR}"
                COMMENT "Symlinking the resources directory into the build tree"
                VERBATIM)
    endif ()

endfunction()

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
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/deps/+GMP/gmp/lib/win${_bits}/libgmp-10.dll ${_out_dir}
        COMMENT "Copy gmp runtime to build tree"
        VERBATIM)

    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/deps/+MPFR/mpfr/lib/win${_bits}/libmpfr-4.dll ${_out_dir}
        COMMENT "Copy mpfr runtime to build tree"
        VERBATIM)

    if (TARGET OCCTWrapper)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:OCCTWrapper>" ${_out_dir}
            COMMENT "Copy OCCTWrapper to build tree"
            VERBATIM)
    endif()
endfunction()

function(slic3r_app_extract_symbols target)
    # This entire process is only relevant on Apple platforms.
    if(NOT APPLE OR NOT SLIC3R_RELEASE_DEBUG_SYMBOLS OR NOT "${CMAKE_BUILD_TYPE}" STREQUAL "Release")
        return()
    endif()

    # Find the necessary command-line tools.
    find_program(DSYMUTIL_PROGRAM dsymutil)
    find_program(STRIP_PROGRAM strip)

    if(DSYMUTIL_PROGRAM AND STRIP_PROGRAM)
        message(STATUS "dSYM generation and stripping enabled for ${target}")

        # 1. Add a post-build command to generate the .dSYM bundle.
        # This command runs *before* the stripping command.
        add_custom_command(
                TARGET ${target}
                POST_BUILD
                COMMAND ${DSYMUTIL_PROGRAM} "$<TARGET_FILE:${target}>"
                COMMENT "Generating dSYM for ${target}..."
                VERBATIM
        )

        # 2. Add a post-build command to strip the symbols from the executable.
        # This runs *after* dSYM generation.
        add_custom_command(
                TARGET ${target}
                POST_BUILD
                COMMAND ${STRIP_PROGRAM} -S "$<TARGET_FILE:${target}>"
                COMMENT "Stripping ${target}..."
                VERBATIM
        )
    else()
        message(WARNING "dsymutil or strip not found. Symbol extraction for ${target} will be skipped.")
    endif()
endfunction()

function(slic3r_add_tracy target)
    if(SLIC3R_ENABLE_PROFILING)
        # Profiling ON: Link the library and enable the macros
        target_link_libraries(${target} PRIVATE Tracy::TracyClient)
        target_compile_definitions(${target} PRIVATE TRACY_ENABLE)
    else()
        # Profiling OFF: Do NOT link the library
        # But WE MUST provide the include directories so #include <Tracy.hpp> doesn't fail.
        get_target_property(TRACY_INCLUDES Tracy::TracyClient INTERFACE_INCLUDE_DIRECTORIES)
        if(TRACY_INCLUDES)
            target_include_directories(${target} PRIVATE ${TRACY_INCLUDES})
        endif()
    endif()
endfunction()