#include "Slic3r/App/CLI/LoadPrintData.hpp"

#include "Slic3r/App/CLI/CLIRuntime.hpp"
#include "Slic3r/App/CLI/CLIUtils.hpp"
#include "Slic3r/App/CLI/ProcessActions.hpp"
#include "Slic3r/App/CLI/ProcessTransform.hpp"
#include "Slic3r/App/CLI/ProfilePresetSelection.hpp"
#include "Slic3r/App/Init.hpp"
#include "Slic3r/Biz/Algorithms/Point.hpp"
#include "Slic3r/Biz/Config/ConfigLoad.hpp"
#include "Slic3r/Biz/FileLoadingLogic.hpp"
#include "Slic3r/Biz/Preset/PresetInteractor.hpp"
#include "Slic3r/Domain/Bed.hpp"
#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Domain/Project.hpp"

#include <algorithm>
#include <fstream>
#include <memory>
#include <string>
#include <variant>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem.hpp>
#include <boost/nowide/iostream.hpp>

#include <nlohmann/json.hpp>

#include <spdlog/spdlog.h>

using Slic3r::Biz::IProjectsChangedListener;
using Slic3r::Biz::ProjectInteractor;
using Slic3r::Biz::Config::PresetAndConfig;
using Slic3r::Biz::Preset::PresetInteractor;
using Slic3r::Domain::BedInstance;
using Slic3r::Domain::BedRef;
using Slic3r::Domain::ConfigContainer;
using Slic3r::Domain::ConfigItem;
using Slic3r::Domain::ConfigLocation;
using Slic3r::Domain::ConfigPack;
using Slic3r::Domain::ConfigPackFDM;
using Slic3r::Domain::ConfigPackSLA;
using Slic3r::Domain::ConstFindResult;
using Slic3r::Domain::FDMConfigLocation;
using Slic3r::Domain::Model;
using Slic3r::Domain::PrinterTechnology;
using Slic3r::Domain::Project;
using Slic3r::Domain::SelectionId;
using Slic3r::Domain::SLAConfigLocation;
using Slic3r::Domain::Vec2d;
using Slic3r::Domain::Preset::HwPrinterConfig;

using namespace Slic3r::Biz;

namespace Slic3r::App::CLI {

static std::optional<PresetAndConfig> load_config_file(const std::string& file_path)
{
    nlohmann::ordered_json json_document;
    try {
        std::ifstream file(file_path);
        if (!file.is_open()) {
            boost::nowide::cerr
                << "Error while reading JSON config from "
                << file_path
                << std::endl;
            return std::nullopt;
        }

        file >> json_document;
    } catch (const std::exception& exception) {
        boost::nowide::cerr
            << "Error while reading config file \""
            << file_path
            << "\": "
            << exception.what()
            << std::endl;
        return std::nullopt;
    }

    tl::expected<PresetAndConfig, std::string> preset_and_config =
        Config::load_preset_and_config(json_document);
    if (!preset_and_config.has_value()) {
        boost::nowide::cerr << file_path << ": " << preset_and_config.error() << std::endl;
        return std::nullopt;
    }

    return std::move(preset_and_config.value());
}

/**
 * @brief Creates a new project from a preset built from the given metadata and configuration.
 *
 * @return ID of the created project, or std::nullopt on failure.
 */
static std::optional<SelectionId>
add_project_with_selected_preset(CLIRuntime& runtime, const PresetAndConfig& preset_and_config)
{
    const tl::expected<SelectionId, std::string> project_id =
        runtime.project_interactor().new_project_with_preset(
            preset_and_config.preset_metadata,
            preset_and_config.config_pack
        );

    if (!project_id.has_value()) {
        boost::nowide::cerr << project_id.error() << std::endl;
        return std::nullopt;
    }

    return project_id.value();
}

/**
 * @brief Loads the geometry of the given file into a new project.
 *
 * @return ID of the created project, or std::nullopt when the file should be skipped.
 */
static std::optional<SelectionId> load_geometry_project(
    CLIRuntime& runtime,
    const std::string& input_file,
    const std::optional<PresetAndConfig>& base_preset_config
)
{
    ProjectInteractor& project_interactor = runtime.project_interactor();

    tl::expected<Model, std::string> model_data =
        FileLoadingLogic::read_model_from_file(input_file, nullptr);
    if (!model_data.has_value()) {
        boost::nowide::cerr << "Error: " + model_data.error() << std::endl;
        return std::nullopt;
    }

    if (model_data.value().objects.empty()) {
        boost::nowide::cerr << "Error: file is empty: " << input_file << std::endl;
        return std::nullopt;
    }

    std::optional<SelectionId> project_id;
    if (base_preset_config.has_value()) {
        project_id = add_project_with_selected_preset(runtime, base_preset_config.value());
        if (!project_id.has_value()) {
            return std::nullopt;
        }
    } else {
        project_id = project_interactor.new_project();
    }

    project_interactor.scene_interactor().add_new_objects(model_data.value().objects);

    return project_id;
}

/**
 * @brief Loads the geometry and configuration of the given 3MF file into a new project.
 *
 * @return ID of the loaded project, or std::nullopt on failure.
 */
static std::optional<SelectionId>
load_3mf_project(CLIRuntime& runtime, const std::string& input_file)
{
    ProjectInteractor& project_interactor = runtime.project_interactor();

    SPDLOG_INFO("Loading project from the 3MF file {}.", input_file);

    ProjectLoadResultListener load_result_listener;
    project_interactor.add_listener<IProjectsChangedListener>(&load_result_listener);

    project_interactor.load_project(boost::filesystem::path{input_file});
    runtime.wait_until([&load_result_listener]() { return load_result_listener.finished(); });

    project_interactor.remove_listener<IProjectsChangedListener>(&load_result_listener);

    if (load_result_listener.load_error.has_value()) {
        boost::nowide::cerr
            << input_file
            << ": "
            << load_result_listener.load_error.value()
            << std::endl;
        return std::nullopt;
    }

    SPDLOG_INFO(
        "Project from the 3MF file {} was loaded as project {}.",
        input_file,
        load_result_listener.loaded_project_id.value()
    );

    return load_result_listener.loaded_project_id;
}

static bool process_input_files(
    CLIRuntime& runtime,
    std::vector<SelectionId>& project_ids,
    const InitParams& init_params,
    const std::optional<PresetAndConfig>& base_preset_config
)
{
    for (const std::string& input_file : init_params.input.input_files) {
        if (boost::starts_with(input_file, "prusaslicer://")) {
            continue;
        }

        if (!boost::filesystem::exists(input_file)) {
            boost::nowide::cerr << "No such file: " << input_file << std::endl;
            return false;
        }

        try {
            std::optional<SelectionId> project_id;
            if (has_full_config_from_profiles(init_params)
                || !FileLoadingLogic::is_project_file(input_file))
            {
                // We have a full bunch of options from profiles set, so just load a geometry.
                project_id = load_geometry_project(runtime, input_file, base_preset_config);
                if (!project_id.has_value()) {
                    continue;
                }
            } else {
                project_id = load_3mf_project(runtime, input_file);
                if (!project_id.has_value()) {
                    return false;
                }
            }

            project_ids.push_back(project_id.value());
        } catch (const std::exception& exception) {
            boost::nowide::cerr << input_file << ": " << exception.what() << std::endl;
            return false;
        }
    }

    return true;
}

static bool apply_profile_presets(CLIRuntime& runtime, const InitParams& init_params)
{
    const ProfilePresetSelectionRequest selection_request{
        .printer_profile_name   = init_params.input.printer_profile_preset.value_or(std::string{}),
        .print_profile_name     = init_params.input.print_profile_preset.value_or(std::string{}),
        .material_profile_names = init_params.input.material_profile_presets,
        .tool_profile_names     = init_params.input.tool_profile_presets
    };

    const std::optional<std::string> selection_error = select_profile_presets_by_name(
        runtime.project_interactor().preset_interactor(),
        selection_request
    );
    if (selection_error.has_value()) {
        boost::nowide::cerr
            << "Error while loading config from profiles: "
            << selection_error.value()
            << std::endl;
        return false;
    }

    return true;
}

static void apply_overriding_config_value(
    PresetInteractor& preset_interactor,
    const ConfigItem& config_item_override,
    const ConfigLocation& override_location,
    const size_t slot_idx
)
{
    ConfigItem override_item{config_item_override.def(), override_location};
    preset_interactor.set_item_value(override_item, config_item_override.value(), {slot_idx});
    preset_interactor.set_item_override(override_item, true, slot_idx);
}

static size_t
overriding_config_slot_count(const ConfigLocation& location, const HwPrinterConfig& hw_config)
{
    if (const FDMConfigLocation* fdm_location = std::get_if<FDMConfigLocation>(&location)) {
        if (*fdm_location == FDMConfigLocation::Tool) {
            return hw_config.tool_count;
        }
        if (*fdm_location == FDMConfigLocation::Filament) {
            return hw_config.material_slot_count();
        }
    } else if (const SLAConfigLocation* sla_location = std::get_if<SLAConfigLocation>(&location)) {
        if (*sla_location == SLAConfigLocation::Material) {
            return 1;
        }
    }

    return 0;
}

static bool apply_config_overrides(CLIRuntime& runtime, const InitParams& init_params)
{
    ProjectInteractor& project_interactor = runtime.project_interactor();
    PresetInteractor& preset_interactor   = project_interactor.preset_interactor();
    ConfigContainer& config_container     = project_interactor.selected_config_container();

    const PrinterTechnology selected_technology = config_container.selected_preset().technology();

    for (const ConfigItem& config_item_override : init_params.config_overrides) {
        const ConfigLocation& location = config_item_override.def().location;
        const std::string& option_name = config_item_override.def().name;

        // Tool/Filament overrides are applied to every slot.
        size_t slot_count = 1;

        if (selected_technology == PrinterTechnology::FFF) {
            if (!std::holds_alternative<FDMConfigLocation>(location)) {
                boost::nowide::cerr
                    << "Error: PrinterTechnology::FFF doesn't contains configuration key: "
                        + option_name
                    << std::endl;
                continue;
            }

            const FDMConfigLocation& fdm_location = std::get<FDMConfigLocation>(location);
            if (fdm_location == FDMConfigLocation::Project) {
                config_container.project_settings()
                    .items.opt(option_name)
                    .set(config_item_override.value());
                continue;
            }

            if (fdm_location != FDMConfigLocation::Printer
                && fdm_location != FDMConfigLocation::Print
                && fdm_location != FDMConfigLocation::Tool
                && fdm_location != FDMConfigLocation::Filament)
            {
                boost::nowide::cerr
                    << "Error: Unsupported location of configuration key: " + option_name
                    << std::endl;
                continue;
            }

            const HwPrinterConfig& hw_config = config_container.selected_preset().hw_config;
            if (fdm_location == FDMConfigLocation::Tool) {
                slot_count = hw_config.tool_count;
            } else if (fdm_location == FDMConfigLocation::Filament) {
                slot_count = hw_config.material_slot_count();
            }
        } else {
            if (!std::holds_alternative<SLAConfigLocation>(location)) {
                boost::nowide::cerr
                    << "Error: PrinterTechnology::SLA doesn't contains configuration key: "
                        + option_name
                    << std::endl;
                continue;
            }

            const SLAConfigLocation& sla_location = std::get<SLAConfigLocation>(location);
            if (sla_location != SLAConfigLocation::Printer
                && sla_location != SLAConfigLocation::Print
                && sla_location != SLAConfigLocation::Material)
            {
                boost::nowide::cerr
                    << "Error: Unsupported location of configuration key: " + option_name
                    << std::endl;
                continue;
            }
        }

        for (size_t slot_idx = 0; slot_idx < slot_count; ++slot_idx) {
            preset_interactor
                .set_item_value(config_item_override, config_item_override.value(), {slot_idx});
        }

        const HwPrinterConfig& hw_config = config_container.selected_preset().hw_config;
        for (const ConfigLocation& override_location : config_item_override.def().overrides_in) {
            const size_t override_slot_count =
                overriding_config_slot_count(override_location, hw_config);
            for (size_t slot_idx = 0; slot_idx < override_slot_count; ++slot_idx) {
                apply_overriding_config_value(
                    preset_interactor,
                    config_item_override,
                    override_location,
                    slot_idx
                );
            }
        }
    }

    return true;
}

/**
 * @brief Centers every given project on its selected bed.
 */
static void
center_projects_on_selected_bed(CLIRuntime& runtime, const std::vector<SelectionId>& project_ids)
{
    ProjectInteractor& project_interactor = runtime.project_interactor();

    for (const SelectionId project_id : project_ids) {
        project_interactor.select_project(project_id);

        const BedRef selected_bed_ref =
            project_interactor.scene_interactor().bed_selection().last_selected_bed();
        const ConfigContainer* config_container =
            project_interactor.selected_project().find_config_container(
                selected_bed_ref.config_container_id
            );
        if (config_container == nullptr) {
            continue;
        }

        const BedInstance& selected_bed_instance =
            config_container->find_bed_instance(selected_bed_ref.instance_id);
        const Vec2d bed_center = config_container->bed().center()
            + Algorithms::Point::to_2d(selected_bed_instance.transformation.get_offset());

        center_selected_project_around_point(runtime, bed_center);
    }
}

bool load_print_data(
    CLIRuntime& runtime,
    std::vector<SelectionId>& project_ids,
    const InitParams& init_params
)
{
    const bool profiles_requested         = has_full_config_from_profiles(init_params);
    ProjectInteractor& project_interactor = runtime.project_interactor();

    std::optional<PresetAndConfig> base_preset_config;
    if (!profiles_requested && init_params.input.config_file.has_value()) {
        const std::string& config_file_path = init_params.input.config_file.value();
        if (!boost::filesystem::exists(config_file_path)) {
            if (!init_params.misc.ignore_nonexistent_config.value_or(false)) {
                boost::nowide::cerr << "No such file: " << config_file_path << std::endl;
                return false;
            }
        } else {
            base_preset_config = load_config_file(config_file_path);
            if (!base_preset_config.has_value()) {
                return false;
            }
        }
    }

    if (!process_input_files(runtime, project_ids, init_params, base_preset_config)) {
        return false;
    }

    std::vector<SelectionId> projects_to_configure = project_ids;
    if (projects_to_configure.empty()) {
        if (base_preset_config.has_value()) {
            const std::optional<SelectionId> config_holder_project_id =
                add_project_with_selected_preset(runtime, base_preset_config.value());

            if (!config_holder_project_id.has_value()) {
                return false;
            }

            projects_to_configure.push_back(config_holder_project_id.value());
        } else {
            projects_to_configure.push_back(project_interactor.new_project());
        }
    }

    for (const SelectionId project_id : projects_to_configure) {
        project_interactor.select_project(project_id);

        if (profiles_requested) {
            if (!apply_profile_presets(runtime, init_params)) {
                return false;
            }
        }

        if (!apply_config_overrides(runtime, init_params)) {
            return false;
        }
    }

    if (!init_params.transform.dont_arrange.value_or(false)) {
        center_projects_on_selected_bed(runtime, project_ids);
    }

    return true;
}

bool
is_needed_post_processing(const CLIRuntime& runtime, const std::vector<SelectionId>& project_ids)
{
    const ProjectInteractor& project_interactor = runtime.project_interactor();

    std::vector<std::string> post_process_scripts;
    for (const SelectionId project_id : project_ids) {
        const Project& project = project_interactor.project(project_id);
        for (const std::unique_ptr<ConfigContainer>& config_container : project.config_containers())
        {
            const ConfigPack config_pack = config_container->build_print_config();
            if (!std::holds_alternative<ConfigPackFDM>(config_pack)) {
                continue;
            }

            const ConstFindResult result =
                std::get<ConfigPackFDM>(config_pack).print.find("post_process");
            if (result.item == nullptr) {
                continue;
            }

            for (const std::string& script : result.item->get<std::vector<std::string>>()) {
                if (std::find(post_process_scripts.begin(), post_process_scripts.end(), script)
                    == post_process_scripts.end())
                {
                    post_process_scripts.push_back(script);
                }
            }
        }
    }

    if (post_process_scripts.empty()) {
        return false;
    }

    boost::nowide::cout << "\nA post-processing script has been detected in the config data:\n\n";
    for (const std::string& post_process_script : post_process_scripts) {
        boost::nowide::cout << "> " << post_process_script << "\n";
    }

    boost::nowide::cout << "\nContinue(Y/N) ? ";

    char user_answer = 0;
    boost::nowide::cin >> user_answer;

    return user_answer != 'Y' && user_answer != 'y';
}

} // namespace Slic3r::App::CLI
