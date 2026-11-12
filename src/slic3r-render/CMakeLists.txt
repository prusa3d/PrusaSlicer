set(SLIC3R_RENDER_FILES
        include/Slic3r/App/Render/ScreenInfo.hpp
        include/Slic3r/App/Render/Types.hpp
        src/Slic3r/App/Render/Types.cpp
        src/Slic3r/App/Render/Init.cpp
        include/Slic3r/App/Render/Init.hpp
        include/Slic3r/App/Render/Context.hpp
        src/Slic3r/App/Render/Context.cpp
        include/Slic3r/App/Render/Device.hpp
        src/Slic3r/App/Render/Device.cpp
        include/Slic3r/App/Render/ShaderManager.hpp
        src/Slic3r/App/Render/ShaderManager_gl.cpp
        include/Slic3r/App/Render/ModelManager.hpp
        include/Slic3r/App/Render/Model.hpp
        include/Slic3r/App/Render/Texture.hpp
        src/Slic3r/App/Render/Texture_gl.cpp
        include/Slic3r/App/Render/Buffer.hpp
        src/Slic3r/App/Render/Buffer_gl.cpp
        include/Slic3r/App/Render/Renderbuffer.hpp
        src/Slic3r/App/Render/Renderbuffer_gl.cpp
        include/Slic3r/App/Render/Shader.hpp
        src/Slic3r/App/Render/Shader_gl.cpp
        include/Slic3r/App/Render/UniformValue.hpp
        src/Slic3r/App/Render/UniformValue.cpp
        include/Slic3r/App/Render/Geometry.hpp
        src/Slic3r/App/Render/Geometry_gl.cpp
        include/Slic3r/App/Render/GeometryBuilder.hpp
        src/Slic3r/App/Render/GeometryBuilder.cpp
        src/Slic3r/App/Render/TextureManager.cpp
        include/Slic3r/App/Render/TextureManager.hpp
        src/Slic3r/App/Render/TextureBufferManager.cpp
        include/Slic3r/App/Render/TextureBufferManager.hpp
        src/Slic3r/App/Render/ImageCodec.cpp
        include/Slic3r/App/Render/ImageCodec.hpp
        include/Slic3r/App/Render/CommandBuffer.hpp
        src/Slic3r/App/Render/CommandBuffer.cpp
        src/Slic3r/App/Render/CommandBuffer_gl.cpp
        src/Slic3r/App/Render/VertexAttribDesc.cpp
        include/Slic3r/App/Render/VertexAttribDesc.hpp
        src/Slic3r/App/Render/MathUtils.cpp
        include/Slic3r/App/Render/MathUtils.hpp
        include/Slic3r/App/Render/ImguiRender.hpp
        src/Slic3r/App/Render/ImguiRender.cpp
        include/Slic3r/App/Render/ImguiTypes.hpp
        include/Slic3r/App/Render/ImguiFontHelper.hpp
        src/Slic3r/App/Render/ImguiFontHelper.cpp
        include/Slic3r/App/Render/ImguiIconHelper.hpp
        src/Slic3r/App/Render/ImguiIconHelper.cpp
        include/Slic3r/App/Render/WithInternal.hpp
        src/Slic3r/App/Render/GL/GLTypes.cpp
        include/Slic3r/App/Render/GL/GLTypes.hpp
        include/Slic3r/App/Render/GL/commonGL.hpp
        src/Slic3r/App/Render/GL/commonGL.cpp
        include/Slic3r/App/Render/GL/GLTextureInternal.hpp
        include/Slic3r/App/Render/GL/GLBufferInternal.hpp
        include/Slic3r/App/Render/GL/GLRenderbufferInternal.hpp
        include/Slic3r/App/Render/GL/GLGeometryInternal.hpp
        include/Slic3r/App/Render/GL/GLShaderInternal.hpp
        include/Slic3r/App/Render/GL/GLDeviceInternal.hpp
        src/Slic3r/App/Render/GL/GLDeviceInternal.cpp
        include/Slic3r/App/Render/ResourceManager.hpp
        include/Slic3r/App/Render/GeometryManager.hpp
        include/Slic3r/App/Render/Material.hpp
        src/Slic3r/App/Render/Material.cpp
        include/Slic3r/App/Render/DrawCommand.hpp
        include/Slic3r/App/Render/DynamicGeometry.hpp
        include/Slic3r/App/Render/ScopedDebugGroup.hpp
        src/Slic3r/App/Render/ScopedDebugGroup.cpp
        include/Slic3r/App/Render/Framebuffer.hpp
        src/Slic3r/App/Render/Framebuffer_gl.cpp
        include/Slic3r/App/Render/GL/GLFramebufferInternal.hpp
        src/Slic3r/App/Render/FramebufferManager.cpp
        include/Slic3r/App/Render/FramebufferManager.hpp
        include/Slic3r/App/Render/IResourceResolver.hpp
        src/Slic3r/App/Render/TextImageGenerator.cpp
        include/Slic3r/App/Render/TextImageGenerator.hpp

        include/Slic3r/Memory.hpp
)

find_package(NanoSVG REQUIRED)
add_library(slic3r-render STATIC ${SLIC3R_RENDER_FILES})
target_link_libraries(slic3r-render PUBLIC
        PlatformGL
        imgui
        imgui_backend_ogl3
        slic3r-biz-algorithms
        slic3r-domain
        NanoSVG::nanosvg
        NanoSVG::nanosvgrast
        Boost::filesystem
        fmt::fmt
)
if (EMSCRIPTEN)
#    target_link_libraries(slic3r-render PUBLIC png-mt )
else()
    find_package(PNG REQUIRED)
    target_link_libraries(slic3r-render PUBLIC PNG::PNG)
    find_package(JPEG REQUIRED)
    target_link_libraries(slic3r-render PUBLIC JPEG::JPEG)
endif ()
target_include_directories(slic3r-render PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

if (NOT SLIC3R_OPENGL_ES)
    target_compile_definitions(slic3r-render PUBLIC SLIC3R_RENDER_TEXTURE_BUFFER_SUPPORTED)
endif ()

# Doxygen
add_target_to_doxygen(slic3r-render)
