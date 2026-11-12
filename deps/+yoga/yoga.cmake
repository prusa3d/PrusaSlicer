add_cmake_project(yoga
  URL "https://github.com/facebook/yoga/archive/refs/tags/v3.1.0.zip"
  URL_HASH SHA256=8beed938026292f3a5d34b9b8dbe9e8b6c1e90c16292a37ff1f7240dbcabe5f2
  PATCH_COMMAND ${PATCH_CMD} ${CMAKE_CURRENT_LIST_DIR}/yoga.patch
)
