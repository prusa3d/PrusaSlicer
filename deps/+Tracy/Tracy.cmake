add_cmake_project(Tracy
    URL "https://github.com/wolfpld/tracy/archive/refs/tags/v0.13.1.tar.gz"
    URL_HASH SHA256=d4efc50ebcb0bfcfdbba148995aeb75044c0d80f5d91223aebfaa8fa9e563d2b
    CMAKE_ARGS
        -DTRACY_ENABLE=ON
)
