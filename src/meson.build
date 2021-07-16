include_src = include_directories('.')

slic3r_gui_args = get_option('gui') ? [ '-DSLIC3R_GUI', ] : []

subdir('build-utils')

# subdir('avrdude')

subdir('admesh')
# boost/nowide
subdir('boost')
subdir('clipper')
subdir('miniz')
subdir('glu-libtess')
subdir('qhull')
subdir('Shiny')
subdir('semver')
subdir('libigl')
subdir('libnest2d')
subdir('libslic3r')
subdir('platform')


if get_option('gui')
    subdir('imgui')
    subdir('hidapi')

    # if os_windows
    #     message('WXWIN environment set to @0@'.format('TODO'))
    # elif os_linux

    # endif

    subdir('slic3r')
endif



executable('PrusaSlicer',
    'PrusaSlicer.cpp',
    # link_with: libslic3r,
    cpp_args: [
        slic3r_gui_args,
    ],
    include_directories: [
        include_platform,
        include_directories('libslic3r'),
    ],
    link_with: [
        admesh,
        libslic3r_gui,
        semver,
    ],
    dependencies: [
        boost,
        clipper,
        curl,
        # libavrdude,
        libqhull,
        eigen,
        libm,
        libdl,
        libnest2d,
        libslic3r,
        glew,
        tbb,
        libpng,
        expat,
        cgal,
        threads,
        nlopt,
        openvdb,
        openexr,
        opengl,
    ],
)
