if (APPLE)
    # This commmand will detect SDK default deployment target
    #execute_process(
    #        COMMAND xcrun --show-sdk-version --sdk macosx
    #        OUTPUT_VARIABLE PREFERRED_DEPLOYMENT_TARGET
    #        OUTPUT_STRIP_TRAILING_WHITESPACE
    #)

    # This is our current deployment target
    SET(PREFERRED_DEPLOYMENT_TARGET "11.0")

    # if explicit deployment target is not set use the preferred one
    if(NOT DEFINED ENV{MACOSX_DEPLOYMENT_TARGET} AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
        message(STATUS "No Deployment Target set. Defaulting to SDK: ${PREFERRED_DEPLOYMENT_TARGET}")
        set(CMAKE_OSX_DEPLOYMENT_TARGET ${PREFERRED_DEPLOYMENT_TARGET} CACHE STRING "" FORCE)
    endif()
endif()
