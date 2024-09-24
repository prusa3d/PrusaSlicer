cmake_minimum_required(VERSION 2.8.12)
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

target_include_directories(imgui PUBLIC .)
