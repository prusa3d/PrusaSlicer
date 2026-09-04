# Initialize the global property to hold C++ source paths (NOT stubs)
set_property(GLOBAL PROPERTY LUA_DOCS_SOURCES "")

function(add_target_to_lua_docs target)
    get_target_property(target_sources ${target} SOURCES)
    get_target_property(target_dir ${target} SOURCE_DIR)

    set(absolute_sources "")
    foreach(src IN LISTS target_sources)
        if(NOT IS_ABSOLUTE "${src}")
            set(src "${target_dir}/${src}")
        endif()
        list(APPEND absolute_sources "${src}")
    endforeach()

    set_property(GLOBAL APPEND PROPERTY LUA_DOCS_SOURCES ${absolute_sources})
endfunction()

find_program(EMMYLUA_DOC_CLI_EXECUTABLE NAMES emmylua_doc_cli)

function(add_lua_docs_target target)
    get_property(all_sources GLOBAL PROPERTY LUA_DOCS_SOURCES)
    if(NOT all_sources)
        message(STATUS "No sources registered for Lua docs. Skipping target creation.")
        return()
    endif()

    list(REMOVE_DUPLICATES all_sources)

    set(EXTRACTOR_SCRIPT "${CMAKE_SOURCE_DIR}/cmake/modules/ExtractLuaDocs.cmake")
    set(STUB_DIR "${CMAKE_BINARY_DIR}/lua_stubs")
    set(SOURCES_LIST_FILE "${CMAKE_BINARY_DIR}/.lua_docs_sources.txt")

    # 1. Filter sources at configure time and write to a list file
    # (prevents exceeding OS command-line character limits)
    file(WRITE "${SOURCES_LIST_FILE}" "")
    set(active_sources "")
    foreach(src IN LISTS all_sources)
        file(STRINGS "${src}" has_lua_docs REGEX "^[ \t]*//-")
        if(has_lua_docs)
            file(APPEND "${SOURCES_LIST_FILE}" "${src}\n")
            list(APPEND active_sources "${src}")
        endif()
    endforeach()

    # 2. Define a single custom command to handle the M:N extraction
    # This guarantees the directory is wiped right before extraction starts.
    set(EXTRACTION_DONE_MARKER "${CMAKE_BINARY_DIR}/.lua_stubs_extracted")
    add_custom_command(
            OUTPUT "${EXTRACTION_DONE_MARKER}"
            # Wipe and recreate the stubs directory
            COMMAND ${CMAKE_COMMAND} -E rm -rf "${STUB_DIR}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${STUB_DIR}"
            # Run the extraction
            COMMAND ${CMAKE_COMMAND}
            -D "SOURCES_LIST_FILE=${SOURCES_LIST_FILE}"
            -D "STUB_DIR=${STUB_DIR}"
            -P "${EXTRACTOR_SCRIPT}"
            # Touch the marker file so CMake knows it succeeded
            COMMAND ${CMAKE_COMMAND} -E touch "${EXTRACTION_DONE_MARKER}"
            DEPENDS ${active_sources} "${EXTRACTOR_SCRIPT}"
            COMMENT "Extracting Lua docs across all sources..."
            VERBATIM
    )

    # 3. Create the final target
    if(EMMYLUA_DOC_CLI_EXECUTABLE)
        add_custom_target("${target}"
                COMMAND "${EMMYLUA_DOC_CLI_EXECUTABLE}"
                --input "${STUB_DIR}"
                --output "${CMAKE_SOURCE_DIR}/doc/lua_api"
                # Depend on the marker instead of individual stub files
                DEPENDS "${EXTRACTION_DONE_MARKER}"
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                COMMENT "Generating Markdown documentation from updated stubs..."
                VERBATIM
        )
    else()
        add_custom_target("${target}"
                COMMAND ${CMAKE_COMMAND} -E echo "====================================================="
                COMMAND ${CMAKE_COMMAND} -E echo " Warning: 'emmylua_doc_cli' was not found in PATH."
                COMMAND ${CMAKE_COMMAND} -E echo " Lua documentation generation is skipped."
                COMMAND ${CMAKE_COMMAND} -E echo "====================================================="
                COMMENT "Skipping Lua documentation generation."
                VERBATIM
        )
        message(STATUS "emmylua_doc_cli not found; '${target}' target will be a no-op.")
    endif()
endfunction()
