#///////////////////////////////////////////////////////////////////////////
#//-------------------------------------------------------------------------
#//
#// Description:
#//      cmake module for finding libnoise installation
#//      Used for advanced fuzzy skin noise patterns (Perlin, Billow, etc.)
#//
#//      following variables are defined:
#//      libnoise_FOUND         - whether libnoise was found
#//      libnoise_INCLUDE_DIR   - libnoise header directory
#//      libnoise_LIBRARY       - libnoise library file
#//
#//      Example usage:
#//          find_package(libnoise REQUIRED)
#//          target_link_libraries(myapp libnoise::libnoise)
#//
#//-------------------------------------------------------------------------

set(libnoise_FOUND FALSE)
set(libnoise_ERROR_REASON "")

# Try to find the header
find_path(libnoise_INCLUDE_DIR
    NAMES libnoise/noise.h
    PATH_SUFFIXES include
)

# Try to find the library
find_library(libnoise_LIBRARY
    NAMES libnoise_static libnoise noise
    PATH_SUFFIXES lib lib64
)

# Check if we found everything
if(libnoise_INCLUDE_DIR AND libnoise_LIBRARY)
    set(libnoise_FOUND TRUE)
else()
    if(NOT libnoise_INCLUDE_DIR)
        set(libnoise_ERROR_REASON "${libnoise_ERROR_REASON} Cannot find libnoise header 'libnoise/noise.h'.")
    endif()
    if(NOT libnoise_LIBRARY)
        set(libnoise_ERROR_REASON "${libnoise_ERROR_REASON} Cannot find libnoise library.")
    endif()
endif()

# Make variables changeable
mark_as_advanced(
    libnoise_INCLUDE_DIR
    libnoise_LIBRARY
)

# Report result and create imported target
if(libnoise_FOUND)
    message(STATUS "Found libnoise: ${libnoise_LIBRARY}")
    message(STATUS "Using libnoise include directory: ${libnoise_INCLUDE_DIR}")

    if(NOT TARGET libnoise::libnoise)
        add_library(libnoise::libnoise STATIC IMPORTED)
        set_target_properties(libnoise::libnoise PROPERTIES
            IMPORTED_LOCATION "${libnoise_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${libnoise_INCLUDE_DIR}"
        )
    endif()
else()
    if(libnoise_FIND_REQUIRED)
        message(FATAL_ERROR "Unable to find required libnoise installation:${libnoise_ERROR_REASON}")
    else()
        if(NOT libnoise_FIND_QUIETLY)
            message(STATUS "libnoise was not found:${libnoise_ERROR_REASON}")
        endif()
    endif()
endif()
