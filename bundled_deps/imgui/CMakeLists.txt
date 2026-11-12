# increased minimum version for IPO/LTO support
cmake_minimum_required(VERSION 3.13)
project(imgui)

add_library(imgui STATIC
    imgui/imconfig.h
    imgui/imgui.cpp
    imgui/imgui.h
    imgui/imgui_demo.cpp
    imgui/imgui_draw.cpp
    imgui/imgui_internal.h
    imgui/imgui_stdlib.cpp
    imgui/imgui_stdlib.h
    imgui/imgui_tables.cpp
    imgui/imgui_widgets.cpp
    # imgui STB
    imgui/imstb_rectpack.h
    imgui/imstb_textedit.h
    imgui/imstb_truetype.h
)

target_include_directories(imgui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
message(STATUS "Imgui include dir: ${CMAKE_CURRENT_SOURCE_DIR}")


add_library(imgui_backend_ogl3_glfw STATIC
        imgui/backends/imgui_impl_glfw.h
        imgui/backends/imgui_impl_glfw.cpp
        imgui/backends/imgui_impl_opengl3.h
        imgui/backends/imgui_impl_opengl3.cpp
)

target_include_directories(
        imgui_backend_ogl3_glfw PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/imgui
)

if (NOT EMSCRIPTEN)
    # There is conflict with Emscripten Port GLFW
    find_package(glfw3 3.3 REQUIRED)
    target_link_libraries(
            imgui_backend_ogl3_glfw PUBLIC glfw3 OpenGL::GL GLEW::GLEW
    )

    if (APPLE)
        target_link_libraries(imgui_backend_ogl3_glfw PUBLIC "-framework Cocoa" "-framework IOKit")
    endif ()
endif()


add_library(imgui_backend_ogl3_sdl2 STATIC
        imgui/backends/imgui_impl_sdl2.h
        imgui/backends/imgui_impl_sdl2.cpp
        imgui/backends/imgui_impl_opengl3.h
        imgui/backends/imgui_impl_opengl3.cpp
)

target_include_directories(
        imgui_backend_ogl3_sdl2 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/imgui
)

# find_package(SDL2 REQUIRED COMPONENTS SDL2)
if (EMSCRIPTEN)
    find_package(SDL2 REQUIRED)
else ()
    find_package(SDL2 2 REQUIRED)
endif ()

target_link_libraries(
        imgui_backend_ogl3_sdl2 PUBLIC SDL2::SDL2 PlatformGL
)

if (APPLE)
    target_link_libraries(imgui_backend_ogl3_sdl2 PUBLIC "-framework Cocoa" "-framework IOKit")
endif ()

add_library(imgui_backend_ogl3 STATIC
        imgui/backends/imgui_impl_opengl3.h
        imgui/backends/imgui_impl_opengl3.cpp
)

target_include_directories(
        imgui_backend_ogl3
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/imgui
)

target_link_libraries(
        imgui_backend_ogl3 PUBLIC PlatformGL
)

if (APPLE)
    target_link_libraries(imgui_backend_ogl3 PUBLIC "-framework Cocoa" "-framework IOKit")
endif ()
