add_cmake_project(Eigen
    URL "https://gitlab.com/libeigen/eigen/-/archive/3.4.0/eigen-3.4.0.zip"
    URL_HASH SHA256=eba3f3d414d2f8cba2919c78ec6daab08fc71ba2ba4ae502b7e5d4d99fc02cda
    CMAKE_ARGS
      -DCMAKE_POLICY_VERSION_MINIMUM=3.5
)
