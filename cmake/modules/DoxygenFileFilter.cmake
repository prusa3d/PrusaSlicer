function(echo line)
    execute_process(COMMAND ${CMAKE_COMMAND} -E echo "${line}")
endfunction()

function(_md_filter file_path)
    # Read the entire file as a list of lines (preserving semicolons safely)
    file(STRINGS "${file_path}" file_lines)

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
            set(modified_line "${current_indent}<pre class=\"mermaid\">")
            set(in_mermaid_block TRUE)
        elseif("${line}" MATCHES "^[ \t]*```" AND in_mermaid_block)
            # Close the mermaid block, replace the closing marker
            set(modified_line "${current_indent}</pre>")
            set(in_mermaid_block FALSE)
        elseif("${line}" MATCHES "^[ \t]*```[A-za-z0-9+-]+")
            set(modified_line "${current_indent}```")
        else()
            # Regular line, add to modified content (including semicolons safely)
            set(modified_line "${line}\n")
        endif()
        string(REGEX REPLACE "\n$" "" modified_line "${modified_line}")
        echo("${modified_line}")
    endforeach()

endfunction()

function(_hpp_filter file_path)
    # Doxygen Filter for source files to prepend library group info
    set(src_root "${CMAKE_CURRENT_LIST_DIR}/../../src")

    # Normalize the path to use forward slashes (necessary for Windows)
    file(TO_CMAKE_PATH "${file_path}" INPUT_FILE)
    file(TO_CMAKE_PATH "${src_root}" src_root)

    # Find the relative path of the file with respect to ${src_root}
    file(RELATIVE_PATH REL_PATH_TO_FILE "${src_root}" "${INPUT_FILE}")

    # Extract the first directory (immediate directory under ${src_root})
    string(REPLACE "/" ";" REL_PATH_LIST "${REL_PATH_TO_FILE}")
    list(GET REL_PATH_LIST 0 IMMEDIATE_DIR)

    #message("/**\n * @file\n * @addtogroup ${IMMEDIATE_DIR}\n * @{\n */\n")
    echo("/**\n * @file \"${REL_PATH_TO_FILE}\"\n * @ingroup ${IMMEDIATE_DIR}\n * @{\n */\n")

    #file(READ "${INPUT_FILE}" FILE_CONTENT)
    #message("${FILE_CONTENT}")
    execute_process(COMMAND ${CMAKE_COMMAND} -E cat "${INPUT_FILE}")

    #message("/**\n * @}\n */\n")
    echo("\n/**\n * @}\n */\n\n")


endfunction()


get_filename_component(INPUT_EXT "${INPUT_FILE}" LAST_EXT)
if (INPUT_EXT MATCHES "\\.h(pp)?")
    _hpp_filter("${INPUT_FILE}")
    #execute_process(COMMAND ${CMAKE_COMMAND} -E cat "${INPUT_FILE}")
elseif (INPUT_EXT MATCHES ".md")
    _md_filter("${INPUT_FILE}")
else ()
    execute_process(COMMAND ${CMAKE_COMMAND} -E cat "${INPUT_FILE}")
endif ()
#execute_process(COMMAND ${CMAKE_COMMAND} -E sleep 0.1)
