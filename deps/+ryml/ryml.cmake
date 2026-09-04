add_cmake_project(ryml
        URL "https://github.com/biojppm/rapidyaml/releases/download/v0.10.0/rapidyaml-0.10.0-src.tgz"
        URL_HASH SHA256=54eb1050789809a26c780f80857b7668a5b3123405d6514a65d733e4292c690b
        CMAKE_ARGS
          -DCMAKE_POLICY_VERSION_MINIMUM=3.5
)
