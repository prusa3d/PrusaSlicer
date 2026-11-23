add_cmake_project(spdlog
    URL "https://github.com/gabime/spdlog/archive/refs/tags/v1.17.0.zip"
    URL_HASH SHA256=b11912a82d149792fef33fabd0503b13d54aeac25c1464755461d4108ea71fc2
    CMAKE_ARGS
        -DSPDLOG_FMT_EXTERNAL=ON
        # This is only relevant for windows, because no one else in the whole world would find useful to use utf16 for filenames
        -DSPDLOG_WCHAR_FILENAMES=ON
        -DSPDLOG_BUILD_EXAMPLE=OFF
)

set(DEP_spdlog_DEPENDS fmt)
