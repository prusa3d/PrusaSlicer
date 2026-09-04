add_cmake_project(
        GLFW
        URL https://github.com/glfw/glfw/releases/download/3.3.9/glfw-3.3.9.zip
        URL_HASH SHA256=55261410f8c3a9cc47ce8303468a90f40a653cd8f25fb968b12440624fb26d08
        EMSCRIPTEN_EXCLUDED ON
        CMAKE_ARGS
        -DBUILD_UTILS=OFF
)