#pragma once

#include "Slic3r/Biz/libpgcode/Processor.hpp"
#include "Slic3r/Biz/GCodeReader/GCodeReader.hpp"
#include "TimeProcessor.hpp"
#include "UsedFilaments.hpp"
#include "OptionsZCorrector.hpp"

#include <float.h>

namespace Slic3r::Biz::libpgcode {

enum class G1DiscretizationOrigin : uint8_t
{
    G1,
    G2G3
};

struct ExtruderColor
{
    uint8_t counter{ 0 };
    uint8_t current{ 0 };

    void reset() {
        counter = 0;
        current = 0;
    }
};

struct FeedMultiply
{
    float current{ 1.0f }; // percentage
    float saved{ 1.0f };   // percentage

    void reset() {
        current = 1.0f;
        saved   = 1.0f;
    }
};

struct CachedPosition
{
    Domain::Vec4f position{ FLT_MAX, FLT_MAX, FLT_MAX, FLT_MAX };
    float feedrate{ FLT_MAX };

    void reset() {
        position = Domain::Vec4f{ FLT_MAX, FLT_MAX, FLT_MAX, FLT_MAX };
        feedrate = FLT_MAX;
    }
};

class ProcessorImpl
{
public:
    explicit ProcessorImpl(ProcessorConfig&& config);

    void process_buffer(std::string&& buffer, std::function<void(float)> progress_callback = nullptr);
    [[nodiscard]] ProcessorResult finalize();

    [[nodiscard]] PostProcessorConfig post_processor_config();

    const std::vector<int>& extruder_temps_config() const { return m_config.extruders.temps_config; }
    const std::vector<int>& extruder_temps_first_layer_config() const { return m_config.extruders.temps_first_layer_config; }

private:
    PositioningType m_global_positioning_type{ PositioningType::Absolute };
    PositioningType m_e_local_positioning_type{ PositioningType::Absolute };
    Domain::GCodeExtrusionRole m_extrusion_role{ Domain::GCodeExtrusionRole::None };
    UnitsType m_units{ UnitsType::Millimeters };
    uint8_t m_extruder_id{ 0 };
    bool m_wiping{ false };
    bool m_flushing{ false };
    bool m_exclude_e{ false };
    bool m_seams_detection_enabled{ false };
    uint32_t m_line_id{ 0 };
    uint32_t m_last_line_id{ 0 };
    uint32_t m_g1_line_id{ 0 };
    uint32_t m_layer_id{ 0 };
    uint32_t m_last_default_color_id{ 0 };
    float m_feedrate{ 0.0f }; // mm/s
    float m_fan_speed{ 0.0f }; // percentage
    float m_width{ 0.0f }; // mm
    float m_height{ 0.0f }; // mm
    float m_width_from_tag{ 0.0f }; // mm
    float m_height_from_tag{ 0.0f }; // mm
    float m_mm3_per_mm{ 0.0f };
    float m_extruded_last_z{ 0.0f }; // mm
    std::vector<float> m_extruder_temps;
    std::vector<uint8_t> m_extruder_colors;
    Domain::Vec4f m_start_position{ Domain::Vec4f::Zero() }; // mm
    Domain::Vec4f m_end_position{ Domain::Vec4f::Zero() }; // mm
    Domain::Vec4f m_saved_position{ Domain::Vec4f::Zero() }; // mm
    Domain::Vec4f m_origin{ Domain::Vec4f::Zero() }; // mm
    GCodeReader::GCodeReader m_parser;
    FeedMultiply m_feed_multiply;
    ProcessorConfig m_config;
    ProcessorResult m_result;
    TimeProcessor m_time_processor;
    UsedFilaments m_used_filaments;
    ExtruderColor m_extruder_color;
    OptionsZCorrector m_options_z_corrector;
    CachedPosition m_cached_position;

    ProcessorLogCallback m_cb_log{ nullptr };

    void init();
    void apply_config(ProcessorConfig&& config);
    void process_gcode_line(const GCodeReader::GCodeReader::GCodeReader::GCodeLine& line);

    //
    // Process G commands
    // 

    // Move
    void process_G0(const GCodeReader::GCodeReader::GCodeReader::GCodeLine& line) { process_G1(line); }
    void process_G1(const GCodeReader::GCodeReader::GCodeReader::GCodeLine& line);
    void process_G1(const std::array<std::optional<float>, 4>& axes = { std::nullopt, std::nullopt, std::nullopt, std::nullopt },
        const std::optional<float>& feedrate = std::nullopt, G1DiscretizationOrigin origin = G1DiscretizationOrigin::G1,
        const std::optional<unsigned int>& remaining_internal_g1_lines = std::nullopt);
    // Arc Move
    void process_G2_G3(const GCodeReader::GCodeReader::GCodeLine& line, bool clockwise);
    // Retract or Set tool temperature
    void process_G10(const GCodeReader::GCodeReader::GCodeLine& line);
    // Unretract
    void process_G11(const GCodeReader::GCodeReader::GCodeLine& line) { store_move(MoveType::Unretract); }
    // Set Units to Inches
    void process_G20(const GCodeReader::GCodeReader::GCodeLine& line) { m_units = UnitsType::Inches; }
    // Set Units to Millimeters
    void process_G21(const GCodeReader::GCodeReader::GCodeLine& line) { m_units = UnitsType::Millimeters; }
    // Firmware controlled Retract
    void process_G22(const GCodeReader::GCodeReader::GCodeLine& line) { store_move(MoveType::Retract); }
    // Firmware controlled Unretract
    void process_G23(const GCodeReader::GCodeReader::GCodeLine& line) { store_move(MoveType::Unretract); }
    // Move to origin
    void process_G28(const GCodeReader::GCodeReader::GCodeLine& line);
    // Save Current Position
    void process_G60(const GCodeReader::GCodeReader::GCodeLine& line);
    // Return to Saved Position
    void process_G61(const GCodeReader::GCodeReader::GCodeLine& line);
    // Set to Absolute Positioning
    void process_G90(const GCodeReader::GCodeReader::GCodeLine& line) { m_global_positioning_type = PositioningType::Absolute; }
    // Set to Relative Positioning
    void process_G91(const GCodeReader::GCodeReader::GCodeLine& line) { m_global_positioning_type = PositioningType::Relative; }

    // Set Position
    void process_G92(const GCodeReader::GCodeReader::GCodeLine& line);

    //
    // Process M commands
    // 

    // Sleep or Conditional stop
    void process_M1(const GCodeReader::GCodeReader::GCodeLine& line) { simulate_st_synchronize(); }
    // Set extruder to absolute mode
    void process_M82(const GCodeReader::GCodeReader::GCodeLine& line) { m_e_local_positioning_type = PositioningType::Absolute; }
    // Set extruder to relative mode
    void process_M83(const GCodeReader::GCodeReader::GCodeLine& line) { m_e_local_positioning_type = PositioningType::Relative; }
    // Set extruder temperature
    void process_M104(const GCodeReader::GCodeReader::GCodeLine& line);
    // Set fan speed
    void process_M106(const GCodeReader::GCodeReader::GCodeLine& line);
    // Disable fan
    void process_M107(const GCodeReader::GCodeReader::GCodeLine& line) { m_fan_speed = 0.0f; }
    // Set tool (Sailfish)
    void process_M108(const GCodeReader::GCodeReader::GCodeLine& line);
    // Set extruder temperature and wait
    void process_M109(const GCodeReader::GCodeReader::GCodeLine& line);
    // Recall stored home offsets
    void process_M132(const GCodeReader::GCodeReader::GCodeLine& line);
    // Set tool (MakerWare)
    void process_M135(const GCodeReader::GCodeReader::GCodeLine& line);
    // Set max printing acceleration
    void process_M201(const GCodeReader::GCodeReader::GCodeLine& line);
    // Set maximum feedrate
    void process_M203(const GCodeReader::GCodeReader::GCodeLine& line);
    // Set default acceleration
    void process_M204(const GCodeReader::GCodeReader::GCodeLine& line);
    // Advanced settings
    void process_M205(const GCodeReader::GCodeReader::GCodeLine& line);
    // Set Feedrate Percentage
    void process_M220(const GCodeReader::GCodeReader::GCodeLine& line);
    // Set extrude factor override percentage
    void process_M221(const GCodeReader::GCodeReader::GCodeLine& line);
    // Repetier: Store x, y and z position
    void process_M401(const GCodeReader::GCodeReader::GCodeLine& line);
    // Repetier: Go to stored position
    void process_M402(const GCodeReader::GCodeReader::GCodeLine& line);
    // Set allowable instantaneous speed change
    void process_M566(const GCodeReader::GCodeReader::GCodeLine& line);
    // Unload the current filament into the MK3 MMU2 unit at the end of print.
    void process_M702(const GCodeReader::GCodeReader::GCodeLine& line);

    //
    // Process T commands
    // 

    // Processes T line (Select Tool)
    void process_T(const GCodeReader::GCodeReader::GCodeLine& line);
    void process_T(const std::string_view command);

    //
    // Process tags embedded into comments
    //
    void process_tags(const std::string_view comment);
    void process_producers_tags(const std::string_view comment);
    void process_bambustudio_tags(const std::string_view comment);
    void process_craftware_tags(const std::string_view comment);
    void process_cura_tags(const std::string_view comment);
    void process_kisslicer_tags(const std::string_view comment);
    void process_ideamaker_tags(const std::string_view comment);
    void process_orcaslicer_tags(const std::string_view comment);
    void process_simplify3d_tags(const std::string_view comment);

    void process_custom_gcode_time(Domain::CustomGCode::Type code);
    void process_filaments(Domain::CustomGCode::Type code);

    void set_extrusion_role(Domain::GCodeExtrusionRole role);

    void reset();
    // Simulates firmware st_synchronize() call
    void simulate_st_synchronize(float additional_time = 0.0f);
    void store_move(MoveType type, bool internal_only = false);
    void calculate_time(size_t keep_last_n_blocks = 0, float additional_time = 0.0f);
    void update_basic_statistics();
};

} // namespace Slic3r::Biz::libpgcode
