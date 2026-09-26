if(SLIC3R_BUILD_TESTS)
    find_package(Catch2 3.8 REQUIRED)
    slic3r_remap_configs("Catch2::Catch2" RelWithDebInfo Release)

    set(CATCH_EXTRA_ARGS "" CACHE STRING "Extra arguments for Catch2 test suites.")
    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(CATCH_ALL_ARGS "~[timeout]")
    endif()
    list(APPEND CATCH_ALL_ARGS ${CATCH_EXTRA_ARGS})
endif()

add_subdirectory(slic3r-base)
add_subdirectory(slic3r-domain)
add_subdirectory(slic3r-biz-algorithms)
add_subdirectory(slic3r-biz-cgal-algorithms)
add_subdirectory(slic3r-biz-crypto)
add_subdirectory(slic3r-biz-parser)
add_subdirectory(slic3r-gcode-reader)
add_subdirectory(libpgcode)
add_subdirectory(slic3r-jthread)
add_subdirectory(libslic3r)
add_subdirectory(slic3r-biz-arrange)
#add_subdirectory(libseqarrange)
add_subdirectory(slic3r-biz-lua)

if (SLIC3R_ENABLE_FORMAT_STEP)
    add_subdirectory(occt_wrapper)
endif ()

if (SLIC3R_GUI)
    # TODO: The following two are GUI only for now, before we can build these
    # targets without GUI dependencies. It requires splitting slic3r-shared first.
    add_subdirectory(slic3r-app-cli)      
    add_subdirectory(slic3r-app-launcher) 

    add_subdirectory(slic3r-render)
    add_subdirectory(slic3r-platform)
    add_subdirectory(libvgcode)
    add_subdirectory(slic3r-shared)
    if (NOT EMSCRIPTEN)
        add_subdirectory(slic3r-platform-wx)
        add_subdirectory(slic3r-app-desktop)
        add_subdirectory(slic3r-shared-wx)
    endif()
else()
    message(FATAL_ERROR "Non-GUI build is not supported yet.")
endif ()

if (SLIC3R_BUILD_TESTS)
    add_subdirectory(slic3r-test-utils)
endif ()

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/platform/osx/Info.plist.in ${CMAKE_CURRENT_BINARY_DIR}/Info.plist @ONLY)

add_doxygen_target(slic3r-doxygen
    EXTRA_INPUT ../doc/Doxygen.md ../doc/CodeStyle.md
)

add_lua_docs_target(slic3r-lua-docs)
