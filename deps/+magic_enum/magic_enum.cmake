add_cmake_project(magic_enum
        URL "https://github.com/Neargye/magic_enum/archive/refs/tags/v0.9.7.zip"
        URL_HASH SHA256=e293afdaf4d5918bc145903bccff06d28b3ed437f1ac8414ace9e8a769a9e470
        CMAKE_ARGS
        -DMAGIC_ENUM_OPT_BUILD_EXAMPLES=OFF
        -DMAGIC_ENUM_OPT_BUILD_TESTS=OFF
)
