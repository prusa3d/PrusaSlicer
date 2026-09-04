#include "Slic3r/App/Render/GL/GLTypes.hpp"

#include "Slic3r/Assert.hpp"

namespace Slic3r::App::Render::GL {

using Domain::PixelFormat;

const char* shader_input_name(VertexAttribType vat)
{
    switch (vat) {
    case VertexAttribType::Vertex:
        return "v_position";

    case VertexAttribType::Normal:
        return "v_normal";

    case VertexAttribType::TexCoord0:
        return "v_tex_coord";

    case VertexAttribType::Color:
        return "v_color";

    case VertexAttribType::Extra:
        return "v_extra";

    case VertexAttribType::PaletteIndex:
        return "v_palette_index";
    }

    // Encountered missing VertexAttribType, if valid, please add it into the switch above
    ASSERT(false);
    return "";
}

GLenum texture_internal_format(PixelFormat format)
{
    switch (format) {
    case PixelFormat::RGB8:           return GL_RGB;
    case PixelFormat::RGBA8:          return GL_RGBA;
    case PixelFormat::R16F:           return GL_R16F;
    case PixelFormat::R32F:           return GL_R32F;
    case PixelFormat::R32UI:          return GL_R32UI;
    case PixelFormat::RG16F:          return GL_RG16F;
    case PixelFormat::RGBA32F:        return GL_RGBA32F;
    case PixelFormat::RGBA16F:        return GL_RGBA16F;
    case PixelFormat::RGB32F:         return GL_RGB32F;
    case PixelFormat::DepthComponent: return GL_DEPTH_COMPONENT24;
    case PixelFormat::RGB_DXT1:       return GL_COMPRESSED_RGB;
    case PixelFormat::RGBA_DXT5:      return GL_COMPRESSED_RGBA;
    default: {
        // Unsupported format
        throw std::runtime_error{"Unreachable code!"};
    }
    }
}

GLenum texture_format(PixelFormat format)
{
    switch (format) {
    case PixelFormat::RGB8:           return GL_RGB;
    case PixelFormat::RGBA8:          return GL_RGBA;
    case PixelFormat::R16F:           return GL_RED;
    case PixelFormat::R32F:           return GL_RED;
    case PixelFormat::R32UI:          return GL_RED_INTEGER;
    case PixelFormat::RG16F:          return GL_RG;
    case PixelFormat::RGBA32F:        return GL_RGBA;
    case PixelFormat::RGBA16F:        return GL_RGBA;
    case PixelFormat::RGB32F:         return GL_RGB;
    case PixelFormat::DepthComponent: return GL_DEPTH_COMPONENT;
    case PixelFormat::RGB_DXT1:       return GL_COMPRESSED_RGB_S3TC_DXT1_EXT;
    case PixelFormat::RGBA_DXT5:      return GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
    default: {
        // Unsupported format
        throw std::runtime_error{"Unreachable code!"};
    }
    }
}

GLenum texture_format_type(PixelFormat format)
{
    switch (format) {
    case PixelFormat::RGB8:           return GL_UNSIGNED_BYTE;
    case PixelFormat::RGBA8:          return GL_UNSIGNED_BYTE;
    case PixelFormat::R16F:           return GL_FLOAT;
    case PixelFormat::R32F:           return GL_FLOAT;
    case PixelFormat::R32UI:          return GL_UNSIGNED_INT;
    case PixelFormat::RG16F:          return GL_FLOAT;
    case PixelFormat::RGBA32F:        return GL_FLOAT;
    case PixelFormat::RGBA16F:        return GL_FLOAT;
    case PixelFormat::RGB32F:         return GL_FLOAT;
    case PixelFormat::DepthComponent: return GL_FLOAT;
    case PixelFormat::RGB_DXT1:       return GL_UNSIGNED_BYTE;
    case PixelFormat::RGBA_DXT5:      return GL_UNSIGNED_BYTE;
    default:
        // Unsupported format
        ASSERT(false);
        return GL_UNSIGNED_BYTE;
    }
}

GLenum type(BlendFactor type)
{
    constexpr static GLenum translation_table[] = {
        GL_ZERO,
        GL_ONE,
        GL_SRC_COLOR,
        GL_ONE_MINUS_SRC_COLOR,
        GL_DST_COLOR,
        GL_ONE_MINUS_DST_COLOR,
        GL_SRC_ALPHA,
        GL_ONE_MINUS_SRC_ALPHA,
        GL_DST_ALPHA,
        GL_ONE_MINUS_DST_ALPHA
    };

    const size_t idx = static_cast<size_t>(type);
    ASSERT(idx < sizeof(translation_table)/sizeof(translation_table[0]));
    return translation_table[idx];
}

GLenum type(BlendEquation type)
{
    constexpr static GLenum translation_table[] = {
        GL_FUNC_ADD,
        GL_FUNC_SUBTRACT,
        GL_FUNC_REVERSE_SUBTRACT,
        GL_MIN,
        GL_MAX
    };

    const size_t idx = static_cast<size_t>(type);
    ASSERT(idx < sizeof(translation_table)/sizeof(translation_table[0]));
    return translation_table[idx];
}


}
