add_cmake_project(spdlog
    URL "https://github.com/gabime/spdlog/archive/refs/tags/v1.15.3.zip"
    URL_HASH SHA256=b74274c32c8be5dba70b7006c1d41b7d3e5ff0dff8390c8b6390c1189424e094
    CMAKE_ARGS
        -DSPDLOG_FMT_EXTERNAL=ON
        # This is only relevant for windows, because no one else in the whole world would find useful to use utf16 for filenames
        -DSPDLOG_WCHAR_FILENAMES=ON
        -DSPDLOG_BUILD_EXAMPLE=OFF
)

set(DEP_spdlog_DEPENDS fmt)
