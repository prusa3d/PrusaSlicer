#pragma once

#include "Slic3r/Domain/PixelFormat.hpp"
#include "commonGL.hpp"
#include "Slic3r/App/Render/Types.hpp"
#include "Slic3r/Assert.hpp"

namespace Slic3r::App::Render::GL {
inline GLenum type(DataType type)
{
    constexpr static GLenum translation_table[] = {
        // Float = 0,
        GL_FLOAT,
        // Byte,
        GL_BYTE,
        // UByte
        GL_UNSIGNED_BYTE,
        // Short
        GL_SHORT
    };

    const size_t idx = static_cast<size_t>(type);
    ASSERT(idx < (sizeof(translation_table) / sizeof(translation_table[0])));

    return translation_table[idx];
}

inline GLenum type(IndexType type)
{
    constexpr static GLenum translation_table[] = {
        // UByte = 0,
        GL_UNSIGNED_BYTE,
        // UShort,
        GL_UNSIGNED_SHORT,
        // UInt
        GL_UNSIGNED_INT,
    };

    const size_t idx = static_cast<size_t>(type);
    ASSERT(idx < sizeof(translation_table) / sizeof(translation_table[0]));

    return translation_table[idx];
}

inline GLenum type(PrimitiveType type)
{
    constexpr static GLenum translation_table[] = {
        GL_POINTS,
        GL_LINE_STRIP,
        GL_LINE_LOOP,
        GL_LINES,
        GL_TRIANGLE_STRIP,
        GL_TRIANGLE_FAN,
        GL_TRIANGLES
    };

    const size_t idx = static_cast<size_t>(type);
    ASSERT(idx < sizeof(translation_table) / sizeof(translation_table[0]));

    return translation_table[idx];
}

inline GLenum type(BufferTarget target)
{
    switch (target)
    {
    case BufferTarget::VertexBuffer:
        return GL_ARRAY_BUFFER;
    case BufferTarget::IndexBuffer:
        return GL_ELEMENT_ARRAY_BUFFER;
#ifdef SLIC3R_RENDER_TEXTURE_BUFFER_SUPPORTED
    case BufferTarget::TextureBuffer:
        return GL_TEXTURE_BUFFER;
#endif // SLIC3R_RENDER_TEXTURE_BUFFER_SUPPORTED
    default:
        throw std::runtime_error{"Unreachable code!"};
    }
    UNREACHABLE();
}

inline GLenum type(FramebufferTarget target)
{
    switch (target)
    {
    case FramebufferTarget::Framebuffer:
        return GL_FRAMEBUFFER;
    case FramebufferTarget::DrawFramebuffer:
        return GL_DRAW_FRAMEBUFFER;
    case FramebufferTarget::ReadFramebuffer:
        return GL_READ_FRAMEBUFFER;
    default:
        throw std::runtime_error{"Unreachable code!"};
    }
    UNREACHABLE();
}

inline GLenum type(TextureTarget target)
{
    switch (target)
    {
    case TextureTarget::Texture1D:
        return GL_TEXTURE_1D;
    case TextureTarget::Texture2D:
        return GL_TEXTURE_2D;
    case TextureTarget::Texture3D:
        return GL_TEXTURE_3D;
    case TextureTarget::Texture1DArray:
        return GL_TEXTURE_1D_ARRAY;
    case TextureTarget::Texture2DArray:
        return GL_TEXTURE_2D_ARRAY;
    case TextureTarget::TextureRectangle:
        return GL_TEXTURE_RECTANGLE;
    case TextureTarget::TextureCubeMap:
        return GL_TEXTURE_CUBE_MAP;
    case TextureTarget::TextureCubeMapArray:
        return GL_TEXTURE_CUBE_MAP_ARRAY;
    case TextureTarget::TextureBuffer:
        return GL_TEXTURE_BUFFER;
    case TextureTarget::Texture2DMultisample:
        return GL_TEXTURE_2D_MULTISAMPLE;
    case TextureTarget::Texture2DMultisampleArray:
        return GL_TEXTURE_2D_MULTISAMPLE_ARRAY;
    default:
        throw std::runtime_error{"Unreachable code!"};
    }
    UNREACHABLE();
}

inline GLenum type(BufferUsage usage)
{
    switch (usage) {
    case BufferUsage::StaticDraw:
        return GL_STATIC_DRAW;
    case BufferUsage::DynamicDraw:
        return GL_DYNAMIC_DRAW;
    case BufferUsage::StreamDraw:
        return GL_STREAM_DRAW;
    default:
        throw std::runtime_error{"Unreachable code!"};
    }
    UNREACHABLE();
}

inline GLenum type(BufferAccess access)
{
    switch (access) {
    case BufferAccess::ReadOnly:  return GL_READ_ONLY;
    case BufferAccess::WriteOnly: return GL_WRITE_ONLY;
    case BufferAccess::ReadWrite: return GL_READ_WRITE;
    default: throw std::runtime_error{"Unreachable code!"};
    }
    UNREACHABLE();
}

inline GLenum type(TextureMinFilter filter)
{
    switch (filter)
    {
    case TextureMinFilter::Linear:               return GL_LINEAR;
    case TextureMinFilter::Nearest:              return GL_NEAREST;
    case TextureMinFilter::MipMapNearestNearest: return GL_NEAREST_MIPMAP_NEAREST;
    case TextureMinFilter::MipMapLinearNearest:  return GL_LINEAR_MIPMAP_NEAREST;
    case TextureMinFilter::MipMapNearestLinear:  return GL_NEAREST_MIPMAP_LINEAR;
    case TextureMinFilter::MipMapLinearLinear:   return GL_LINEAR_MIPMAP_LINEAR;
    default: throw std::runtime_error{"Unreachable code!"};
    }
    UNREACHABLE();
}

inline GLenum type(TextureMagFilter filter)
{
    switch (filter)
    {
    case TextureMagFilter::Linear:  return GL_LINEAR;
    case TextureMagFilter::Nearest: return GL_NEAREST;
    default: throw std::runtime_error{"Unreachable code!"};
    }
    UNREACHABLE();
}

inline GLenum type(TextureWrap wrap)
{
    switch (wrap)
    {
    case TextureWrap::ClampToEdge:       return GL_CLAMP_TO_EDGE;
    case TextureWrap::ClampToBorder:     return GL_CLAMP_TO_BORDER;
    case TextureWrap::Repeat:            return GL_REPEAT;
    case TextureWrap::MirroredRepeat:    return GL_MIRRORED_REPEAT;
    case TextureWrap::MirrorClampToEdge: return GL_MIRROR_CLAMP_TO_EDGE;
    default: throw std::runtime_error{"Unreachable code!"};
    }
    UNREACHABLE();    
}

inline GLenum type(CullFaceMode mode)
{
    switch (mode)
    {
    case CullFaceMode::Front:         return GL_FRONT;
    case CullFaceMode::Back:          return GL_BACK;
    case CullFaceMode::FrontAndBack:  return GL_FRONT_AND_BACK;
    default: throw std::runtime_error{"Unreachable code!"};
    }
    UNREACHABLE();
}

inline GLenum type(BlitFramebufferMask mask)
{
    switch (mask)
    {
    case BlitFramebufferMask::ColorBufferBit:   return GL_COLOR_BUFFER_BIT;
    case BlitFramebufferMask::DepthBufferBit:   return GL_DEPTH_BUFFER_BIT;
    case BlitFramebufferMask::StencilBufferBit: return GL_STENCIL_BUFFER_BIT;
    default: throw std::runtime_error{"Unreachable code!"};
    }
    UNREACHABLE();
}

inline GLenum type(BlitFramebufferFilter filter)
{
    switch (filter)
    {
    case BlitFramebufferFilter::Nearest: return GL_NEAREST;
    case BlitFramebufferFilter::Linear:  return GL_LINEAR;
    default: throw std::runtime_error{"Unreachable code!"};
    }
    UNREACHABLE();
}

const char* shader_input_name(VertexAttribType vat);
GLenum texture_internal_format(Domain::PixelFormat format);
GLenum texture_format(Domain::PixelFormat format);
GLenum texture_format_type(Domain::PixelFormat format);

inline bool is_compressed(Domain::PixelFormat format)
{
    switch (format)
    {
    case Domain::PixelFormat::RGB_DXT1:
    case Domain::PixelFormat::RGBA_DXT5: return true;
    default:                             return false;
    }
}

GLenum type(BlendFactor type);
GLenum type(BlendEquation type);

inline bool is_integer(DataType dt) { return dt != DataType::Float; }

}
