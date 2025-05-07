#ifndef slic3r_TextLines_hpp_
#define slic3r_TextLines_hpp_

#include <vector>
#include <libslic3r/Polygon.hpp>
#include <libslic3r/Point.hpp>
#include <libslic3r/Emboss.hpp>
#include "slic3r/GUI/GLModel.hpp"
#include "slic3r/Utils/EmbossStyleManager.hpp"

namespace Slic3r {
class ModelVolume;
typedef std::vector<ModelVolume *> ModelVolumePtrs;
struct FontProp;
}

namespace Slic3r::GUI {
class TextLinesModel
{
public:
    /// <summary>
    /// Initialize model and lines
    /// </summary>
    /// <param name="text_tr">Transformation of text volume inside object (aka inside of instance)</param>
    /// <param name="volumes_to_slice">Vector of volumes to be sliced</param>
    /// <param name="style_manager">Contain Font file, size and align</param>
    /// <param name="count_lines">Count lines of embossed text(for veritcal alignment)</param>
    void init(const Transform3d &text_tr, const ModelVolumePtrs &volumes_to_slice, /*const*/ Emboss::StyleManager &style_manager, unsigned count_lines);

    void render(const Transform3d &text_world);

    bool is_init() const { return m_model.is_initialized(); }
    void reset() { m_model.reset(); m_lines.clear(); }
    const Slic3r::Emboss::TextLines &get_lines() const { return m_lines; }

private:
    Slic3r::Emboss::TextLines m_lines;

    // Keep model for visualization text lines
    GLModel m_model;
};
} // namespace Slic3r::GUI

namespace Slic3r::Emboss{
/// <summary>
/// creation line without model for backend only
/// </summary>
/// <param name="text_tr">Transformation of text volume inside object (aka inside of
/// instance)</param> <param name="volumes_to_slice">Vector of volumes to be sliced</param> <param
/// name="ff"></param> <param name="fp"></param> <param name="count_lines">Count lines of embossed
/// text(for veritcal alignment)</param> <param name="line_height_mm_ptr">[output] line height in
/// mm</param> <returns></returns>
TextLines create_text_lines(
    const Transform3d &text_tr,
    const ModelVolumePtrs &volumes_to_slice,
    const FontFile &ff,
    const FontProp &fp,
    unsigned count_lines = 1,
    double *line_height_mm_ptr = nullptr
);
}
#endif // slic3r_TextLines_hpp_