# Minimal, statically linked RTSP video support. No external player is required.
find_program(FFMPEG_SHELL NAMES sh bash)
find_program(FFMPEG_MAKE NAMES gmake make)
if(NOT FFMPEG_SHELL OR NOT FFMPEG_MAKE)
    message(FATAL_ERROR "Building FFmpeg requires a POSIX shell and GNU make (MSYS2 on Windows).")
endif()

set(_ffmpeg_platform)
if(APPLE)
    list(APPEND _ffmpeg_platform --enable-indev=avfoundation
        --enable-avfoundation)
    set(_ffmpeg_input CONFIG_AVFOUNDATION_INDEV)
elseif(WIN32)
    list(APPEND _ffmpeg_platform --enable-indev=dshow)
    set(_ffmpeg_input CONFIG_DSHOW_INDEV)
else()
    list(APPEND _ffmpeg_platform --enable-indev=v4l2)
    set(_ffmpeg_input CONFIG_V4L2_INDEV)
endif()
if(APPLE)
    execute_process(COMMAND xcrun --show-sdk-path OUTPUT_VARIABLE _ffmpeg_sdk OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(CMAKE_OSX_SYSROOT)
        set(_ffmpeg_sdk ${CMAKE_OSX_SYSROOT})
    endif()
    set(_ffmpeg_arch ${CMAKE_OSX_ARCHITECTURES})
    if(NOT _ffmpeg_arch)
        set(_ffmpeg_arch ${CMAKE_SYSTEM_PROCESSOR})
    endif()
    list(APPEND _ffmpeg_platform --target-os=darwin --arch=${_ffmpeg_arch}
        "--extra-cflags=-arch ${_ffmpeg_arch} -isysroot ${_ffmpeg_sdk} -mmacosx-version-min=${DEP_OSX_TARGET}"
        "--extra-ldflags=-arch ${_ffmpeg_arch} -isysroot ${_ffmpeg_sdk} -mmacosx-version-min=${DEP_OSX_TARGET}"
        "--host-cflags=-isysroot ${_ffmpeg_sdk}"
        "--host-ldflags=-isysroot ${_ffmpeg_sdk}")
endif()
if(MSVC)
    list(APPEND _ffmpeg_platform --toolchain=msvc --target-os=win64 --cc=cl --cxx=cl)
else()
    list(APPEND _ffmpeg_platform --cc=${CMAKE_C_COMPILER} --cxx=${CMAKE_CXX_COMPILER})
endif()

ExternalProject_Add(dep_FFmpeg
    URL https://github.com/FFmpeg/FFmpeg/archive/refs/tags/n8.1.3.tar.gz
    URL_HASH SHA256=09f990289327d3ebfedf4fc15cae3f884a65dab7a5aac7bbb5b01ff83900efd9
    DOWNLOAD_DIR ${${PROJECT_NAME}_DEP_DOWNLOAD_DIR}/FFmpeg
    PATCH_COMMAND ${PATCH_CMD} ${CMAKE_CURRENT_LIST_DIR}/avfoundation-interrupt.patch
    BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/builds/FFmpeg
    CONFIGURE_COMMAND ${FFMPEG_SHELL} <SOURCE_DIR>/configure
        --prefix=${${PROJECT_NAME}_DEP_INSTALL_PREFIX}
        --disable-everything --disable-autodetect --disable-programs --disable-doc
        --disable-shared --enable-static --enable-pic
        --disable-gpl --disable-nonfree --disable-x86asm
        --enable-avdevice --disable-avfilter --disable-swresample
        --disable-videotoolbox --disable-audiotoolbox --disable-iconv
        --disable-zlib --disable-bzlib --disable-lzma
        --enable-network --enable-avformat --enable-avcodec --enable-avutil --enable-swscale
        --enable-protocol=file,tcp,udp,rtp,http
        --enable-demuxer=rtsp,rtp,sdp,h264,hevc,mjpeg,mpjpeg,yuv4mpegpipe
        --enable-parser=h264,hevc,mjpeg
        --enable-decoder=h264,hevc,mjpeg,rawvideo
        ${_ffmpeg_platform}
        COMMAND ${CMAKE_COMMAND}
            -DFFMPEG_CONFIG=<BINARY_DIR>/config_components.h
            -DFFMPEG_INPUT=${_ffmpeg_input}
            -P ${CMAKE_CURRENT_LIST_DIR}/VerifyCameraInput.cmake
    BUILD_COMMAND ${FFMPEG_MAKE} -j3
    INSTALL_COMMAND ${FFMPEG_MAKE} install)
