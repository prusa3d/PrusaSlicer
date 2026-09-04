#pragma once

#include "Slic3r/Domain/Config.hpp"
#include "Slic3r/Domain/PrinterTechnology.hpp"
#include "Slic3r/Domain/Percentage.hpp"
#include "Slic3r/Domain/Types.hpp"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace Slic3r::App {

struct PluginKeygenActionParams
{
    int key_size{2048};
    std::string pub_key_file;
    std::string priv_key_file;
};

struct PluginInitActionParams
{
    std::string dest_path{"."};
    std::optional<std::string> id;
    std::optional<std::string> name;
    std::optional<std::string> author;
    std::optional<std::string> description;
    std::optional<std::string> license;
    bool force{false};
};

struct PluginSignActionParams
{
    std::string bundle_path;
    std::string private_key_path;
    bool force{false};
};

struct ActionParams
{
    bool help_fff                           = false;
    bool help_sla                           = false;
    bool model_info                         = false;
    bool configuration_save                 = false;
    bool query_printer_models               = false;
    bool query_print_tool_filament_profiles = false;
    bool slice                              = false;
    bool export_stl                         = false;
    bool export_obj                         = false;
    bool export_3mf                         = false;
    bool export_gcode                       = false;
    bool export_sla                         = false;
    bool gcode_viewer                       = false;
    bool preset_updater_sync                = false;
    bool preset_updater_perform             = false;
    bool preset_updater_add_local           = false;
    bool preset_updater_remove_local        = false;
    bool preset_updater_list_repos          = false;
    bool preset_updater_switch_repo         = false;
    bool preset_updater_cleanup             = false;
    bool dump_json_model                    = false;
    bool generate_preset_cache              = false;
    std::variant<
        PluginInitActionParams,
        PluginKeygenActionParams,
        PluginSignActionParams,
        std::monostate>
        subcommand_action = std::monostate{};

    bool has_any_action() const
    {
        return help_fff
            || help_sla
            || model_info
            || configuration_save
            || query_printer_models
            || query_print_tool_filament_profiles
            || slice
            || export_stl
            || export_obj
            || export_3mf
            || export_gcode
            || export_sla
            || gcode_viewer
            || preset_updater_sync
            || preset_updater_perform
            || preset_updater_add_local
            || preset_updater_remove_local
            || preset_updater_list_repos
            || preset_updater_switch_repo
            || preset_updater_cleanup
            || dump_json_model
            || generate_preset_cache
            || !std::holds_alternative<std::monostate>(subcommand_action);
    }

    bool has_preset_updater_action() const
    {
        return preset_updater_sync
            || preset_updater_perform
            || preset_updater_add_local
            || preset_updater_remove_local
            || preset_updater_list_repos
            || preset_updater_switch_repo
            || preset_updater_cleanup;
    }

    bool has_subcommand_action() const
    {
        return !std::holds_alternative<std::monostate>(subcommand_action);
    }
};

struct InputParams
{
    std::vector<std::string> input_files; // Input files or URLs
    std::optional<std::string> config_file;
    std::vector<std::string> material_profile_presets;
    std::vector<std::string> tool_profile_presets;

    std::optional<std::string> print_profile_preset;
    std::optional<std::string> printer_profile_preset;
};

struct TransformParams
{
    std::optional<Domain::Vec2d> align_xy;
    std::optional<Domain::Vec2d> center;
    std::optional<double> cut_z;
    std::optional<bool> dont_arrange;

    std::optional<uint32_t> duplicate;
    std::optional<std::array<uint32_t, 2>> duplicate_grid;

    std::optional<bool> ensure_on_bed;
    std::optional<bool> merge;

    std::optional<Domain::Vec3d> rotation;

    std::optional<Domain::FloatOrPercentage> scale;
    std::optional<Domain::Vec3d> scale_to_fit;

    std::optional<bool> split;
};

struct MiscParams
{
    std::optional<std::string> datadir;
    std::optional<uint8_t> loglevel;
    std::optional<std::string> output;
    std::optional<std::string> config_model_json_file;

    std::optional<bool> delete_after_load;
    std::optional<bool> ignore_nonexistent_config;
    std::optional<bool> single_instance;
    std::optional<bool> webdev;
    std::optional<uint16_t> threads;

    std::optional<bool> opengl_aa;
    std::optional<bool> opengl_compatibility;
    std::optional<bool> opengl_debug;
    std::optional<std::pair<int, int>> opengl_version;
    std::optional<bool> sw_renderer;
};

class InitParams final
{
public:
    InitParams() = default;
    InitParams(int argc, char** argv);

    ActionParams action;
    InputParams input;
    TransformParams transform;
    MiscParams misc;

    std::vector<Domain::ConfigItem> config_overrides;

    int argc    = 0;
    char** argv = nullptr;

    std::optional<int> exit_code;
};

void init_paths(const InitParams& init_params);

void init_common();

} // namespace Slic3r::App
