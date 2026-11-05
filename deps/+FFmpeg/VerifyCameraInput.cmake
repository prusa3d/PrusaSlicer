# FFmpeg may warn and disable an explicitly requested input instead of failing.
# Treat a missing platform camera backend as a dependency configuration error.
file(READ "${FFMPEG_CONFIG}" _ffmpeg_components)
if(NOT _ffmpeg_components MATCHES "#define ${FFMPEG_INPUT} 1")
    message(FATAL_ERROR "FFmpeg camera capture backend ${FFMPEG_INPUT} was disabled. Check its platform dependencies.")
endif()
