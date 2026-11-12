
#include "UsedFilaments.hpp"
#include "ProcessorImpl.hpp"
#include "Slic3r/Biz/libpgcode/ProcessorResult.hpp"

namespace Slic3r::Biz::libpgcode {
using namespace Domain;

void UsedFilaments::increase_caches(float extruded_volume, uint8_t extruder_id, float parking_volume, float extra_loading_volume)
{
    if (size_t(extruder_id) >= m_extruder_retracted_volume.size())
        m_extruder_retracted_volume.resize(extruder_id + 1, parking_volume);
    
    if (m_recent_toolchange) {
        extruded_volume -= extra_loading_volume;
        m_recent_toolchange = false;
    }
    
    m_extruder_retracted_volume[extruder_id] -= extruded_volume;

    if (m_extruder_retracted_volume[extruder_id] < 0.0f) {
        extruded_volume = -m_extruder_retracted_volume[extruder_id];
        m_extruder_retracted_volume[extruder_id] = 0.0f;

        color_change_cache += extruded_volume;
        tool_change_cache += extruded_volume;
        role_cache += extruded_volume;
    }
}

void UsedFilaments::update_flush_per_extruder(float flushed_volume, uint8_t extruder_id)
{
    if (flushed_volume > 0.f) {
        flush_volumes_per_extruder[extruder_id] += flushed_volume;
        volumes_per_extruder[extruder_id] += flushed_volume;
    }
}

void UsedFilaments::process_color_change_cache()
{
    if (color_change_cache != 0.0f) {
        volumes_per_color_change.push_back(color_change_cache);
        color_change_cache = 0.0f;
    }
}

void UsedFilaments::process_extruder_cache(uint8_t extruder_id)
{
    if (tool_change_cache != 0.0f) {
        volumes_per_extruder[extruder_id] += tool_change_cache;
        tool_change_cache = 0.0f;
    }
    m_recent_toolchange = true;
}

void UsedFilaments::process_role_cache(const ProcessorResult& result, uint8_t extruder_id, GCodeExtrusionRole role)
{
    if (role_cache != 0.0f) {
        std::pair<float, float> filament = { 0.0f, 0.0f };

        float s = result.filament_geometry(extruder_id).area_cross_section;
        filament = { 0.001f * role_cache / s,
                     0.001f * role_cache * result.filament_densities[extruder_id] };

        if (filaments_per_role.find(role) != filaments_per_role.end()) {
            filaments_per_role[role].first  += filament.first;
            filaments_per_role[role].second += filament.second;
        }
        else
            filaments_per_role[role] = filament;

        if (role == GCodeExtrusionRole::WipeTower) {
            wipe_tower_volumes_per_extruder[extruder_id] += role_cache;
        }

        role_cache = 0.0f;
    }
}

void UsedFilaments::process_caches(const ProcessorResult& result, uint8_t extruder_id, GCodeExtrusionRole role)
{
    process_color_change_cache();
    process_extruder_cache(extruder_id);
    process_role_cache(result, extruder_id, role);
}

void UsedFilaments::reset()
{
    color_change_cache = 0.0f;
    volumes_per_color_change.clear();

    tool_change_cache = 0.0f;
    volumes_per_extruder.clear();
    wipe_tower_volumes_per_extruder.clear();
    flush_volumes_per_extruder.clear();

    role_cache = 0.0f;
    filaments_per_role.clear();

    m_recent_toolchange = false;
    m_extruder_retracted_volume.clear();
}

} // namespace Slic3r::Biz::libpgcode
