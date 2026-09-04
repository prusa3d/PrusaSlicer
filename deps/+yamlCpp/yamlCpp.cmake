add_cmake_project(yamlCpp
        URL "https://github.com/jbeder/yaml-cpp/releases/download/yaml-cpp-0.9.0/yaml-cpp-yaml-cpp-0.9.0.tar.gz"
        URL_HASH SHA256=298593d9c440fd9034b8b193d96318b76d49bc97c6ceadb7b0836edf0b6d7539
        CMAKE_ARGS
          -DCMAKE_POLICY_VERSION_MINIMUM=3.5
)
