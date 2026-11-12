# Material database (JSON files). Not a library: the archive is only downloaded.
# The PrusaSlicer build later copies it to resources/matdb.
set(_matdb_download_dir ${${PROJECT_NAME}_DEP_DOWNLOAD_DIR}/matdb)
set(_matdb_install_dir ${${PROJECT_NAME}_DEP_INSTALL_PREFIX}/share/matdb)

ExternalProject_Add(dep_matdb
    URL https://slicer-assets.prusa3d.com/matdb.zip
    DOWNLOAD_DIR ${_matdb_download_dir}

    CONFIGURE_COMMAND ""
    BUILD_COMMAND ""

    INSTALL_COMMAND
        ${CMAKE_COMMAND} -E rm -rf ${_matdb_install_dir}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${_matdb_install_dir}
        COMMAND ${CMAKE_COMMAND} -E copy
            ${_matdb_download_dir}/matdb.zip
            ${_matdb_install_dir}/matdb.zip
)
