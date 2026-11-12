#pragma once
#include "Slic3r/Biz/Emboss/ShapeProvider.hpp"

namespace Slic3r::Biz::Emboss {
struct Scale {
    std::optional<float> width;
    std::optional<float> height;
    std::optional<float> depth;
};


class SvgShapeProvider : public ShapeProvider {
    const Scale& m_scale;
public:
    SvgShapeProvider(const Domain::EmbossShape& shape, const Scale& scale);

    bool create_shape() override;
};

} // namespace Slic3r::Biz::Emboss
