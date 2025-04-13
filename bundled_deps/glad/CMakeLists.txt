# This has been created by the Vulkan/GL/GLES/EGL/GLX/WGL Loader-Generator
#
# See https://glad.sh/ for the webservice
#
# Permalink to code:
# https://gen.glad.sh/#generator=c&api=gl%3D4.6%2Cgles2%3D3.2&profile=gl%3Dcompatibility%2Cgles1%3Dcommon&extensions=GL_ARB_compatibility%2CGL_ARB_framebuffer_object%2CGL_EXT_framebuffer_blit%2CGL_EXT_framebuffer_multisample%2CGL_EXT_framebuffer_object%2CGL_EXT_texture_compression_s3tc%2CGL_EXT_texture_filter_anisotropic%2CGL_KHR_debug&options=LOADER
add_library(glad_gl STATIC ${CMAKE_CURRENT_SOURCE_DIR}/src/gl.c)
target_include_directories(
    glad_gl PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)
add_library(GLAD::GL ALIAS glad_gl)

add_library(glad_gles STATIC ${CMAKE_CURRENT_SOURCE_DIR}/src/gl.c)
target_include_directories(
    glad_gles PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)
add_library(GLAD::GLES ALIAS glad_gles)
