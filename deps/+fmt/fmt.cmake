add_cmake_project(fmt
    URL "https://github.com/fmtlib/fmt/releases/download/12.1.0/fmt-12.1.0.zip"
    URL_HASH SHA256=695fd197fa5aff8fc67b5f2bbc110490a875cdf7a41686ac8512fb480fa8ada7
    EMSCRIPTEN_CMAKE_ARGS
        -DFMT_TEST=OFF
)
