
opencsg_sources = [
    'main.cpp',
    'Engine.cpp',
    'ShaderCSGDisplay.cpp',

    meson.source_root() / 'src/slic3r/GUI/Jobs/Job.cpp',
    meson.source_root() / 'src/slic3r/GUI/ProgressStatusBar.cpp',
    meson.source_root() / 'src/slic3r/GUI/I18N.cpp',
]

opencsg_example = executable('opencsg_example',
    opencsg_sources,
    dependencies: [
        glew,
        opencsg,
        opengl,
        wxwidgets,
        # Internal dependencies
        libslic3r,
    ],
)
