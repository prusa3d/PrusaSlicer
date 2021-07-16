test_name = 'libnest2d'

test_exec = executable(test_name + '_tests',
    test_name + '_tests_main.cpp',
    'printer_parts.cpp',
    dependencies: [
        libnest2d,
        clipper,
    ],
    include_directories: include_test,
    cpp_args: [
        '-DLIBNEST2D_GEOMETRIES_clipper',
        '-DLIBNEST2D_OPTIMIZER_nlopt',
    ],
)

test(test_name, test_exec)
