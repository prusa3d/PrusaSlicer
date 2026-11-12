#pragma once

#include "Slic3r/App/libvgcode/Types.hpp"

#include <map>

namespace Slic3r::App::libvgcode {

class ExtrusionRoles
{
public:
    struct Item
    {
        Biz::libpgcode::Times times;
        //
        // first = length in mm
        // second = mass in g
        //  
        std::pair<float, float> used_filament;
    };

    void add(Domain::GCodeExtrusionRole role, const std::pair<float, float>& used_filament);
    void update(Domain::GCodeExtrusionRole role, const Biz::libpgcode::Times& times);

    size_t roles_count() const { return m_items.size(); }
    Biz::libpgcode::GCodeExtrusionRoles roles() const;
    float time(Domain::GCodeExtrusionRole role, Biz::libpgcode::TimeMode mode) const;
    float used_filament_length(Domain::GCodeExtrusionRole role) const;
    float used_filament_mass(Domain::GCodeExtrusionRole role) const;

    void reset() { m_items.clear(); }

private:
    std::map<Domain::GCodeExtrusionRole, Item> m_items;
};

} // namespace Slic3r::App::libvgcode
