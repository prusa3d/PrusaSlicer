cmake_minimum_required(VERSION 3.13)
project(OCCTWrapper)

if (APPLE)
    # TODO: we need to fix notarization with the separate shared library
    add_library(OCCTWrapper STATIC OCCTWrapper.cpp)
else ()
    add_library(OCCTWrapper MODULE OCCTWrapper.cpp)
endif ()

set_target_properties(OCCTWrapper
    PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/src"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/src"
    PREFIX ""
)

include(GenerateExportHeader)

generate_export_header(OCCTWrapper)

find_package(OpenCASCADE REQUIRED)
# OpenCASCADE has an exact version match even if you don't specify the EXACT
# keyword in the find_package(). So lets implement it on our own.
set(OPENCASCADE_MODULE_VERSION
    "${OpenCASCADE_MAJOR_VERSION}.${OpenCASCADE_MINOR_VERSION}.${OpenCASCADE_MAINTENANCE_VERSION}")
set(OPENCASCADE_REQUIRED_VERSION "7.8.0")
if (${OPENCASCADE_MODULE_VERSION} VERSION_LESS ${OPENCASCADE_REQUIRED_VERSION})
    message(
        FATAL_ERROR
        "Coun't find a compatible OpenCASCADE version - "
        "required: ${OPENCASCADE_REQUIRED_VERSION}, found: "
        "${OPENCASCADE_MODULE_VERSION}"
    )
endif()

set(OCCT_LIBS
    TKDESTEP
)

slic3r_remap_configs("${OCCT_LIBS}" RelWithDebInfo Release)

target_include_directories(OCCTWrapper PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_include_directories(OCCTWrapper PUBLIC ${OpenCASCADE_INCLUDE_DIR})
target_link_libraries(OCCTWrapper ${OCCT_LIBS})
target_link_libraries(OCCTWrapper libslic3r admesh)

include(GNUInstallDirs)

if (WIN32 OR APPLE)
    install(TARGETS OCCTWrapper DESTINATION "${CMAKE_INSTALL_BINDIR}")
else()
    install(TARGETS OCCTWrapper DESTINATION "${CMAKE_INSTALL_LIBDIR}")
endif()
