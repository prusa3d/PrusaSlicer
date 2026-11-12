#pragma once

#include "commonGL.hpp"

namespace Slic3r::App::Render::GL {
template<typename I> struct IndexTypeTraits
{
    static constexpr GLenum type_id = 0;
};

template<> struct IndexTypeTraits<unsigned int>
{
    static constexpr GLenum type_id = GL_UNSIGNED_INT;
};

template<> struct IndexTypeTraits<unsigned short>
{
    static constexpr GLenum type_id = GL_UNSIGNED_SHORT;
};

template<> struct IndexTypeTraits<unsigned char>
{
    static constexpr GLenum type_id = GL_UNSIGNED_BYTE;
};

} // namespace Slic3r::App::Render::GL

