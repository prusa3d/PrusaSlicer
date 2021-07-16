libclipper = static_library('clipper',
    'clipper_z.cpp',
    'clipper.cpp',
    include_directories: include_src,
)

clipper = declare_dependency(
    link_with: libclipper,
    include_directories: include_directories('.'),
)
