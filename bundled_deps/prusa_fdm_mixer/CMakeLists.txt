add_library(prusa_fdm_mixer STATIC
    prusa_fdm_mixer.cpp
    prusa_fdm_mixer.hpp
)
target_include_directories(prusa_fdm_mixer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(prusa_fdm_mixer PUBLIC cxx_std_17)
