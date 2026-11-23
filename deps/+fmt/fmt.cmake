add_cmake_project(fmt
    URL "https://github.com/fmtlib/fmt/releases/download/12.2.0/fmt-12.2.0.zip"
    URL_HASH SHA256=a2f4a8d51178f954e4c339007f77edd76ba0cb2e36f87a48e5a5403d9be5878f
    EMSCRIPTEN_CMAKE_ARGS
        -DFMT_TEST=OFF
)
