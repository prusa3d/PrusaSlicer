libimgui = static_library('imgui',
    'imgui.cpp',
    'imgui_demo.cpp',
    'imgui_draw.cpp',
    'imgui_widgets.cpp',
)


imgui = declare_dependency(
    link_with: libimgui,
    include_directories: include_directories('.'),
)
