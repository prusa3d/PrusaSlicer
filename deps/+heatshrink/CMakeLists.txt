cmake_minimum_required(VERSION 3.10)

project(heatshrink LANGUAGES C VERSION 0.4.1)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

add_library(${PROJECT_NAME} heatshrink_decoder.c heatshrink_encoder.c)
add_library(${PROJECT_NAME}_dynalloc heatshrink_decoder.c heatshrink_encoder.c)

find_library(MATH_LIBRARY m) # Business as usual
if(MATH_LIBRARY)
    target_link_libraries(${PROJECT_NAME} PUBLIC ${MATH_LIBRARY})
endif()

target_include_directories(${PROJECT_NAME} PUBLIC $<INSTALL_INTERFACE:include>)
target_include_directories(${PROJECT_NAME}_dynalloc PUBLIC $<INSTALL_INTERFACE:include>)

target_compile_definitions(${PROJECT_NAME} PUBLIC HEATSHRINK_DYNAMIC_ALLOC=0)
target_compile_definitions(${PROJECT_NAME}_dynalloc PUBLIC HEATSHRINK_DYNAMIC_ALLOC=1)

if (UNIX)
  add_executable(${PROJECT_NAME}_cmd heatshrink.c)
  target_link_libraries(${PROJECT_NAME}_cmd ${PROJECT_NAME}_dynalloc)
  set_target_properties(${PROJECT_NAME}_cmd PROPERTIES OUTPUT_NAME ${PROJECT_NAME})
endif ()

foreach (tgt ${PROJECT_NAME} ${PROJECT_NAME}_dynalloc)
  set_target_properties(${tgt}
    PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION})
endforeach()

# Installation and export:

include(CMakePackageConfigHelpers)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
)

set(_exported_targets ${PROJECT_NAME} ${PROJECT_NAME}_dynalloc)
if (UNIX)
  list(APPEND _exported_targets ${PROJECT_NAME}_cmd)
endif ()

install(TARGETS ${_exported_targets}
        EXPORT ${PROJECT_NAME}Targets
)

export(EXPORT ${PROJECT_NAME}Targets 
       FILE "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Targets.cmake" 
       NAMESPACE ${PROJECT_NAME}::
)

include(GNUInstallDirs)
set(ConfigPackageLocation ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME})

configure_package_config_file(${CMAKE_CURRENT_SOURCE_DIR}/Config.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake"
  INSTALL_DESTINATION ${ConfigPackageLocation}
)

install(
    FILES
      heatshrink_common.h
      heatshrink_config.h
      heatshrink_encoder.h
      heatshrink_decoder.h
    DESTINATION
      include/${PROJECT_NAME}
  )

install(EXPORT ${PROJECT_NAME}Targets
  FILE
    ${PROJECT_NAME}Targets.cmake
  NAMESPACE
    ${PROJECT_NAME}::
  DESTINATION
    ${ConfigPackageLocation}
)

install(
  FILES
    "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake"
  DESTINATION
    ${ConfigPackageLocation}
)