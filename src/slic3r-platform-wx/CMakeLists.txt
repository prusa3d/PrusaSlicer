add_library(slic3r-platform-wx STATIC
    include/Slic3r/App/Platform/WX/WXRenderCanvas.hpp
    src/Slic3r/App/Platform/WX/WXRenderCanvas.cpp
    include/Slic3r/App/Platform/WX/WXMainThreadDispatcher.hpp
    src/Slic3r/App/Platform/WX/WXMainThreadDispatcher.cpp
)

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
    set (wxWidgets_CONFIG_OPTIONS "--toolkit=gtk3")
endif ()
find_package(NanoSVG REQUIRED)

set(WX_COMPONENTS base core adv html gl)
if(SLIC3R_ENABLE_WEBKIT)
    list(APPEND WX_COMPONENTS webview)
endif()
find_package(wxWidgets 3.3 CONFIG REQUIRED COMPONENTS ${WX_COMPONENTS})

slic3r_remap_configs(wx::wxhtml wx::wxadv wx::wxgl wx::wxcore wx::wxbase RelWithDebInfo Release)

target_link_libraries(slic3r-platform-wx PUBLIC
    wxWidgets::wxWidgets
    OpenGL::GL
    GLEW::GLEW
    imgui_backend_ogl3
    slic3r-shared
)
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # The wx libraries are static and their CONFIG package does not propagate
    # what wxGLCanvas needs, so repeat it here. wx guards its Xlib code with
    # GDK_WINDOWING_X11 and its Wayland code with GDK_WINDOWING_WAYLAND, so
    # follow GTK, which installs a pkg-config file per backend it was built for.
    find_package(PkgConfig REQUIRED)
    find_package(OpenGL REQUIRED COMPONENTS EGL)
    target_link_libraries(slic3r-platform-wx PUBLIC OpenGL::EGL)

    pkg_check_modules(GTK3_X11 gtk+-x11-3.0)
    if (GTK3_X11_FOUND)
        # wx core uses Xlib in src/unix/utilsx11.cpp whatever the GL backend is.
        find_package(X11 REQUIRED)
        target_link_libraries(slic3r-platform-wx PUBLIC X11::X11)
    endif ()

    pkg_check_modules(GTK3_WAYLAND gtk+-wayland-3.0)
    if (GTK3_WAYLAND_FOUND)
        pkg_check_modules(WAYLAND REQUIRED IMPORTED_TARGET wayland-client wayland-egl)
        target_link_libraries(slic3r-platform-wx PUBLIC PkgConfig::WAYLAND)
    endif ()
endif ()

target_include_directories(slic3r-platform-wx PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)


# Doxygen
add_target_to_doxygen(slic3r-platform-wx)
slic3r_add_tracy(slic3r-platform-wx)
