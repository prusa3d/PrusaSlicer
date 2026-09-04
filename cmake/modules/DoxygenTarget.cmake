
function(_get_target_libraries_with_prefix target_name prefix output_var)
    get_target_property(linked_libraries ${target_name} INTERFACE_LINK_LIBRARIES)
    #get_all_linked_libraries(${target_name} linked_libraries)

    if(NOT linked_libraries)
        return()
    endif()

    set(matching_libraries "")

    foreach(lib ${linked_libraries})
        string(FIND "${lib}" "${prefix}" prefix_pos)
        if(prefix_pos EQUAL 0)
            list(APPEND matching_libraries "${lib}")
        endif()
    endforeach()
    set(${output_var} "${matching_libraries}" PARENT_SCOPE)
endfunction()



set(MODULE_SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR})

function(_get_doxygen_output_dir target output_var)
    set(${output_var} "${MODULE_SOURCE_DIR}/../../doc/doxy/${target}" PARENT_SCOPE)
endfunction()

function(_get_doxygen_tag_file target output_var)
    set(${output_var} "${MODULE_SOURCE_DIR}/../../doc/doxy/${target}/tag" PARENT_SCOPE)
endfunction()


function(_md_fix_fenced_blocks file_list out_dir base_dir out_files_var)
    # Initialize the list of output files
    set(output_files "")

    foreach(file_path IN LISTS file_list)
        get_filename_component(abs_file_path "${file_path}"
                REALPATH BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        # Compute the relative path of the current file with respect to the base_dir
        file(RELATIVE_PATH relative_path "${base_dir}" "${abs_file_path}")

        # Construct the full destination path based on out_dir and relative_path
        set(destination_path "${out_dir}/${relative_path}")

        # Ensure the destination directory exists
        get_filename_component(destination_dir "${destination_path}" DIRECTORY)
        file(MAKE_DIRECTORY ${destination_dir})

        # Read the entire file as a list of lines (preserving semicolons safely)
        file(STRINGS "${file_path}" file_lines)

        # Prepare the modified content
        set(modified_content "")

        # Track whether we are inside a mermaid block
        set(in_mermaid_block FALSE)
        set(current_indent "")

        foreach(line IN LISTS file_lines)
            # Extract leading whitespace from the current line
            if("${line}" MATCHES "^[ \t]")
                string(REGEX MATCH "^[ \t]*" current_indent ${line})
            else()
                # Reset the indent if it's not a line with leading whitespace
                set(current_indent "")
            endif()

            if("${line}" MATCHES "^[ \t]*```mermaid")
                # Enter a mermaid block, replace the opening marker
                set(modified_content "${modified_content}${current_indent}<pre class=\"mermaid\">\n")
                set(in_mermaid_block TRUE)
            elseif("${line}" MATCHES "^[ \t]*```" AND in_mermaid_block)
                # Close the mermaid block, replace the closing marker
                set(modified_content "${modified_content}${current_indent}</pre>\n")
                set(in_mermaid_block FALSE)
            elseif("${line}" MATCHES "^[ \t]*```[A-za-z0-9+-]+")
                set(modified_content "${modified_content}${current_indent}```\n")
            else()
                # Regular line, add to modified content (including semicolons safely)
                set(modified_content "${modified_content}${line}\n")
            endif()

        endforeach()

        # Write the modified content to the destination path
        file(WRITE ${destination_path} "${modified_content}")

        #message(STATUS "Content written to ${destination_path}:\n${modified_content}")

        # Append the destination path to the output files list
        list(APPEND output_files ${destination_path})
    endforeach()

    # Return the list of output files to the parent scope
    set(${out_files_var} "${output_files}" PARENT_SCOPE)
endfunction()


find_package(Doxygen QUIET)

function(add_target_to_doxygen lib_target)
    if (NOT DOXYGEN_FOUND)
        message(STATUS "No doxygen found, doxygen related targets wont be defined")
        return()
    endif ()

    # Parsing arguments
    set(options)
    set(oneValueArgs)
    set(multiValueArgs INPUT)
    cmake_parse_arguments(PARSE_ARGV 1 arg
            "${options}" "${oneValueArgs}" "${multiValueArgs}"
    )

    # Setup variables to be used as target properties
    if (DEFINED arg_INPUT)
        set(DOXYGEN_INPUT "")
        foreach (input IN LISTS arg_INPUT)
            list(APPEND DOXYGEN_INPUT "${CMAKE_CURRENT_SOURCE_DIR}/${input}")
        endforeach ()
    else ()
        set(DOXYGEN_INPUT "${CMAKE_CURRENT_SOURCE_DIR}/include")
    endif ()

    set(DOXYGEN_PROJECT_NAME "${lib_target}")
    _get_doxygen_output_dir(${lib_target} DOXYGEN_OUTPUT_DIR)
    _get_doxygen_tag_file(${lib_target} DOXYGEN_TAG_FILE)

    file(GLOB_RECURSE DOXYGEN_OTHER_MARKDOWN
            # Use REALTIVE if _md_fix_fenced_blocks is uncommented bellow
            #RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
            LIST_DIRECTORIES false
            "${CMAKE_CURRENT_SOURCE_DIR}/*.md"
    )

#     This is done in DoxygenFileFilter.cmake
#    _md_fix_fenced_blocks("${DOXYGEN_OTHER_MARKDOWN}" "${CMAKE_CURRENT_BINARY_DIR}/doxy" "${CMAKE_CURRENT_SOURCE_DIR}" processed_files)
#    set(DOXYGEN_OTHER_MARKDOWN "${processed_files}")

    # Create definitions of topics/groups for each library (all files in the library are associated with this group)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/${lib_target}.dox" "/** @defgroup ${lib_target} Library ${lib_target} */")
    set(DOXYGEN_OTHER_DOX "${CMAKE_CURRENT_BINARY_DIR}/${lib_target}.dox")

    # Set target properties to be used in add_doxygen_target()
    set_target_properties(${lib_target} PROPERTIES
            DOXYGEN_INPUT "${DOXYGEN_INPUT}"
            #DOXYGEN_README_MD_PATH "${DOXYGEN_README_PATH}"
            DOXYGEN_OTHER_MARKDOWN "${DOXYGEN_OTHER_MARKDOWN}"
            DOXYGEN_OTHER_DOX "${DOXYGEN_OTHER_DOX}"
    )

    if(NOT DEFINED DOXYGEN_TARGETS)
        set(DOXYGEN_TARGETS "${lib_target}" CACHE INTERNAL "List of all Doxygen targets" FORCE)
    else ()
        set(DOXYGEN_TARGETS "${DOXYGEN_TARGETS};${lib_target}"  CACHE INTERNAL "List of all Doxygen targets" FORCE)
    endif()
endfunction()

function(add_doxygen_target target)

    # Parse input args
    set(options)
    set(oneValueArgs)
    set(multiValueArgs EXTRA_INPUT)
    cmake_parse_arguments(PARSE_ARGV 1 arg
            "${options}" "${oneValueArgs}" "${multiValueArgs}"
    )

    set(DOXYGEN_HTML_HEADER "${MODULE_SOURCE_DIR}/Doxygen-header.html")
    separate_arguments(DOXYGEN_TARGETS)
    if (DOXYGEN_TARGETS)
        set(doxygen_targets "")

        set(DOXYGEN_PROJECT_NAME "${CMAKE_PROJECT_NAME}")
        set(DOXYGEN_INPUT "")
        set(DOXYGEN_INPUT_FILTER "")
        set(DOXYGEN_OTHER_MDS "")
        set(DOXYGEN_OTHER_DOXS "")
        set(DOXYGEN_STRIP_FROM_INC_PATH "")
        set(DOXYGEN_OUTPUT_DIR "${MODULE_SOURCE_DIR}/../../doc/doxy/")
        set(DOXYGEN_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
        set(DOXYGEN_HTML_EXTRA_STYLESHEET "${MODULE_SOURCE_DIR}/Doxygen-awesome.css")

        if (DEFINED arg_EXTRA_INPUT)
            foreach (extra_input IN LISTS arg_EXTRA_INPUT)
                if (NOT IS_ABSOLUTE "${extra_input}")
                    set(extra_input "${CMAKE_CURRENT_SOURCE_DIR}/${extra_input}")
                endif ()
                if (extra_input MATCHES "[*?]")
                    file(GLOB_RECURSE extra_input "${extra_input}")
                endif()
                list(APPEND DOXYGEN_INPUT "${extra_input}")
            endforeach ()
        endif ()

        foreach (doxygen_req_target IN LISTS DOXYGEN_TARGETS)
            #get_target_property(BIN_DIR ${doxygen_req_target} BIN_DIR)
            #get_target_property(SRC_DIR ${doxygen_req_target} SRC_DIR)
            get_target_property(INPUT ${doxygen_req_target} DOXYGEN_INPUT)
            get_target_property(OTHER_MARKDOWN ${doxygen_req_target} DOXYGEN_OTHER_MARKDOWN)
            get_target_property(OTHER_DOXS ${doxygen_req_target} DOXYGEN_OTHER_DOX)

            list(APPEND DOXYGEN_INPUT "${INPUT}")
            list(APPEND DOXYGEN_STRIP_FROM_INC_PATH "${INPUT}")
            list(APPEND DOXYGEN_OTHER_MDS "${OTHER_MARKDOWN}")
            list(APPEND DOXYGEN_OTHER_DOXS "${OTHER_DOXS}")
        endforeach ()

        string(REPLACE ";" " " DOXYGEN_INPUT "${DOXYGEN_INPUT};${DOXYGEN_OTHER_MDS};${DOXYGEN_OTHER_DOXS}")
        string(REPLACE ";" " " DOXYGEN_STRIP_FROM_INC_PATH "${DOXYGEN_STRIP_FROM_INC_PATH}")

        if (WIN32)
            set(DOXYGEN_INPUT_FILTER "${MODULE_SOURCE_DIR}/DoxygenFileFilter.bat")
        else ()
            set(DOXYGEN_INPUT_FILTER "${MODULE_SOURCE_DIR}/DoxygenFileFilter.sh")
        endif ()

        file(MAKE_DIRECTORY "${DOXYGEN_OUTPUT_DIR}")
        configure_file("${MODULE_SOURCE_DIR}/Doxyfile.in" "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile" @ONLY)

        add_custom_target("${target}"
                # Use this command instead to debug DoxygenFileFilter.cmake/.bat/.sh
                #   COMMAND doxygen -d  filteroutput Doxyfile
                COMMAND doxygen Doxyfile
                WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        )
    else ()
        message(WARNING "No doxygen targets found, skipping definition of top-level all-doxygens target")
    endif ()
endfunction()
