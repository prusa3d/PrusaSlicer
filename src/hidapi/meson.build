libhidapi_sources = []

if os_windows
    libhidapi_sources += 'win/hid.c'
elif os_apple
    libhidapi_sources += 'mac/hid.c'
elif os_linux
    libhidapi_sources += 'linux/hid.c'
endif


libhidapi = static_library('hidapi',
    libhidapi_sources,
    include_directories: include_directories('include'),
    dependencies: [
        libdl,
    ],
)

hidapi = declare_dependency(
    link_with: libhidapi,
    include_directories: include_directories('include'),
)
