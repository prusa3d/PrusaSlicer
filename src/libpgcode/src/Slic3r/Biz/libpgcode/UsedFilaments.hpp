#pragma once

#include "Slic3r/Biz/libpgcode/Types.hpp"

namespace Slic3r::Biz::libpgcode {

struct ProcessorResult;

struct UsedFilaments
{
    float color_change_cache{ 0.0f };
    std::vector<float> volumes_per_color_change;

    float tool_change_cache{ 0.0f };
    // Extruder ID -> volume
    std::map<uint8_t, float> volumes_per_extruder;
    std::map<uint8_t, float> wipe_tower_volumes_per_extruder;
    std::map<uint8_t, float> flush_volumes_per_extruder;

    float role_cache{ 0.0f };
    // Extrusion Role -> (length [m], mass [g])
    std::map<Domain::GCodeExtrusionRole, std::pair<float, float>> filaments_per_role;

    void increase_caches(float extruded_volume, uint8_t extruder_id, float parking_volume, float extra_loading_volume);
    void update_flush_per_extruder(float flushed_volume, uint8_t extruder_id);

    void process_color_change_cache();
    void process_extruder_cache(uint8_t extruder_id);
    void process_role_cache(const ProcessorResult& result, uint8_t extruder_id, Domain::GCodeExtrusionRole role);
    void process_caches(const ProcessorResult& result, uint8_t extruder_id, Domain::GCodeExtrusionRole role);

    void reset();

private:
    std::vector<float> m_extruder_retracted_volume;
    bool m_recent_toolchange{ false };
};

} // namespace Slic3r::Biz::libpgcode
