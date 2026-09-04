#pragma once

#include "Slic3r/Biz/libpgcode/Types.hpp"
#include "Slic3r/Domain/GCodeFlavor.hpp"

namespace Slic3r::Biz::libpgcode {

enum class MachineLimitsUsageType : uint8_t
{
    EmitToGCode,
    TimeEstimateOnly,
    Ignore,
    COUNT
};

static constexpr size_t MACHINE_LIMITS_USAGE_TYPES_COUNT = size_t(MachineLimitsUsageType::COUNT);

struct MachineLimitsConfig
{
    MachineLimitsUsageType usage{ MachineLimitsUsageType::TimeEstimateOnly };

    std::vector<float> max_acceleration_x;
    std::vector<float> max_acceleration_y;
    std::vector<float> max_acceleration_z;
    std::vector<float> max_acceleration_e;
    std::vector<float> max_feedrate_x;
    std::vector<float> max_feedrate_y;
    std::vector<float> max_feedrate_z;
    std::vector<float> max_feedrate_e;
    std::vector<float> max_jerk_x;
    std::vector<float> max_jerk_y;
    std::vector<float> max_jerk_z;
    std::vector<float> max_jerk_e;

    std::vector<float> max_acceleration_extruding;
    std::vector<float> max_acceleration_retracting;
    std::vector<float> max_acceleration_travel;

    std::vector<float> max_junction_deviation;

    std::vector<float> min_travel_rate;
    std::vector<float> min_extruding_rate;

    //
    // set to default all missing values
    //
    std::vector<std::string> validate();
    void reset();
};

struct FilamentsConfig
{
    std::vector<float> diameters;
    std::vector<float> densities;
    std::vector<float> costs;

    void reset();
};

struct ExtrudersConfig
{
    uint8_t count{ MIN_EXTRUDERS_COUNT };
    std::vector<Domain::Vec3f> offsets;
    std::vector<std::string> str_colors;
    std::vector<int> temps_config;
    std::vector<int> temps_first_layer_config;

    void reset();
};

typedef std::function<void(const std::string&)> ProcessorLogCallback;

struct ProcessorCallbacksConfig
{
    ProcessorLogCallback cb_log;
};

struct ProcessorConfig
{
    GCodeProducer producer{ GCodeProducer::Unknown };
    Domain::GCodeFlavor flavor{ Domain::GCodeFlavor::gcfRepRapSprinter };
    bool use_volumetric_e{ false };
    bool export_remaining_time_enabled{ false };
    bool stealth_time_estimator_enabled{ false };
    bool spiral_vase_enabled{ false };
    bool sequential_print{ false };
    bool do_M104_backtrace{ false };
    bool single_extruder_multi_material{ false };
    float z_offset{ 0.0f };
    float max_print_height{ 0.0f };
    float first_layer_height{ 0.0f };
    float parking_pos_retraction{ 0.0f };
    float extra_loading_move{ 0.0f };
    float kisslicer_toolchange_time_correction{ 0.0f };
    float filament_change_time{ 0.0f };
    std::string color_change_gcode;
    std::string pause_print_gcode;
    std::string template_custom_gcode;
    std::vector<Domain::Vec2f> bed_shape;
    FilamentsConfig filaments;
    ExtrudersConfig extruders;
    MachineLimitsConfig machine_limits;
    PrintSettings print_settings;
    ProcessorCallbacksConfig callbacks;

    void reset();
};

ProcessorConfig extract_processor_config_from_prusaslicer_gcode(const std::string& gcode);

// updated to AnkerMake Studio 1.5.24
ProcessorConfig extract_processor_config_from_ankermakestudio_gcode(const std::string& gcode);
// updated to BambuStudio 1.9.7.52
ProcessorConfig extract_processor_config_from_bambustudio_gcode(const std::string& gcode);
// updated to CraftWare 1.2.1.707
ProcessorConfig extract_processor_config_from_craftware_gcode(const std::string& gcode);
// updated to Cura 5.8.1
ProcessorConfig extract_processor_config_from_cura_gcode(const std::string& gcode);
// updated to KISSlicer 23.05
ProcessorConfig extract_processor_config_from_kisslicer_gcode(const std::string& gcode);
// updated to ideaMaker 5.1.2
ProcessorConfig extract_processor_config_from_ideamaker_gcode(const std::string& gcode);
// updated to Orcaslicer 2.1.1
ProcessorConfig extract_processor_config_from_orcaslicer_gcode(const std::string& gcode);
// updated to Simplify3D 5.1.2
ProcessorConfig extract_processor_config_from_simplify3d_gcode(const std::string& gcode);
// updated to SuperSlicer 2.5.59.13
ProcessorConfig extract_processor_config_from_superslicer_gcode(const std::string& gcode);
// updated to XDesktop 3.0.2
ProcessorConfig extract_processor_config_from_xdesktop_gcode(const std::string& gcode);

} // namespace Slic3r::Biz::libpgcode
