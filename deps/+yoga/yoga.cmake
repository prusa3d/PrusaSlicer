add_cmake_project(yoga
  URL "https://github.com/facebook/yoga/archive/refs/tags/v3.2.2.zip"
  URL_HASH SHA256=9d223002452981280702e70fab7e2468bd4189a5d4083b39e46f8902616b4d71
  PATCH_COMMAND ${PATCH_CMD} ${CMAKE_CURRENT_LIST_DIR}/yoga.patch
)
