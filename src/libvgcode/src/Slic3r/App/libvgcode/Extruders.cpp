#include "Slic3r/App/libvgcode/Extruders.hpp"

namespace Slic3r::App::libvgcode {

void Extruders::add(uint8_t id, const std::pair<float, float>& used_filament)
{
    auto it = m_items.find(id);
    if (it == m_items.end())
        m_items.insert({ id, { ColorPrints{}, used_filament}});
}

void Extruders::update(uint8_t id, const ColorPrint& color_print)
{
    auto it = m_items.find(id);
    if (it != m_items.end()) {
        if (it->second.color_prints.empty() || it->second.color_prints.back().color_id != color_print.color_id)
            it->second.color_prints.emplace_back(color_print);
    }
}

std::vector<uint8_t> Extruders::extruders_ids() const
{
    std::vector<uint8_t> ret;
    ret.reserve(m_items.size());
    for (const auto& [id, item] : m_items) {
        ret.emplace_back(id);
    }
    return ret;
}

size_t Extruders::extruder_color_prints_count(uint8_t id) const
{
    auto it = m_items.find(id);
    return (it == m_items.end()) ? 0 : it->second.color_prints.size();
}

ColorPrints Extruders::extruder_color_prints(uint8_t id) const
{
    auto it = m_items.find(id);
    return (it == m_items.end()) ? ColorPrints{} : it->second.color_prints;
}

float Extruders::extruder_used_filament_length(uint8_t id) const
{
    auto it = m_items.find(id);
    return (it == m_items.end()) ? 0.0f : it->second.used_filament.first;
}

float Extruders::extruder_used_filament_mass(uint8_t id) const
{
    auto it = m_items.find(id);
    return (it == m_items.end()) ? 0.0f : it->second.used_filament.second;
}

} // namespace Slic3r::App::libvgcode
