cmake_minimum_required(VERSION 3.13)
project(PrusaSlicer-native)

add_subdirectory(clipper)
find_package(Qhull 7.2 REQUIRED)
add_library(qhull INTERFACE)
if(SLIC3R_STATIC)
    slic3r_remap_configs("Qhull::qhullcpp;Qhull::qhullstatic_r" RelWithDebInfo Release)
    target_link_libraries(qhull INTERFACE Qhull::qhullcpp Qhull::qhullstatic_r)
else()
    slic3r_remap_configs("Qhull::qhullcpp;Qhull::qhull_r" RelWithDebInfo Release)
    target_link_libraries(qhull INTERFACE Qhull::qhullcpp Qhull::qhull_r)
endif()

add_subdirectory(libslic3r)

if (SLIC3R_ENABLE_FORMAT_STEP)
    add_subdirectory(occt_wrapper)
endif ()

if (SLIC3R_GUI)
    add_subdirectory(libvgcode)

    if(WIN32)
        message(STATUS "WXWIN environment set to: $ENV{WXWIN}")
    elseif(UNIX)
        set(wxWidgets_USE_UNICODE ON)
        if(SLIC3R_STATIC)
            set(wxWidgets_USE_STATIC ON)
        else()
            set(wxWidgets_USE_STATIC OFF)
        endif()
    endif()

    if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set (wxWidgets_CONFIG_OPTIONS "--toolkit=gtk${SLIC3R_GTK}")
    endif ()
    find_package(wxWidgets 3.2 MODULE REQUIRED COMPONENTS base core adv html gl webview)

    include(${wxWidgets_USE_FILE})

    slic3r_remap_configs(wx::wxhtml wx::wxadv wx::wxgl wx::wxcore wx::wxbase RelWithDebInfo Release)

    if(UNIX)
        message(STATUS "wx-config path: ${wxWidgets_CONFIG_EXECUTABLE}")
    endif()

    find_package(JPEG MODULE QUIET)
    find_package(NanoSVG REQUIRED)

    string(REGEX MATCH "wxpng" WX_PNG_BUILTIN ${wxWidgets_LIBRARIES})
    if (PNG_FOUND AND NOT WX_PNG_BUILTIN)
        list(FILTER wxWidgets_LIBRARIES EXCLUDE REGEX png)
        list(APPEND wxWidgets_LIBRARIES ${PNG_LIBRARIES})
    endif ()

    string(REGEX MATCH "wxjpeg" WX_JPEG_BUILTIN ${wxWidgets_LIBRARIES})
    if (JPEG_FOUND AND NOT WX_JPEG_BUILTIN)
        list(FILTER wxWidgets_LIBRARIES EXCLUDE REGEX jpeg)
        list(APPEND wxWidgets_LIBRARIES ${JPEG_LIBRARIES})
    endif ()

    string(REGEX MATCH "wxexpat" WX_EXPAT_BUILTIN ${wxWidgets_LIBRARIES})
    if (EXPAT_FOUND AND NOT WX_EXPAT_BUILTIN)
        list(FILTER wxWidgets_LIBRARIES EXCLUDE REGEX expat)
        list(APPEND wxWidgets_LIBRARIES libexpat)
    endif ()
    
    # This is an issue in the new wxWidgets cmake build, doesn't deal with librt
    find_library(LIBRT rt)
    if(LIBRT)
        list(APPEND wxWidgets_LIBRARIES ${LIBRT})
    endif()

    # This fixes a OpenGL linking issue on OSX. wxWidgets cmake build includes
    # wrong libs for opengl in the link line and it does not link to it by himself.
    # libslic3r_gui will link to opengl anyway, so lets override wx
    list(FILTER wxWidgets_LIBRARIES EXCLUDE REGEX OpenGL)
    
    if (UNIX AND NOT APPLE)
        list(APPEND wxWidgets_LIBRARIES X11 wayland-client wayland-egl EGL)
    endif ()
#    list(REMOVE_ITEM wxWidgets_LIBRARIES oleacc)
    message(STATUS "wx libs: ${wxWidgets_LIBRARIES}")

    add_subdirectory(slic3r)
endif()


# Create a slic3r executable
# Process mainfests for various platforms.
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/platform/msw/PrusaSlicer.rc.in ${CMAKE_CURRENT_BINARY_DIR}/PrusaSlicer.rc @ONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/platform/msw/PrusaSlicer-gcodeviewer.rc.in ${CMAKE_CURRENT_BINARY_DIR}/PrusaSlicer-gcodeviewer.rc @ONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/platform/msw/PrusaSlicer.manifest.in ${CMAKE_CURRENT_BINARY_DIR}/PrusaSlicer.manifest @ONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/platform/osx/Info.plist.in ${CMAKE_CURRENT_BINARY_DIR}/Info.plist @ONLY)
if (WIN32)
    add_library(PrusaSlicer SHARED PrusaSlicer.cpp PrusaSlicer.hpp)
else ()
    add_executable(PrusaSlicer PrusaSlicer.cpp PrusaSlicer.hpp)
endif ()

if (MINGW)
    target_link_options(PrusaSlicer PUBLIC "-Wl,-allow-multiple-definition")
    set_target_properties(PrusaSlicer PROPERTIES PREFIX "")
endif (MINGW)

if (NOT WIN32 AND NOT APPLE)
    # Binary name on unix like systems (Linux, Unix)
    set_target_properties(PrusaSlicer PROPERTIES OUTPUT_NAME "prusa-slicer")
endif ()

target_link_libraries(PrusaSlicer libslic3r libcereal)

if (APPLE)
#    add_compile_options(-stdlib=libc++)
#    add_definitions(-DBOOST_THREAD_DONT_USE_CHRONO -DBOOST_NO_CXX11_RVALUE_REFERENCES -DBOOST_THREAD_USES_MOVE)
    # -liconv: boost links to libiconv by default
    target_link_libraries(PrusaSlicer "-liconv -framework IOKit" "-framework CoreFoundation" -lc++)
elseif (MSVC)
    # Manifest is provided through PrusaSlicer.rc, don't generate your own.
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /MANIFEST:NO")
else ()
    # Boost on Raspberry-Pi does not link to pthreads explicitely.
    target_link_libraries(PrusaSlicer ${CMAKE_DL_LIBS} -lstdc++ Threads::Threads)
endif ()

# Add the Slic3r GUI library, libcurl, OpenGL and GLU libraries.
if (SLIC3R_GUI)
#    target_link_libraries(PrusaSlicer ws2_32 uxtheme setupapi libslic3r_gui ${wxWidgets_LIBRARIES})
target_link_libraries(PrusaSlicer libslic3r_gui)
    if (MSVC)
        # Generate debug symbols even in release mode.
        target_link_options(PrusaSlicer PUBLIC "$<$<CONFIG:RELEASE>:/DEBUG>")
        target_link_libraries(PrusaSlicer user32.lib Setupapi.lib)
    elseif (MINGW)
        target_link_libraries(PrusaSlicer ws2_32 uxtheme setupapi)
    elseif (APPLE)
        target_link_libraries(PrusaSlicer "-framework OpenGL")
    else ()
        target_link_libraries(PrusaSlicer -ldl)
    endif ()
    if (WIN32)
        find_library(PSAPI_LIB NAMES Psapi)
        target_link_libraries(PrusaSlicer ${PSAPI_LIB})
    endif ()
endif ()

# On Windows, a shim application is required to produce a console / non console version of the Slic3r application.
# Also the shim may load the Mesa software OpenGL renderer if the default renderer does not support OpenGL 2.0 and higher.
if (WIN32)
    if (MINGW)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -municode")
    endif()

    add_executable(PrusaSlicer_app_gui WIN32 PrusaSlicer_app_msvc.cpp ${CMAKE_CURRENT_BINARY_DIR}/PrusaSlicer.rc)
    # Generate debug symbols even in release mode.
    if(MSVC)
        target_link_options(PrusaSlicer_app_gui PUBLIC "$<$<CONFIG:RELEASE>:/DEBUG>")
    endif()
    target_compile_definitions(PrusaSlicer_app_gui PRIVATE -DSLIC3R_WRAPPER_NOCONSOLE)
    add_dependencies(PrusaSlicer_app_gui PrusaSlicer)
    set_target_properties(PrusaSlicer_app_gui PROPERTIES OUTPUT_NAME "prusa-slicer")
    target_link_libraries(PrusaSlicer_app_gui PRIVATE boost_headeronly)

    add_executable(PrusaSlicer_app_console PrusaSlicer_app_msvc.cpp ${CMAKE_CURRENT_BINARY_DIR}/PrusaSlicer.rc)
    # Generate debug symbols even in release mode.
    if (MSVC)
        target_link_options(PrusaSlicer_app_console PUBLIC "$<$<CONFIG:RELEASE>:/DEBUG>")
    endif ()
    target_compile_definitions(PrusaSlicer_app_console PRIVATE -DSLIC3R_WRAPPER_CONSOLE)
    add_dependencies(PrusaSlicer_app_console PrusaSlicer)
    set_target_properties(PrusaSlicer_app_console PROPERTIES OUTPUT_NAME "prusa-slicer-console")
    target_link_libraries(PrusaSlicer_app_console PRIVATE boost_headeronly)

    add_executable(PrusaSlicer_app_gcodeviewer WIN32 PrusaSlicer_app_msvc.cpp ${CMAKE_CURRENT_BINARY_DIR}/PrusaSlicer-gcodeviewer.rc)
    # Generate debug symbols even in release mode.
    if (MSVC)
        target_link_options(PrusaSlicer_app_gcodeviewer PUBLIC "$<$<CONFIG:RELEASE>:/DEBUG>")
    endif ()
    target_compile_definitions(PrusaSlicer_app_gcodeviewer PRIVATE -DSLIC3R_WRAPPER_NOCONSOLE -DSLIC3R_WRAPPER_GCODEVIEWER)
    add_dependencies(PrusaSlicer_app_gcodeviewer PrusaSlicer)
    set_target_properties(PrusaSlicer_app_gcodeviewer PROPERTIES OUTPUT_NAME "prusa-gcodeviewer")
    target_link_libraries(PrusaSlicer_app_gcodeviewer PRIVATE boost_headeronly)
endif ()

# Link the resources dir to where Slic3r GUI expects it
if (WIN32)
    if (CMAKE_CONFIGURATION_TYPES)
        foreach (CONF ${CMAKE_CONFIGURATION_TYPES})
            file(TO_NATIVE_PATH "${CMAKE_CURRENT_BINARY_DIR}/${CONF}" WIN_CONF_OUTPUT_DIR)
            file(TO_NATIVE_PATH "${CMAKE_CURRENT_BINARY_DIR}/${CONF}/resources" WIN_RESOURCES_SYMLINK)
            add_custom_command(TARGET PrusaSlicer POST_BUILD
                COMMAND if exist "${WIN_CONF_OUTPUT_DIR}" "("
                        if not exist "${WIN_RESOURCES_SYMLINK}" "("
                            mklink /J "${WIN_RESOURCES_SYMLINK}" "${SLIC3R_RESOURCES_DIR_WIN}"
                        ")"
                    ")"
                COMMENT "Symlinking the resources directory into the build tree"
                VERBATIM
            )

            if (SLIC3R_GUI)
                add_custom_command(TARGET PrusaSlicer POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy
                    "${wxWidgets_ROOT_DIR}/bin/WebView2Loader.dll"
                    "${CMAKE_CURRENT_BINARY_DIR}/${CONF}/WebView2Loader.dll")
            endif()
        endforeach ()
    else ()
        file(TO_NATIVE_PATH "${CMAKE_CURRENT_BINARY_DIR}/resources" WIN_RESOURCES_SYMLINK)
        add_custom_command(TARGET PrusaSlicer POST_BUILD
            COMMAND if not exist "${WIN_RESOURCES_SYMLINK}" "(" mklink /J "${WIN_RESOURCES_SYMLINK}" "${SLIC3R_RESOURCES_DIR_WIN}" ")"
            COMMENT "Symlinking the resources directory into the build tree"
            VERBATIM
        )
        if (SLIC3R_GUI)
            add_custom_command(TARGET PrusaSlicer POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy
                "${wxWidgets_ROOT_DIR}/bin/WebView2Loader.dll"
                "${CMAKE_CURRENT_BINARY_DIR}/${CONF}/WebView2Loader.dll")
        endif()
    endif ()

    # This has to be a separate target due to the windows command line lenght limits
    add_custom_target(PrusaSlicerDllsCopy ALL DEPENDS PrusaSlicer)
    prusaslicer_copy_dlls(PrusaSlicerDllsCopy)

else ()
    if (APPLE)
        # On OSX, the name of the binary matches the name of the Application.
        add_custom_command(TARGET PrusaSlicer POST_BUILD
            COMMAND ln -sf PrusaSlicer prusa-slicer
            COMMAND ln -sf PrusaSlicer prusa-gcodeviewer
            COMMAND ln -sf PrusaSlicer PrusaGCodeViewer
            WORKING_DIRECTORY "$<TARGET_FILE_DIR:PrusaSlicer>"
            COMMENT "Symlinking the G-code viewer to PrusaSlicer, symlinking to prusa-slicer and prusa-gcodeviewer"
            VERBATIM)
    else ()
        add_custom_command(TARGET PrusaSlicer POST_BUILD
            COMMAND ln -sf prusa-slicer prusa-gcodeviewer
            WORKING_DIRECTORY "$<TARGET_FILE_DIR:PrusaSlicer>"
            COMMENT "Symlinking the G-code viewer to PrusaSlicer"
            VERBATIM)
    endif ()
    if (XCODE)
        # Because of Debug/Release/etc. configurations (similar to MSVC) the slic3r binary is located in an extra level
        set(BIN_RESOURCES_DIR "${CMAKE_CURRENT_BINARY_DIR}/resources")
    else ()
        set(BIN_RESOURCES_DIR "${CMAKE_CURRENT_BINARY_DIR}/../resources")
    endif ()
    add_custom_command(TARGET PrusaSlicer POST_BUILD
        COMMAND ln -sfn "${SLIC3R_RESOURCES_DIR}" "${BIN_RESOURCES_DIR}"
        COMMENT "Symlinking the resources directory into the build tree"
        VERBATIM)
endif ()

# Slic3r binary install target
if (WIN32)
    install(TARGETS PrusaSlicer RUNTIME DESTINATION "${CMAKE_INSTALL_PREFIX}")
    if (MSVC)
        install(TARGETS PrusaSlicer_app_gui RUNTIME DESTINATION "${CMAKE_INSTALL_PREFIX}")
        install(TARGETS PrusaSlicer_app_console RUNTIME DESTINATION "${CMAKE_INSTALL_PREFIX}")
    endif ()
else ()
    install(TARGETS PrusaSlicer RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

    # Install the symlink for gcodeviewer
    install(CODE "execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink prusa-slicer prusa-gcodeviewer WORKING_DIRECTORY \$ENV{DESTDIR}/${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR})")
endif ()
