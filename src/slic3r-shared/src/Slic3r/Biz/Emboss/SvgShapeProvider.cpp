#include "Slic3r/Biz/Emboss/SvgShapeProvider.hpp"

namespace Slic3r::Biz::Emboss {

SvgShapeProvider::SvgShapeProvider(const Domain::EmbossShape& shape, const Scale& scale) :
    ShapeProvider(shape),
    m_scale(scale)
{
    ASSERT(m_shape.svg_file.has_value());
    ASSERT(m_shape.svg_file->file_data != nullptr);
}

bool SvgShapeProvider::create_shape()
{
    if (m_shape.shapes_with_ids.empty())
        read_shape_from_file(m_shape, m_scale.width, m_scale.height);
    return !m_shape.shapes_with_ids.empty();
}

} // namespace Slic3r::Biz::Emboss
