#include "Slic3r/App/libvgcode/ExtrusionRoles.hpp"

using namespace Slic3r::Biz::libpgcode;

namespace Slic3r::App::libvgcode {

using Domain::GCodeExtrusionRole;

void ExtrusionRoles::add(GCodeExtrusionRole role, const std::pair<float, float>& used_filament)
{
    auto it = m_items.find(role);
    if (it == m_items.end())
        m_items.insert({ role, { {}, used_filament } });
}

void ExtrusionRoles::update(GCodeExtrusionRole role, const Biz::libpgcode::Times& times)
{
    auto it = m_items.find(role);
    if (it != m_items.end()) {
        for (size_t i = 0; i < TIME_MODES_COUNT; ++i) {
            it->second.times[i] += times[i];
        }
    }
}

GCodeExtrusionRoles ExtrusionRoles::roles() const
{
    GCodeExtrusionRoles ret;
    ret.reserve(m_items.size());
    for (const auto& [role, item] : m_items) {
        ret.emplace_back(role);
    }
    return ret;
}

float ExtrusionRoles::time(GCodeExtrusionRole role, TimeMode mode) const
{
    auto it = m_items.find(role);
    if (it == m_items.end())
        return 0.0f;

    return (mode < TimeMode::COUNT) ? it->second.times[size_t(mode)] : 0.0f;
}

float ExtrusionRoles::used_filament_length(GCodeExtrusionRole role) const
{
    auto it = m_items.find(role);
    return (it != m_items.end()) ? it->second.used_filament.first : 0.0f;
}

float ExtrusionRoles::used_filament_mass(GCodeExtrusionRole role) const
{
    auto it = m_items.find(role);
    return (it != m_items.end()) ? it->second.used_filament.second : 0.0f;
}

} // namespace Slic3r::App::libvgcode
