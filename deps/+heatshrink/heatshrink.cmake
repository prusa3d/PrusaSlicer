add_cmake_project(heatshrink
    URL https://github.com/atomicobject/heatshrink/archive/refs/tags/v0.4.1.zip
    URL_HASH SHA256=2e2db2366bdf36cb450f0b3229467cbc6ea81a8c690723e4227b0b46f92584fe
    PATCH_COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt ./CMakeLists.txt &&
                  ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_LIST_DIR}/Config.cmake.in ./Config.cmake.in
)