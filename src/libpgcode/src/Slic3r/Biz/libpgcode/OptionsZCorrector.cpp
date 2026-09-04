
#include "OptionsZCorrector.hpp"

namespace Slic3r::Biz::libpgcode {

void OptionsZCorrector::update(float height)
{
    if (!m_move_id.has_value() || !m_custom_gcode_per_print_z_id.has_value())
        return;

    const Domain::Vec3f& position = m_result.moves().back().position;

    MoveVertex& move = m_result.moves().emplace_back(m_result.moves()[*m_move_id]);
    move.position = position;
    move.height = height;
    m_result.moves().erase(m_result.moves().begin() + *m_move_id);
    m_result.custom_gcode_per_print_z[*m_custom_gcode_per_print_z_id].print_z = position.z();
    reset();
}

} // namespace Slic3r::Biz::libpgcode
