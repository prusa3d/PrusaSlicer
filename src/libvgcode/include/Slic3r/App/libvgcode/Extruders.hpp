#pragma once

#include "Slic3r/App/libvgcode/Types.hpp"

#include <map>

namespace Slic3r::App::libvgcode {

class Extruders
{
public:
    struct Item
    {
        ColorPrints color_prints;
        //
        // first = length in mm
        // second = mass in g
        //  
        std::pair<float, float> used_filament;
    };

    void add(uint8_t id, const std::pair<float, float>& used_filament);
    void update(uint8_t id, const ColorPrint& color_print);

    uint8_t extruders_count() const { return uint8_t(m_items.size()); }
    std::vector<uint8_t> extruders_ids() const;
    uint8_t extruder_max_id() const { return m_items.empty() ? 0 : m_items.rbegin()->first; }
    size_t extruder_color_prints_count(uint8_t id) const;
    ColorPrints extruder_color_prints(uint8_t id) const;
    float extruder_used_filament_length(uint8_t id) const;
    float extruder_used_filament_mass(uint8_t id) const;

    void reset() { m_items.clear(); }

private:
    std::map<uint8_t, Item> m_items;
};

} // namespace Slic3r::App::libvgcode
