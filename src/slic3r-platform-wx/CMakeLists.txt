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
target_include_directories(slic3r-platform-wx PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)


# Doxygen
add_target_to_doxygen(slic3r-platform-wx)
slic3r_add_tracy(slic3r-platform-wx)
