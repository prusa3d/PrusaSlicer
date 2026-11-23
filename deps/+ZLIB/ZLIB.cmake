add_cmake_project(ZLIB
  URL https://github.com/madler/zlib/releases/download/v1.4/zlib14.zip
  URL_HASH SHA256=38bc7ed1c8ac3c870a159623deacf74a46c393ae1ad23f1dca1ed8e272deb058
  CMAKE_ARGS
    -DSKIP_INSTALL_FILES=ON         # Prevent installation of man pages et al.
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    -DZLIB_BUILD_SHARED=OFF
    -DZLIB_BUILD_STATIC=ON
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5
)

