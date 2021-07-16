include_libnest2d = include_directories('include')
libnest2d_sources = [
    'src/libnest2d.cpp',
]

lib_libnest2d = static_library('nest2d',
    libnest2d_sources,
    include_directories: include_libnest2d,
    cpp_args: [
        '-DLIBNEST2D_GEOMETRIES_clipper',
        '-DLIBNEST2D_OPTIMIZER_nlopt',
        '-DLIBNEST2D_STATIC',
        '-DLIBNEST2D_THREADING_tbb',
    ],
    dependencies: [
        clipper,
        # tbb,
        # nlopt,
    ],
)

libnest2d = declare_dependency(
    link_with: lib_libnest2d,
    include_directories: include_libnest2d,
)
