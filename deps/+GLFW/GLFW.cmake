add_cmake_project(
        GLFW
        URL https://github.com/glfw/glfw/releases/download/3.5.1/glfw-3.5.1.zip
        URL_HASH SHA256=ea79bc5feffc254c87291980c2d0bce9acebb68c4983b79f961dcd2cb8a611a0
        EMSCRIPTEN_EXCLUDED ON
        CMAKE_ARGS
        -DBUILD_UTILS=OFF
)
