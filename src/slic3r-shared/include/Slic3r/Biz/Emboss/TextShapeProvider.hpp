#pragma once

#include "Slic3r/Biz/Emboss/ShapeProvider.hpp"

namespace Slic3r::Biz::Emboss {
class TextShapeProvider : public ShapeProvider
{
public:
    TextShapeProvider(
        const Domain::TextConfiguration& text_configuration,
        const Domain::EmbossProjection& projection,
        const TextLines& text_lines,
        FontFileWithCache& font_with_cache
    );

    bool create_shape() override;

    void
    create_text_lines(const Domain::Transform3d& tr, const Domain::ModelObject& object) override;

    void write(Domain::ModelVolume& volume) const override;

private:
    // font item is not used for create object
    Domain::TextConfiguration m_text_configuration;
    FontFileWithCache m_font_with_cache;
};

} // namespace Slic3r::Biz::Emboss
