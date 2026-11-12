# Expects SOURCES_LIST_FILE and STUB_DIR to be passed via -D arguments.

# Read the list of source files from the temporary file
file(STRINGS "${SOURCES_LIST_FILE}" INPUT_FILES)

foreach(input_file IN LISTS INPUT_FILES)
    file(STRINGS "${input_file}" lines)

    # 1. Set the fallback stub name for this specific .cpp file
    # (used if comments appear before any @file directive)
    string(MD5 path_hash "${input_file}")
    get_filename_component(file_name "${input_file}" NAME)
    set(current_stub "${STUB_DIR}/${path_hash}_${file_name}.lua")

    foreach(line IN LISTS lines)
        # Case 1: Detect @file directive and switch the active output scope
        if(line MATCHES "^[ \t]*//--@file[ \t]+([^\r\n \t]+)")
            set(current_stub "${STUB_DIR}/${CMAKE_MATCH_1}")
            continue()

            # Case 2: EmmyLua Annotations (//--)
        elseif(line MATCHES "^[ \t]*//--(.*)")
            file(APPEND "${current_stub}" "---${CMAKE_MATCH_1}\n")

            # Case 3: Raw Lua Code (//-)
        elseif(line MATCHES "^[ \t]*//- ?(.*)")
            file(APPEND "${current_stub}" "${CMAKE_MATCH_1}\n")
        endif()
    endforeach()
endforeach()
