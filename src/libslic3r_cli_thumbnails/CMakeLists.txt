#/|/ Copyright (c) Prusa Research 2026 — CLI thumbnail renderer
#/|/
#/|/ PrusaSlicer is released under the terms of the AGPLv3 or higher

set(LIBSLIC3R_CLI_THUMBNAILS_SOURCES
    OffscreenGLContext.hpp
    CLIThumbnailRenderer.hpp
    CLIThumbnailRenderer.cpp
)

if(APPLE)
    list(APPEND LIBSLIC3R_CLI_THUMBNAILS_SOURCES OffscreenGLContext_CGL.cpp)
elseif(WIN32)
    list(APPEND LIBSLIC3R_CLI_THUMBNAILS_SOURCES OffscreenGLContext_WGL.cpp)
elseif(UNIX)
    list(APPEND LIBSLIC3R_CLI_THUMBNAILS_SOURCES OffscreenGLContext_EGL.cpp)
else()
    list(APPEND LIBSLIC3R_CLI_THUMBNAILS_SOURCES OffscreenGLContext_Stub.cpp)
endif()

add_library(libslic3r_cli_thumbnails STATIC ${LIBSLIC3R_CLI_THUMBNAILS_SOURCES})

target_include_directories(libslic3r_cli_thumbnails PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# libslic3r gives us Model / TriangleMesh / BoundingBox / Color / Point plus
# the GCode/ThumbnailData and PrintConfig types we use. No wxWidgets pulled in.
target_link_libraries(libslic3r_cli_thumbnails PUBLIC libslic3r)

# GL function loading: GLEW (the deps bundle already builds it; PrusaSlicer's
# top-level CMakeLists.txt calls find_package(GLEW REQUIRED) unconditionally).
target_link_libraries(libslic3r_cli_thumbnails PUBLIC GLEW::GLEW OpenGL::GL)

# Platform offscreen-context libs.
if(APPLE)
    # CGL lives in the OpenGL framework. OpenGL::GL covers this on macOS; add
    # explicitly for clarity + to silence deprecation warnings on the .cpp file.
    target_compile_options(libslic3r_cli_thumbnails PRIVATE -Wno-deprecated-declarations)
elseif(WIN32)
    # opengl32.lib covers WGL symbols; OpenGL::GL brings it in. gdi32/user32
    # for window creation.
    target_link_libraries(libslic3r_cli_thumbnails PUBLIC gdi32 user32)
elseif(UNIX)
    # EGL is separate from the core OpenGL lib on Linux.
    find_package(OpenGL COMPONENTS EGL)
    if(TARGET OpenGL::EGL)
        target_link_libraries(libslic3r_cli_thumbnails PUBLIC OpenGL::EGL)
    else()
        # Older CMake / Mesa without the EGL component: fall back to plain -lEGL.
        target_link_libraries(libslic3r_cli_thumbnails PUBLIC EGL)
    endif()
endif()

# source_group for IDE clarity
foreach(_source IN ITEMS ${LIBSLIC3R_CLI_THUMBNAILS_SOURCES})
    get_filename_component(_source_path "${_source}" PATH)
    string(REPLACE "/" "\\" _group_path "${_source_path}")
    source_group("${_group_path}" FILES "${_source}")
endforeach()
