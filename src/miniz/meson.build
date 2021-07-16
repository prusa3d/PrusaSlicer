# project('miniz',
#     'c',
#     version: '2.1.0-prusa',
# )

libminiz = static_library('miniz',
    'miniz.c',
)

miniz = declare_dependency(
    version: '2.1.0-prusa',
    link_with: libminiz,
    include_directories: include_directories('.')
)
