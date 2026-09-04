
#include "Slic3r/Biz/libpgcode/ProcessorResult.hpp"
#include <numbers>

namespace Slic3r::Biz::libpgcode {
using namespace Domain;

constexpr auto PI{std::numbers::pi_v<float>};

static uint32_t ID = 0;

uint32_t ProcessorResult::id() const { return m_id; }

void ProcessorResult::set_new_id()
{
    if (ID + 1 == UINT_MAX) ID = 0;
    ++ID;
    m_id = ID;
}

FilamentGeometry ProcessorResult::filament_geometry(uint8_t extruder_id) const
{
    FilamentGeometry ret;
    ret.diameter = (size_t(extruder_id) < filament_diameters.size()) ?
        filament_diameters[extruder_id] : filament_diameters.back();
    ret.area_cross_section = PI * (0.5f * ret.diameter) * (0.5f * ret.diameter);
    return ret;
}

uint32_t ProcessorResult::layer_id_at(uint32_t gcode_id) const
{
    if (m_moves->empty())
        return 0;

    auto it = std::lower_bound(m_moves->begin(), m_moves->end(), gcode_id,
        [](const MoveVertex& m, uint32_t id) { return m.gcode_id < id; });

    return (it == m_moves->end()) ? m_moves->back().layer_id : it->layer_id;
}

std::vector<std::string> ProcessorResult::color_strings_for_color_print() const
{
    std::vector<std::string> ret = extruder_str_colors;
    ret.reserve(ret.size() + custom_gcode_per_print_z.size() + 1);
    for (const CustomGCode::Item& code : custom_gcode_per_print_z) {
        if (code.type == CustomGCode::Type::ColorChange)
            ret.emplace_back(code.color);
    }
    // gray color for pause print or custom G-code 
    ret.emplace_back(DUMMY_STR_COLOR);
    return ret;
}

void ProcessorResult::reset()
{
    producer = GCodeProducer::Unknown;
    extruders_count = MIN_EXTRUDERS_COUNT;
    spiral_vase_enabled = false;
    sequential_print = false;
    z_offset = 0.0f;
    max_print_height = 0.0f;
    filament_diameters.clear();
    filament_densities.clear();
    filament_costs.clear();
    bed_shape.clear();
    m_gcode = std::make_shared<LineView>();
    m_moves = std::make_shared<MoveVertices>();
    extruder_str_colors.clear();
    custom_gcode_per_print_z.clear();
    print_statistics = {};
    print_settings.reset();
}

} // namespace Slic3r::Biz::libpgcode
