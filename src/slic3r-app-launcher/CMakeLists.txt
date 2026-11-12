# Create a slic3r-app-launcher executable
# Process mainfests for various platforms.
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/../platform/msw/slic3r-app-launcher.rc.in ${CMAKE_CURRENT_BINARY_DIR}/slic3r-app-launcher.rc @ONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/../platform/msw/PrusaSlicer.manifest.in ${CMAKE_CURRENT_BINARY_DIR}/slic3r-app-launcher.manifest @ONLY)

add_executable(slic3r-app-launcher
    src/Slic3r/App/Launcher/main.cpp
    src/Slic3r/App/Launcher/ReadCLI.cpp
    src/Slic3r/App/Launcher/ReadCLI.hpp
    src/Slic3r/App/Launcher/SentryScope.hpp
    src/Slic3r/App/Launcher/SentryScope.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/slic3r-app-launcher.rc
)

if (MSVC AND SLIC3R_RELEASE_DEBUG_SYMBOLS)
    target_link_options(slic3r-app-launcher PRIVATE "$<$<CONFIG:RELEASE>:/DEBUG>")
endif()

find_package(CLI11 CONFIG REQUIRED)

target_include_directories(slic3r-app-launcher PRIVATE src)
target_link_libraries(slic3r-app-launcher PRIVATE slic3r-app-cli CLI11::CLI11)

slic3r_add_tracy(slic3r-app-launcher)

if (SLIC3R_SENTRY_DSN)
    find_package(sentry REQUIRED COMPONENTS crashpad)
    target_link_libraries(slic3r-app-launcher PRIVATE sentry::sentry)
    if(TARGET sentry_crashpad::crashpad_handler)
        message(STATUS "SENTRY crashpad handler target present")
        add_custom_command(
                TARGET slic3r-app-launcher POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy
                "$<TARGET_FILE:sentry_crashpad::crashpad_handler>"
                "$<TARGET_FILE_DIR:slic3r-app-launcher>"
                COMMENT "Copying crashpad_handler to build directory"
        )
        install(
            PROGRAMS "$<TARGET_FILE:sentry_crashpad::crashpad_handler>"
            DESTINATION "${CMAKE_INSTALL_BINDIR}"
        )
    else()
        message(STATUS "SENTRY crashpad handler target not present")
    endif ()
endif ()

if (SLIC3R_GUI)
    target_link_libraries(slic3r-app-launcher PRIVATE slic3r-app-desktop)
    target_compile_definitions(slic3r-app-launcher PRIVATE SLIC3R_GUI)

    if (MSVC AND SLIC3R_ENABLE_WEBKIT)
        find_file(WEBVIEW2_DLL_PATH
            NAMES WebView2Loader.dll
            PATH_SUFFIXES bin
        )
        if(NOT WEBVIEW2_DLL_PATH)
            message(FATAL_ERROR "WebView2Loader.dll not found!")
        endif()

        add_custom_command(TARGET slic3r-app-launcher POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy
            "${WEBVIEW2_DLL_PATH}"
            "$<TARGET_FILE_DIR:slic3r-app-launcher>"
        )
    endif()
endif()

if (MSVC)
    # Manifest is provided through PrusaSlicer.rc, don't generate your own.
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /MANIFEST:NO")
endif ()

slic3r_link_resources(slic3r-app-launcher ${SLIC3R_RESOURCES_DIR})
slic3r_app_extract_symbols(slic3r-app-launcher)

if (WIN32)
    prusaslicer_copy_dlls(slic3r-app-launcher)
endif ()

install(TARGETS slic3r-app-launcher RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
