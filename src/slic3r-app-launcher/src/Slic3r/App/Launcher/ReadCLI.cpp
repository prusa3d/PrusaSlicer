#include "Slic3r/App/Launcher/ReadCLI.hpp"

#include "Slic3r/Domain/Config.hpp"
#include "Slic3r/Domain/ConfigPack.hpp"
#include "Slic3r/Domain/Types.hpp"
#include "Slic3r/Semver.hpp"

#include <array>
#include <cassert>
#include <optional>
#include <thread>
#include <iostream>
#include <vector>
#include <string>

#include <boost/nowide/iostream.hpp>
#include <boost/optional.hpp>

#include <CLI/CLI.hpp>

using namespace Slic3r;
using namespace Slic3r::Domain;

namespace {

using ConstConfigBoxRefs = std::vector<std::reference_wrapper<const ConfigBox>>;

class CustomFormatter : public CLI::Formatter
{
private:
    bool m_show_full_fdm_help = false;
    bool m_show_full_sla_help = false;

public:
    std::string make_group(
        std::string group,
        bool is_positional,
        std::vector<const CLI::Option*> opts
    ) const override
    {
        if ((!m_show_full_fdm_help && group == "FDM configuration")
            || (!m_show_full_sla_help && group == "SLA configuration"))
        {
            return "";
        }

        std::string result = CLI::Formatter::make_group(group, is_positional, opts);

        // Append note after the input group.
        if (group == "Input" && !result.empty()) {
            result +=
                "\nNote: To load configuration from profiles, you need to set whole bunch of presets\n";
        }

        return result;
    }

    void set_show_full_fdm_help(const bool show_full_fdm_help = true)
    {
        m_show_full_fdm_help = show_full_fdm_help;
    }

    void set_show_full_sla_help(const bool show_full_sla_help = true)
    {
        m_show_full_sla_help = show_full_sla_help;
    }
};

CLI::Option* add_vec2d_option(
    CLI::App& app,
    const std::string& option_name,
    std::optional<Vec2d>& variable,
    const std::string& option_description = ""
)
{
    return app
        .add_option_function<std::array<double, 2>>(
            option_name,
            [&variable](const std::array<double, 2>& arr) { variable = Vec2d(arr[0], arr[1]); },
            option_description
        )
        ->delimiter(',');
}

CLI::Option* add_vec3d_option(
    CLI::App& app,
    const std::string& option_name,
    std::optional<Vec3d>& variable,
    const std::string& option_description = ""
)
{
    return app
        .add_option_function<std::array<double, 3>>(
            option_name,
            [&variable](const std::array<double, 3>& arr)
            { variable = Vec3d(arr[0], arr[1], arr[2]); },
            option_description
        )
        ->delimiter(',');
}

Percentage parse_percentage(const std::string& str)
{
    if (str.ends_with("%")) {
        const double value = std::stod(str.substr(0, str.length() - 1));
        return {Percentage{value}};
    } else {
        const double value = std::stod(str);
        return Percentage{value};
    }
}

FloatOrPercentage parse_float_or_percentage(const std::string& str)
{
    if (str.ends_with("%")) {
        const double value = std::stod(str.substr(0, str.length() - 1));
        return {Percentage{value}};
    } else {
        const double value = std::stod(str);
        return {value};
    }
}

void add_input_options(CLI::App& app, App::InitParams& params)
{
    app.option_defaults()->group("Input");

    app.add_option(
           "--load",
           params.input.config_file,
           "Load configuration from the specified single JSON file in the format produced by --save (identical to the configuration stored inside 3MF files)."
    )
        ->option_text("FILE");

    app.add_option(
           "--material-profile",
           params.input.material_profile_presets,
           "Name(s) of the material preset(s) used for slicing. Could be filaments or sla_material preset name(s) depending on printer technology."
    )
        ->option_text("ABCD")
        ->delimiter(','); // Material presets are comma-separated.

    app.add_option(
           "--tool-print-profile",
           params.input.tool_profile_presets,
           "Name(s) of the tool print preset(s) used for slicing."
    )
        ->option_text("ABCD")
        ->delimiter(','); // Tool presets are comma-separated.

    app.add_option(
           "--print-profile",
           params.input.print_profile_preset,
           "Name of the print preset used for slicing."
    )
        ->option_text("ABCD");

    app.add_option(
           "--printer-profile",
           params.input.printer_profile_preset,
           "Name of the printer preset used for slicing."
    )
        ->option_text("ABCD");

    app.add_option("Input-files", params.input.input_files, "Input files to process.")
        ->type_name("FILES");
}

void add_transform_options(CLI::App& app, App::InitParams& params)
{
    app.option_defaults()->group("Transform");

    add_vec2d_option(
        app,
        "--align-xy",
        params.transform.align_xy,
        "Align the model to the given point."
    )
        ->type_name("X,Y");

    add_vec2d_option(
        app,
        "--center",
        params.transform.center,
        "Center the print around the given center."
    )
        ->type_name("X,Y");

    app.add_option("--cut", params.transform.cut_z, "Cut model at the given Z.")->type_name("N");

    app.add_flag(
        "--dont-arrange",
        params.transform.dont_arrange,
        "Do not rearrange the given models before merging and keep their original XY coordinates."
    );

    app.add_option("--duplicate", params.transform.duplicate, "Multiply copies by this factor.")
        ->type_name("N");

    app.add_option(
           "--duplicate-grid",
           params.transform.duplicate_grid,
           "Multiply copies by creating a grid."
    )
        ->type_name("X,Y")
        ->delimiter(',');

    app.add_flag(
           "--ensure-on-bed,!--no-ensure-on-bed",
           params.transform.ensure_on_bed,
           "Lift the object above the bed when it is partially below. Enabled by default, use --no-ensure-on-bed to disable."
    )
        ->default_val(true);

    app.add_flag(
        "--merge,-m",
        params.transform.merge,
        "Arrange the supplied models in a plate and merge them in a single model in order to perform actions once."
    );

    auto update_rotation = [&params](size_t index, double value)
    {
        assert(index < 3);

        if (!params.transform.rotation.has_value()) {
            params.transform.rotation = Vec3d::Zero();
        }

        params.transform.rotation.value()(static_cast<int>(index)) = value;
    };

    app.add_option_function<double>(
           "--rotate-x",
           [update_rotation](double value) { update_rotation(0, value); },
           "Rotation angle around the X axis in degrees."
    )
        ->type_name("N");

    app.add_option_function<double>(
           "--rotate-y",
           [update_rotation](double value) { update_rotation(1, value); },
           "Rotation angle around the Y axis in degrees."
    )
        ->type_name("N");

    app.add_option_function<double>(
           "--rotate-z,--rotate",
           [update_rotation](double value) { update_rotation(2, value); },
           "Rotation angle around the Z axis in degrees."
    )
        ->type_name("N");

    app.add_option_function<std::string>(
           "--scale",
           [&params](const std::string& str)
           { params.transform.scale = parse_float_or_percentage(str); },
           "Scaling factor or percentage."
    )
        ->type_name("N");

    add_vec3d_option(
        app,
        "--scale-to-fit",
        params.transform.scale_to_fit,
        "Scale to fit the given volume."
    )
        ->type_name("X,Y,Z")
        ->check(CLI::PositiveNumber);

    app.add_flag(
        "--split",
        params.transform.split,
        "Detect unconnected parts in the given model(s) and split them into separate objects."
    );
}

void add_misc_options(CLI::App& app, App::InitParams& params)
{
    app.option_defaults()->group("Other options");

    // TODO: --config-compatibility is not implemented yet.

    app.add_option(
           "--datadir",
           params.misc.datadir,
           "Load and store settings at the given directory. This is useful for maintaining "
           "different profiles or including configurations from a network storage."
    )
        ->type_name("ABCD");

    app.add_flag(
        "--delete-after-load",
        params.misc.delete_after_load,
        "Delete files after loading."
    );

    app.add_flag(
        "--ignore-nonexistent-config",
        params.misc.ignore_nonexistent_config,
        "Do not fail if a file supplied to --load does not exist."
    );

    app.add_option(
           "--loglevel",
           params.misc.loglevel,
           "Sets logging sensitivity. 0:fatal, 1:error, 2:warning, 3:info, 4:debug, 5:trace "
           "For example. loglevel=2 logs fatal, error and warning level messages."
    )
        ->type_name("N")
        ->check(CLI::Range(0, 5));

    app.add_flag(
        "--opengl-aa",
        params.misc.opengl_aa,
        "Automatically select the highest number of samples for OpenGL antialiasing."
    );

    app.add_flag(
        "--opengl-compatibility",
        params.misc.opengl_compatibility,
        "Enable OpenGL compatibility profile"
    );

    app.add_flag(
        "--opengl-debug",
        params.misc.opengl_debug,
        "Activate OpenGL debug output on graphic cards which support it (OpenGL 4.3 or higher)"
    );

    app.add_option_function<std::string>(
           "--opengl-version",
           [&params](const std::string& opengl_version_str)
           {
               const Semver opengl_minimum    = Semver(3, 2, 0);
               boost::optional<Semver> semver = Semver::parse(opengl_version_str);
               if (semver.has_value() && (*semver) >= opengl_minimum) {
                   params.misc.opengl_version = std::make_pair(semver->maj(), semver->min());
                   // TODO: Add validation of supported OpenGL versions.
                   /*
                   if (std::find(GUI::OpenGLVersions::core.begin(), GUI::OpenGLVersions::core.end(), std::make_pair(version.first, version.second)) == GUI::OpenGLVersions::core.end()) {
                       params.misc.opengl_version = { 0, 0 };
                       boost::nowide::cerr << "Required OpenGL version " << opengl_version_str << " not recognized.\n Option 'opengl-version' ignored." << std::endl;
                   }
                   */
               } else {
                   boost::nowide::cerr
                       << "Required OpenGL version "
                       << opengl_version_str
                       << " is invalid. Must be greater than or equal to "
                       << opengl_minimum.to_string()
                       << "\n Option 'opengl-version' ignored."
                       << std::endl;
               }
           },
           "Select a specific version of OpenGL"
    )
        ->type_name("ABCD");

    app.add_option(
           "--output,-o",
           params.misc.output,
           "The file where the output will be written (if not specified, it will be based on the input file)."
    )
        ->type_name("ABCD");

    app.add_flag(
        "--single-instance,!--no-single-instance,--single-instance-on-url",
        params.misc.single_instance,
        "If enabled, the command line arguments are sent to an existing instance of GUI PrusaSlicer, "
        "or an existing PrusaSlicer window is activated. Overrides the \"single_instance\" configuration "
        "value from application preferences."
    );

    app.add_flag(
        "--sw-renderer",
        params.misc.sw_renderer,
        "Render with a software renderer. The bundled MESA software renderer is loaded "
        "instead of the default OpenGL driver."
    );

    app.add_flag(
        "--webdev",
        params.misc.webdev,
        "Enable webdev tools in webview components."
    );

    app.add_option(
           "--threads",
           params.misc.threads,
           "Sets the maximum number of threads the slicing process will use. "
           "If not defined, it will be decided automatically."
    )
        ->type_name("N")
        ->check(CLI::Range(1, static_cast<int>(std::thread::hardware_concurrency())));
}

void add_action_options(CLI::App& app, App::InitParams& params)
{
    app.option_defaults()->group("Actions");

    // Export options
    app.add_flag("--export-3mf", params.action.export_3mf, "Export the model(s) as 3MF.");

    app.add_flag(
        "--export-gcode,--gcode,-g",
        params.action.export_gcode,
        "Slice the model and export toolpaths as G-code."
    );

    app.add_flag("--export-obj", params.action.export_obj, "Export the model(s) as OBJ.");

    app.add_flag(
        "--export-sla,--sla",
        params.action.export_sla,
        "Slice the model and export SLA printing layers as PNG."
    );

    app.add_flag("--export-stl", params.action.export_stl, "Export the model(s) as STL.");

    app.add_flag(
        "--gcodeviewer",
        params.action.gcode_viewer,
        "Visualize an already sliced and saved G-code."
    );

    // Help options
    app.add_flag(
        "--help-fff",
        params.action.help_fff,
        "Show the full list of print/G-code configuration options."
    );

    app.add_flag(
        "--help-sla",
        params.action.help_sla,
        "Show the full list of SLA print configuration options."
    );

    // Other actions
    app.add_flag(
        "--info",
        params.action.model_info,
        "Write information about the model to the console."
    );

    app.add_flag(
        "--query-print-tool-filament-profiles",
        params.action.query_print_tool_filament_profiles,
        "Get list of print profiles, tool print profiles and filament profiles for the selected 'printer-profile' into JSON. "
        "Note: To print out JSON into file use 'output' option. To specify configuration folder use 'datadir' option."
    );

    app.add_flag(
        "--query-printer-models",
        params.action.query_printer_models,
        "Get list of installed printer models (both FFF and SLA) into JSON. "
        "To print out JSON into file use 'output' option. To specify configuration folder use 'datadir' option."
    );

    app.add_option_function<std::string>(
           "--save",
           [&params](const std::string& file)
           {
               params.action.configuration_save = true;
               if (!params.misc.output.has_value()) {
                   params.misc.output = file;
               }
           },
           "Save configuration to the specified file."
    )
        ->option_text("ABCD");

    app.add_flag(
        "--slice,-s",
        params.action.slice,
        "Slice the model and export the result. The printer technology (FFF or SLA) is determined by the selected printer profile."
    );

    // Allow using at most one action simultaneously from this group.
    auto& preset_updater_action_group = *app.add_option_group("Preset Updater");
    preset_updater_action_group.required(false);
    preset_updater_action_group.require_option(0, 1);

    preset_updater_action_group.add_flag(
        "--preset-update",
        params.action.preset_updater_perform,
        "Preset Updater performs all available reconfigurations. This action includes downloading possible update files from servers for all currently selected sources. After this action all profiles should be up-to-date."
    );

    preset_updater_action_group.add_flag(
        "--preset-update-list",
        params.action.preset_updater_sync,
        "Preset Updater performs check of reconfigurations and prints result in json format. This includes downloading possible update files from servers for all currently selected sources."
    );

    preset_updater_action_group
        .add_option_function<std::string>(
            "--preset-update-add-local",
            [&params](const std::string& file)
            {
                params.action.preset_updater_add_local = true;
                if (!params.misc.output.has_value()) {
                    params.misc.output = file;
                }
            },
            "Adds local source to Preset Updater sources. The source is specified by a path to a zip file."
        )
        ->option_text("ABCD");

    preset_updater_action_group
        .add_option_function<std::string>(
            "--preset-update-remove-local",
            [&params](const std::string& file)
            {
                params.action.preset_updater_remove_local = true;
                if (!params.misc.output.has_value()) {
                    params.misc.output = file;
                }
            },
            "Removes local source from Preset Updater by UUID."
        )
        ->option_text("ABCD");

    preset_updater_action_group.add_flag(
        "--preset-update-sources",
        params.action.preset_updater_list_repos,
        "Lists all sources of Preset Updater in json format."
    );

    preset_updater_action_group
        .add_option_function<std::string>(
            "--preset-update-select-source",
            [&params](const std::string& file)
            {
                params.action.preset_updater_switch_repo = true;
                if (!params.misc.output.has_value()) {
                    params.misc.output = file;
                }
            },
            "Selects or unselects Preset Updater Source by given UUID. If source is selected, automatically unselects other sources with same id."
        )
        ->option_text("ABCD");

    preset_updater_action_group.add_flag(
        "--preset-update-cleanup",
        params.action.preset_updater_cleanup,
        "Deletes all files previously staged for update by preset-update-list."
    );

    app.add_option_function<std::string>(
           "--export-config-schema",
           [&params](const std::string& file)
           {
               params.action.dump_json_model = true;
               if (!params.misc.config_model_json_file.has_value()) {
                   params.misc.config_model_json_file = file;
               }
           },
           "Save preset/config data model description into a JSON file."
    )
        ->option_text("FILE");

    app.add_flag(
           "--generate-preset-cache",
           params.action.generate_preset_cache,
           "Generate preset bundle cache for faster debug build startup."
    )
        ->group("");
}

void add_config_item_override(
    CLI::App& app,
    App::InitParams& params,
    const ConfigBox& config_box,
    const ConfigItem& config_item
)
{
    const ConfigItemDef& config_item_def = config_item.def();
    const std::string& config_item_key   = config_item_def.name;
    const std::string option_name        = [&config_item_key]() -> std::string
    {
        std::string name = config_item_key;
        std::replace(name.begin(), name.end(), '_', '-');
        return name;
    }();
    const std::string option_name_with_prefix = "--" + option_name;
    const std::string option_description      = [&config_item_def]() -> std::string
    {
        const ConfigValue option_default_value = config_item_def.init_fn();

        std::string option_default_value_str;
        option_default_value.visit(
            [&option_default_value_str](const auto& value)
            {
                using ValueType = std::remove_cvref_t<decltype(value)>;

                if constexpr (std::is_same_v<ValueType, EnumWrapper>) {
                    option_default_value_str = value.get_string();
                } else if constexpr (std::is_same_v<ValueType, std::string>) {
                    option_default_value_str = value;
                } else if constexpr (std::is_same_v<ValueType, int>
                                     || std::is_same_v<ValueType, double>)
                {
                    option_default_value_str = std::to_string(value);
                } else if constexpr (std::is_same_v<ValueType, bool>) {
                    option_default_value_str = value ? "true" : "false";
                }
            }
        );

        return config_item_def.tooltip + " (default: " + option_default_value_str + ")";
    }();

    if (app.get_option_no_throw(option_name_with_prefix) != nullptr) {
        // This option was already defined (for the example same option name is used for FDM and SLA).
        return;
    };

    config_item.visit(
        [&app,
         &option_name_with_prefix,
         &option_name,
         &option_description,
         &params,
         &config_box,
         config_item_key,
         &config_item_def](auto&& item_value)
        {
            using ValueType = std::remove_cvref_t<decltype(item_value)>;
            if constexpr (std::is_same_v<ValueType, EnumWrapper>) {
                app.add_option_function<std::string>(
                       option_name_with_prefix,
                       [&params, &config_box, config_item_key, &config_item_def](
                           const std::string& value
                       )
                       {
                           ConfigItem parsed_config_item{config_item_def, config_box.location};
                           parsed_config_item.set(value);
                           params.config_overrides.emplace_back(parsed_config_item);
                       },
                       option_description
                )
                    ->option_text("ABCD");
            } else if constexpr (std::is_same_v<ValueType, Domain::EnumVectorWrapper>) {
                app.add_option_function<std::vector<std::string>>(
                       option_name_with_prefix,
                       [&params, &config_box, config_item_key, &config_item_def](
                           const std::vector<std::string>& values
                       )
                       {
                           ConfigItem parsed_config_item{config_item_def, config_box.location};
                           parsed_config_item.set(values);
                           params.config_overrides.emplace_back(parsed_config_item);
                       },
                       option_description
                )
                    ->option_text("ABCD")
                    ->delimiter(',');
            } else if constexpr (std::is_same_v<ValueType, Domain::Percentage>) {
                app.add_option_function<std::string>(
                       option_name_with_prefix,
                       [&params, &config_box, config_item_key, &config_item_def](
                           const std::string& value
                       )
                       {
                           ConfigItem parsed_config_item{config_item_def, config_box.location};
                           parsed_config_item.set(parse_percentage(value));
                           params.config_overrides.emplace_back(parsed_config_item);
                       },
                       option_description
                )
                    ->type_name("N");
            } else if constexpr (std::is_same_v<ValueType, Domain::FloatOrPercentage>) {
                app.add_option_function<std::string>(
                       option_name_with_prefix,
                       [&params, &config_box, config_item_key, &config_item_def](
                           const std::string& value
                       )
                       {
                           ConfigItem parsed_config_item{config_item_def, config_box.location};
                           parsed_config_item.set(parse_float_or_percentage(value));
                           params.config_overrides.emplace_back(parsed_config_item);
                       },
                       option_description
                )
                    ->type_name("N");
            } else if constexpr (std::is_same_v<ValueType, bool>) {
                app.add_flag_function(
                    option_name_with_prefix + ",--no-" + option_name,
                    [&params, &config_box, config_item_key, &config_item_def](const size_t& value)
                    {
                        ConfigItem parsed_config_item{config_item_def, config_box.location};
                        parsed_config_item.set(static_cast<bool>(value));
                        params.config_overrides.emplace_back(parsed_config_item);
                    },
                    option_description
                );
            } else {
                CLI::Option* option = app.add_option_function<ValueType>(
                    option_name_with_prefix,
                    [&params, &config_box, config_item_key, &config_item_def](
                        const ValueType& value
                    )
                    {
                        ConfigItem parsed_config_item{config_item_def, config_box.location};
                        parsed_config_item.set(value);
                        params.config_overrides.emplace_back(parsed_config_item);
                    },
                    option_description
                );

                if constexpr (std::is_same_v<ValueType, std::string>) {
                    option->option_text("ABCD");
                } else if constexpr (std::is_same_v<ValueType, std::vector<std::string>>) {
                    option->option_text("ABCD")->delimiter(',');
                } else if constexpr (std::is_same_v<ValueType, Vec2d>
                                     || std::is_same_v<ValueType, std::vector<Vec2d>>)
                {
                    option->type_name("X,Y")->delimiter(',');
                } else {
                    option->delimiter(',');
                }
            }
        }
    );
}

void add_config_boxes_overrides(
    CLI::App& app,
    App::InitParams& params,
    const ConstConfigBoxRefs& config_boxes
)
{
    for (const ConfigBox& config_box : config_boxes) {
        for (const ConfigItem& config_item : config_box.items.all_items()) {
            if (const ConfigItemDef& config_item_def = config_item.def();
                config_item_def.cli == ConfigItemDef::nocli
                || config_item_def.category == ConfigItemDef::Category::Hidden)
            {
                continue;
            }

            add_config_item_override(app, params, config_box, config_item);
        }
    }
}

void add_fdm_config_boxes_overrides(CLI::App& app, App::InitParams& params)
{
    app.option_defaults()->group("FDM configuration");

    const ConfigPackFDM fdm_config_pack;
    const ConstConfigBoxRefs config_boxes = {
        fdm_config_pack.print,
        fdm_config_pack.printer,
        fdm_config_pack.tool.front(),
        fdm_config_pack.filament.front(),
        fdm_config_pack.project
    };

    add_config_boxes_overrides(app, params, config_boxes);
}

void add_sla_config_boxes_overrides(CLI::App& app, App::InitParams& params)
{
    app.option_defaults()->group("SLA configuration");

    const ConfigPackSLA sla_config_pack;
    const ConstConfigBoxRefs config_boxes = {
        sla_config_pack.sla_print_settings,
        sla_config_pack.sla_printer_settings,
        sla_config_pack.sla_material_settings
    };

    add_config_boxes_overrides(app, params, config_boxes);
}

template <typename ActionParamsType>
auto make_string_opt_parser(
    App::InitParams& params,
    const std::function<std::string&(ActionParamsType&)>& field_getter
)
{
    return [field_getter,&params](const std::vector<std::string>& opts) -> bool
    {
        if (opts.size() != 1) {
            return false;
        }
        auto& action_params = std::get<ActionParamsType>(params.action.subcommand_action);
        auto& field = field_getter(action_params);
        field = opts.at(0);
        return true;
    };
}

void add_plugin_subcommand(CLI::App& app, App::InitParams& params)
{
    using namespace Slic3r::App;
    auto* plugin_app = app.add_subcommand("plugin", "Plugin tools");

    // Command: plugin init
    {
        auto* plugin_init_app = plugin_app->add_subcommand("init", "Bootstrap plugin bundle");
        plugin_init_app->preparse_callback([&params](size_t)
        {
            params.action.subcommand_action = PluginInitActionParams{};
        });
        plugin_init_app->add_flag_callback(
            "-f,--force",
            [&params]
            {
                auto& action_params =
                    std::get<PluginInitActionParams>(params.action.subcommand_action);
                action_params.force = true;
            },
            "Remove old artifacts if encountered, or suppress warnings being treated as errors"
        );
        plugin_init_app->add_option(
            "-d",
            make_string_opt_parser<PluginInitActionParams>(
                params,
                [](auto& action_params) -> auto& { return action_params.dest_path; }
            ),
            "Path to the directory where plugin directory will be created and populated"
        )->default_str(".");
        plugin_init_app->add_option(
            "bundle_id",
            make_string_opt_parser<PluginInitActionParams>(
                params,
                [](auto& action_params) -> auto&
                {
                    if (!action_params.id.has_value()) {
                        action_params.id = "";
                    }
                    return action_params.id.value();
                }
            ),
            "An ID of newly created plugin bundle"
        );
    }

    // Command: plugin keygen
    {
        auto* plugin_keygen_app =
            plugin_app->add_subcommand("keygen", "Generate new public and private key pair");
        plugin_keygen_app->preparse_callback([&params](size_t)
        {
            params.action.subcommand_action = PluginKeygenActionParams{};
        });
        plugin_keygen_app->add_option(
            "-k,--keysize",
            [&](const auto& opts) -> bool
            {
                if (opts.size() != 1) {
                    return false;
                }
                size_t processed_chars = 0;

                auto& action_params =
                    std::get<PluginKeygenActionParams>(params.action.subcommand_action);
                action_params.key_size = std::stoi(opts.at(0), &processed_chars);
                if (processed_chars == 0 || processed_chars != opts.at(0).size()) {
                    return false;
                }

                if (action_params.key_size < 1024
                    || (action_params.key_size & (action_params.key_size - 1)) != 0
                    || action_params.key_size > 8192)
                {
                    return false;
                }

                return true;
            },
            "Set key size, must be power-of-two and in range [1024, 8192]"
        )->default_str("2048");
        plugin_keygen_app->add_option(
            "-p,--public",
            make_string_opt_parser<PluginKeygenActionParams>(
                params,
                [](auto& action_params) -> auto&
                { return action_params.pub_key_file; }
            ),
            "Path to generated private key including file name"
        )->required();
        plugin_keygen_app
            ->add_option(
                "-P,--private",
                make_string_opt_parser<PluginKeygenActionParams>(
                    params,
                    [](auto& action_params) -> auto& { return action_params.priv_key_file; }
                ),
                "Path to generated public key including file name"
            )
            ->required();
    }

    // Command: plugin sign
    {
        auto* plugin_sign_app = plugin_app->add_subcommand("sign", "Sign existing plugin bundle");
        plugin_sign_app->preparse_callback([&params](size_t)
        {
            params.action.subcommand_action = PluginSignActionParams{};
        });
        plugin_sign_app->add_flag_callback(
            "-f,--force",
            [&params]
            {
                auto& action_params =
                    std::get<PluginSignActionParams>(params.action.subcommand_action);
                action_params.force = true;
            },
            "Remove old artifacts if encountered, or suppress warnings being treated as errors"
        );
        plugin_sign_app->add_option(
            "-P,--private",
            make_string_opt_parser<PluginSignActionParams>(
                params,
                [](auto& action_params) -> auto& { return action_params.private_key_path; }
            ),
            "Path to private key to sign the plugin bundle with"
        )->required();
        plugin_sign_app->add_option(
            "bundle_path",
            make_string_opt_parser<PluginSignActionParams>(
                params,
                [](auto& action_params) -> auto& { return action_params.bundle_path; }
            ),
            "Path to the bundle"
        )->required();
    }

}

} // namespace

namespace Slic3r::App::Launcher {

InitParams read_cli(::CLI::App& app, const std::string& app_version, const int argc, char** argv)
{
    InitParams init_params{argc, argv};

    std::shared_ptr<CustomFormatter> custom_formatter = std::make_shared<CustomFormatter>();
    app.formatter(custom_formatter);

    app.set_version_flag(
        "-v,--version",
        app_version,
        "Display PrusaSlicer version information and exit."
    );
    app.set_help_flag("--help,-h", "Show this help message and exit.");

    ::add_input_options(app, init_params);
    ::add_action_options(app, init_params);
    ::add_transform_options(app, init_params);
    ::add_misc_options(app, init_params);
    ::add_fdm_config_boxes_overrides(app, init_params);
    ::add_sla_config_boxes_overrides(app, init_params);
    ::add_plugin_subcommand(app, init_params);

    app.footer(
        "\nPrint options are processed in the following order:\n"
        "\t1) Config keys from the command line, for example --fill-pattern=stars\n"
        "\t           (highest priority, overwrites everything below)\n"
        "\t2) Config files loaded with --load\n"
        "\t3) Config values loaded from 3mf files\n\n"
        "Run --help-fff / --help-sla to see the full listing of print options."
    );

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& e) {
        init_params.exit_code = app.exit(e, std::cerr, std::cerr);
        return init_params;
    }

#ifdef __linux__
    if (init_params.misc.webdev.has_value()) {
        std::cerr << "Error: --webdev is not supported on this system (Linux)." << std::endl;
        init_params.exit_code = 1;
        return init_params;
    }
#endif

    if (init_params.action.help_fff || init_params.action.help_sla) {
        custom_formatter->set_show_full_fdm_help(init_params.action.help_fff);
        custom_formatter->set_show_full_sla_help(init_params.action.help_sla);
        std::cerr << app.help() << std::endl;
        init_params.exit_code = 0;
    }

    if (init_params.transform.cut_z.has_value() && !init_params.action.has_any_action()) {
        // Cutting transformations are setting an "export" action.
        init_params.action.export_stl = true;
    }

    return init_params;
}

} // namespace Slic3r::App::Launcher
