#include "Slic3r/App/Preview/GCodeWindow.hpp"

#include "Slic3r/App/Preview/GCodeDisplay.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"

#include "Slic3r/Assert.hpp"

#include <algorithm>
#include <boost/algorithm/string/trim.hpp>

namespace Slic3r::App::Preview {

GCodeWindowData::Line GCodeWindowData::line_at(uint32_t id) const
{
    if (id >= uint32_t(m_gcode->size())) {
        return {};
    }

    // Trim the trailing newline and the leading whitespaces to not mismatch command as parameters.
    const std::string_view payload = boost::algorithm::trim_copy((*m_gcode)[id]);

    const size_t comment_pos = std::min(payload.find(';'), payload.size());
    const size_t params_pos  = std::min(payload.substr(0, comment_pos).find(' '), comment_pos);

    return {
        .command    = payload.substr(0, params_pos),
        .parameters = payload.substr(params_pos, comment_pos - params_pos),
        .comment    = payload.substr(comment_pos)
    };
}

void GCodeWindowData::resize_range(Range& range, uint32_t lines_count, uint32_t curr_line_id) const
{
    uint32_t total_lines_count = uint32_t(m_gcode->size());
    DEBUG_ASSERT(total_lines_count > 0);
    uint32_t half_lines_count = lines_count / 2;
    range.min = (curr_line_id > half_lines_count) ? curr_line_id - half_lines_count : 1;
    range.max = *range.min + lines_count - 1;
    if (*range.max >= total_lines_count) {
        range.max = total_lines_count - 1;
        range.min = *range.max - lines_count + 1;
    }
}

GCodeWindow::GCodeWindow(libvgcode::FdmViewer* viewer, GCodeWindowData* data) :
    Yoga::CollapsibleWindow(Biz::_u8L("G-code viewer"), "GCodeWindow")
{
    set_flex_grow(1.);
    content()->set_flex_grow(1);
    m_gcode = content()->emplace_back<GCodeDisplay>(viewer, data);
    m_gcode->set_flex_grow(1);
}

void GCodeWindow::set_clip_text(bool clip_text) { m_gcode->set_clip_text(clip_text); }

} // namespace Slic3r::App::Preview
