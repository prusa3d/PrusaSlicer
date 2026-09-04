#include "Slic3r/Biz/Emboss/TextShapeProvider.hpp"
#include "Slic3r/Biz/Emboss/TextLines.hpp"
#include "Slic3r/Biz/Emboss/TextBender.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"

#include <boost/nowide/convert.hpp>

namespace Slic3r::Biz::Emboss {

TextShapeProvider::TextShapeProvider(
    const Domain::TextConfiguration& text_configuration,
    const Domain::EmbossProjection& projection,
    const Biz::Emboss::TextLines& text_lines,
    Biz::Emboss::FontFileWithCache& font_with_cache
) :
    ShapeProvider(
        Domain::EmbossShape{
            .scale = Biz::Emboss::get_text_shape_scale(
                text_configuration.style.prop,
                *font_with_cache.font_file
            ),
            .projection = projection
        },
        text_lines
    ),
    m_text_configuration(text_configuration) // copy
    ,
    m_font_with_cache(font_with_cache)
{}

bool TextShapeProvider::create_shape()
{
    ASSERT(m_shape.final_shape.expolygons.empty()); // already created
    std::wstring text                 = boost::nowide::widen(m_text_configuration.text);
    const Domain::FontProp& font_prop = m_text_configuration.style.prop;
    m_shape.shapes_with_ids = Biz::Emboss::text2vshapes(m_font_with_cache, text, font_prop);
    return true;
}

void TextShapeProvider::create_text_lines(
    const Domain::Transform3d& tr,
    const Domain::ModelObject& object
)
{
    ASSERT(m_text_lines.empty());
    if (!m_text_configuration.style.prop.per_glyph)
        return; // Do not create text lines when not neccessary

    Domain::ModelVolumePtrs vols = Biz::Emboss::prepare_volumes_to_slice(object);

    const Domain::FontFile& ff = *m_font_with_cache.font_file;
    const Domain::FontProp& fp = m_text_configuration.style.prop;
    unsigned l   = Biz::Emboss::get_count_lines(m_text_configuration.text); // SHOULD be 1
    m_text_lines = Biz::Emboss::create_text_lines(tr, vols, ff, fp, l);
}

void TextShapeProvider::write(Domain::ModelVolume& volume) const
{
    ShapeProvider::write(volume); // write emboss_shape
    volume.text_configuration = m_text_configuration; // copy
    volume.text_configuration->bend_reference = m_bend_reference;
    ASSERT(volume.emboss_shape.has_value());

    // Fix for object: stored attribute that volume is embossed per glyph
    if (volume.is_the_only_one_part() && m_text_configuration.style.prop.per_glyph) {
        volume.text_configuration->style.prop.per_glyph = false;
    }
}

void TextShapeProvider::deform_mesh(Domain::TriangleMesh& mesh) const
{
    m_bend_reference = Domain::bounding_box(mesh.its);
    const auto& prop = m_text_configuration.style.prop;
    const BendParams params{prop.bend_horizontal.value_or(0.0f), prop.bend_vertical.value_or(0.0f),
        prop.bend_arc.value_or(0.0f)};
    if (params.horizontal_bend == 0.0f && params.vertical_curl == 0.0f && params.vertical_arc == 0.0f)
        return;
    auto its = std::move(mesh.its);
    TextBender::bend_mesh(its, params, *m_bend_reference);
    mesh = Algorithms::TriangleMesh::construct(std::move(its));
}
} // namespace Slic3r::Biz::Emboss
