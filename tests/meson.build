test_data_dir = meson.current_source_dir() / 'data'


# TODO Add individual tests as executables in separate directories
# add_subirectory(<testcase>)

catch2 = dependency('catch2')
if os_apple
    # -DCATCH_CONFIG_NO_CPP17_UNCAUGHT_EXCEPTIONS
endif

include_test = include_directories('.')


# add_library(test_common INTERFACE)
# target_compile_definitions(test_common INTERFACE TEST_DATA_DIR=R"\(${TEST_DATA_DIR}\)" CATCH_CONFIG_FAST_COMPILE)
# target_link_libraries(test_common INTERFACE Catch2::Catch2)

# if (APPLE)
#     target_link_libraries(test_common INTERFACE "-liconv -framework IOKit" "-framework CoreFoundation" -lc++)
# endif()

# set_property(GLOBAL PROPERTY USE_FOLDERS ON)

subdir('libnest2d')
# subdir('libslic3r')
# subdir('slic3rutils')
# subdir('fff_print')
# subdir('sla_print')
# subdir('cpp17')     # does not have to be built all the time
# subdir('example')
