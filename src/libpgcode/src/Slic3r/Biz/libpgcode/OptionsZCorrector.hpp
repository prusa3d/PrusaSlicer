#pragma once

#include "Slic3r/Biz/libpgcode/ProcessorResult.hpp"

namespace Slic3r::Biz::libpgcode {

// Helper class used to fix the z for color change, pause print and
// custom gcode markes
class OptionsZCorrector
{
public:
    explicit OptionsZCorrector(ProcessorResult& result) : m_result(result) {}

    void set() {
        m_move_id = m_result.const_moves()->size() - 1;
        m_custom_gcode_per_print_z_id = m_result.custom_gcode_per_print_z.size() - 1;
    }

    void update(float height);

    void reset() {
        m_move_id.reset();
        m_custom_gcode_per_print_z_id.reset();
    }

private:
    ProcessorResult& m_result;
    std::optional<size_t> m_move_id;
    std::optional<size_t> m_custom_gcode_per_print_z_id;
};

} // namespace Slic3r::Biz::libpgcode
