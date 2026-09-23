# Populate before resource discovery/packaging, and recheck on subsequent builds.
set(ORCA_PROFILES_DESTINATION "${SLIC3R_RESOURCES_DIR}/profiles")
include("${CMAKE_CURRENT_LIST_DIR}/DownloadOrcaProfiles.cmake")
add_custom_target(orca_profiles ALL
    COMMAND "${CMAKE_COMMAND}"
        "-DORCA_PROFILES_DESTINATION=${ORCA_PROFILES_DESTINATION}"
        -P "${CMAKE_CURRENT_LIST_DIR}/DownloadOrcaProfiles.cmake"
    COMMENT "Checking OrcaSlicer profiles"
    VERBATIM)
