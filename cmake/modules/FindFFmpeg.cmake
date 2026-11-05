# Locate the small FFmpeg library bundle built by deps/+FFmpeg.
find_path(FFmpeg_INCLUDE_DIR NAMES libavformat/avformat.h)
foreach(_component avdevice avformat avcodec swscale avutil)
    find_library(FFmpeg_${_component}_LIBRARY NAMES ${_component})
endforeach()
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(FFmpeg REQUIRED_VARS FFmpeg_INCLUDE_DIR
    FFmpeg_avdevice_LIBRARY FFmpeg_avformat_LIBRARY FFmpeg_avcodec_LIBRARY FFmpeg_swscale_LIBRARY FFmpeg_avutil_LIBRARY)
if(FFmpeg_FOUND AND NOT TARGET FFmpeg::FFmpeg)
    find_package(Threads REQUIRED)
    add_library(FFmpeg::FFmpeg INTERFACE IMPORTED)
    set_target_properties(FFmpeg::FFmpeg PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${FFmpeg_INCLUDE_DIR}"
        INTERFACE_LINK_LIBRARIES "${FFmpeg_avdevice_LIBRARY};${FFmpeg_avformat_LIBRARY};${FFmpeg_avcodec_LIBRARY};${FFmpeg_swscale_LIBRARY};${FFmpeg_avutil_LIBRARY};Threads::Threads")
    if(WIN32)
        set_property(TARGET FFmpeg::FFmpeg APPEND PROPERTY INTERFACE_LINK_LIBRARIES ws2_32 bcrypt psapi ole32 strmiids uuid oleaut32 shlwapi)
    elseif(APPLE)
        set_property(TARGET FFmpeg::FFmpeg APPEND PROPERTY INTERFACE_LINK_LIBRARIES
            "-framework AVFoundation" "-framework Foundation" "-framework CoreMedia"
            "-framework CoreVideo" "-framework CoreAudio" "-framework CoreGraphics" m)
    elseif(UNIX)
        set_property(TARGET FFmpeg::FFmpeg APPEND PROPERTY INTERFACE_LINK_LIBRARIES m)
    endif()
endif()
mark_as_advanced(FFmpeg_avdevice_LIBRARY FFmpeg_INCLUDE_DIR FFmpeg_avformat_LIBRARY FFmpeg_avcodec_LIBRARY
    FFmpeg_swscale_LIBRARY FFmpeg_avutil_LIBRARY)
