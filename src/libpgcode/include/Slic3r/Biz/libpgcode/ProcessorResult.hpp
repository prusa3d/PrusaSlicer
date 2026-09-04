#pragma once

#include "Slic3r/Biz/libpgcode/LineView.hpp"
#include "Slic3r/Biz/libpgcode/Types.hpp"
#include "Slic3r/Assert.hpp"
#include "Slic3r/Domain/PrintStatistics.hpp"

#include <memory>

namespace Slic3r::Biz::libpgcode {

struct FilamentGeometry
{
    float diameter{ 0.0f };
    float area_cross_section{ 0.0f };
};

struct ProcessorResult
{
    ProcessorResult()
        : m_gcode{ std::make_shared<LineView>() }
    {}
    ProcessorResult(const ProcessorResult&) = delete;
    ProcessorResult(ProcessorResult&&) = default;
    ProcessorResult& operator=(const ProcessorResult&) = delete;
    ProcessorResult& operator=(ProcessorResult&&) = default;

    GCodeProducer producer{ GCodeProducer::Unknown };
    uint8_t extruders_count{ MIN_EXTRUDERS_COUNT };
    bool spiral_vase_enabled{ false };
    bool sequential_print{ false };
    float z_offset{ 0.0f };
    float max_print_height{ 0.0f };
    std::string color_change_gcode;
    std::string pause_print_gcode;
    std::string template_custom_gcode;
    std::vector<float> filament_diameters;
    std::vector<float> filament_densities;
    std::vector<float> filament_costs;
    std::vector<Domain::Vec2f> bed_shape;

    std::vector<std::string> extruder_str_colors;
    std::vector<Domain::CustomGCode::Item> custom_gcode_per_print_z;

    Domain::PrintStatistics print_statistics;

    PrintSettings print_settings;
    std::optional<std::pair<std::string, std::string>> sequential_collision_detected;
    bool contained_in_bed{ true };

    uint32_t id() const;
    void set_new_id();

    FilamentGeometry filament_geometry(uint8_t extruder_id) const;
    uint32_t layer_id_at(uint32_t gcode_id) const;

    std::vector<std::string> color_strings_for_color_print() const;

    void reset();

    void set_gcode(std::string&& buffer)
    {
        m_gcode = std::make_shared<LineView>(std::move(buffer));
    }

    /**
     * @brief Getter for modifiable gcode
     */
    LineView& gcode()
    {
        ASSERT(m_gcode);
        return *m_gcode;  
    }

    /**
     * @brief Getter for const gcode shared pointer. Should be used after processing / postprocessing is done.
     */
    std::shared_ptr<const LineView> const_gcode() const
    {
        ASSERT(m_gcode);
        return m_gcode;  
    }

     /**
     * @brief Getter for modifiable vertices
     */
    MoveVertices& moves()
    {
        ASSERT(m_moves);
        return *m_moves;  
    }

    /**
     * @brief Getter for const vertices shared pointer. Should be used after processing / postprocessing is done.
     */
    std::shared_ptr<const MoveVertices> const_moves() const
    {
        ASSERT(m_moves);
        return m_moves;  
    }

private:
    uint32_t m_id{ 0 };
    std::shared_ptr<LineView> m_gcode;
    std::shared_ptr<MoveVertices> m_moves = std::make_shared<MoveVertices>();
};

} // namespace Slic3r::Biz::libpgcode
