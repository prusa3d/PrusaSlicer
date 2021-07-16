include_glu_libtess = include_directories('include')

libglu_libtess = static_library('glu-libtess',
    'src/dict.c',
    'src/geom.c',
    'src/memalloc.c',
    'src/mesh.c',
    'src/normal.c',
    'src/priorityq.c',
    'src/render.c',
    'src/sweep.c',
    'src/tess.c',
    'src/tessmono.c',
    include_directories: include_glu_libtess,
    dependencies: libm,
)

glu_libtess = declare_dependency(
    link_with: libglu_libtess,
    include_directories: include_glu_libtess,
)
