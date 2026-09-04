#include "Slic3r/App/CLI/ProcessActions.hpp"

#include "Slic3r/App/CLI/CLIRuntime.hpp"
#include "Slic3r/App/CLI/CLIUtils.hpp"
#include "Slic3r/App/CLI/ProcessTransform.hpp"
#include "Slic3r/App/CLI/ProfilesSharingUtils.hpp"
#include "Slic3r/App/ConfigModelDump.hpp"
#include "Slic3r/App/Init.hpp"
#include "Slic3r/App/PresetUpdater/PresetUpdaterCLI.hpp"
#include "Slic3r/App/Lua/PluginCliOps.hpp"
#include "Slic3r/Biz/Algorithms/Model.hpp"
#include "Slic3r/Biz/Config/ConfigSerialize.hpp"
#include "Slic3r/Biz/Config/SelectedPresetJson.hpp"
#include "Slic3r/Biz/Format/3mf.hpp"
#include "Slic3r/Biz/Format/OBJ.hpp"
#include "Slic3r/Biz/Format/ProjectFileConstants.hpp"
#include "Slic3r/Biz/Format/STL.hpp"
#include "Slic3r/Biz/Platform/JobManager/JobManager.hpp"
#include "Slic3r/Biz/Preset/IO/BundleLoader.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/ResultExport/ExportNameParser.hpp"
#include "Slic3r/Biz/Slicing/SlicingInteractor.hpp"
#include "Slic3r/Biz/Utils/CopyFile.hpp"
#include "Slic3r/Biz/PresetUpdater/IPresetUpdaterResultListener.hpp"
#include "Slic3r/Directories.hpp"
#include "Slic3r/Domain/BedInstance.hpp"
#include "Slic3r/Domain/FullConfigFDM.hpp"
#include "Slic3r/Domain/FullConfigSLA.hpp"
#include "Slic3r/Domain/Preset/Bundle.hpp"
#include "Slic3r/Domain/Project.hpp"
#include "Slic3r/Domain/Workbench.hpp"
#include "Slic3r/Log.hpp"
#include "Slic3r/Semver.hpp"
#include "Slic3r/Version.hpp"

#include <cstring>
#include <iostream>
#include <algorithm>
#include <chrono>
#include <set>
#include <sstream>
#include <string>
#include <future>
#include <atomic>

#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <boost/nowide/filesystem.hpp>
#include <boost/nowide/fstream.hpp>
#include <boost/nowide/iostream.hpp>

#include <nlohmann/json.hpp>

namespace fs = boost::filesystem;

using namespace Slic3r;
using namespace Slic3r::Biz;

using Slic3r::Biz::Format::ProjectFileConstants::CONFIGURATION;
using Slic3r::Biz::Format::ProjectFileConstants::PRESET_METADATA;
using Slic3r::Biz::Platform::PlatformServices;
using Slic3r::Biz::Platform::JobManager::IJobManagerStatusChangedListener;
using Slic3r::Biz::Preset::IO::BundlePaths;
using Slic3r::Biz::Slicing::SlicingInteractor;
using Slic3r::Domain::BedInstance;
using Slic3r::Domain::ConfigContainer;
using Slic3r::Domain::ConfigPack;
using Slic3r::Domain::ConfigPackFDM;
using Slic3r::Domain::ConfigPackSLA;
using Slic3r::Domain::Model;
using Slic3r::Domain::PrinterTechnology;
using Slic3r::Domain::Project;
using Slic3r::Domain::SelectionId;
using Slic3r::Domain::SlicingId;
using Slic3r::Domain::Preset::Bundle;

namespace Slic3r::App::CLI {

namespace {

std::string format_slicing_errors(const std::vector<Slicing::Error>& slicing_errors)
{
    std::ostringstream error_stream;
    for (const Slicing::Error& slicing_error : slicing_errors) {
        error_stream << slicing_error << "\n";
    }

    if (slicing_errors.empty()) {
        error_stream << "Slicing failed.";
    }

    return error_stream.str();
}

struct PresetUpdaterApprovalListener : public Biz::PresetUpdater::IPresetUpdaterResultListener
{
    std::promise<bool> promise_approved;
    std::atomic<bool> resolved{false};

    void on_preset_updater_error(
        Biz::PresetUpdater::JobId,
        const std::string& body,
        Biz::PresetUpdater::PresetUpdaterReason
    ) override
    {
        if (!resolved.exchange(true)) {
            promise_approved.set_value(false);
        }
    }

    void on_preset_updater_forced_reconfigurations_list(
        Biz::PresetUpdater::JobId,
        const Biz::PresetUpdater::PresetUpdaterReconfigurationList& reconfigurations,
        const std::vector<Biz::PresetUpdater::PresetUpdaterWarning>& warnings) override
    {
        if (!resolved.exchange(true)) {
            promise_approved.set_value(reconfigurations.empty());
        }
    }

    void on_preset_updater_reconfigurations_list(
        Biz::PresetUpdater::JobId,
        const Biz::PresetUpdater::PresetUpdaterReconfigurationList&,
        const std::vector<Biz::PresetUpdater::PresetUpdaterWarning>&,
        Biz::PresetUpdater::VerboseStyle) override
    {
        // Empty on purpose. Only forced reconfigurations matters.
    }

    void on_preset_updater_status(
        Biz::PresetUpdater::JobId, const std::string&, int, unsigned, Biz::PresetUpdater::VerboseStyle) override
    {
        // Ignored on purpose. Only forced reconfigurations matters.
    }

    void on_preset_updater_job_finished(
        Biz::PresetUpdater::JobId, Biz::PresetUpdater::JobState state) override
    {
        if (state == Biz::PresetUpdater::JobState::Succeeded) {
            return;
        }
        if (!resolved.exchange(true)) {
            promise_approved.set_value(false);
        }
    }
};

class CLIThumbnailImageGenerator : public Slicing::IThumbnailImageGenerator
{
public:
    CLIThumbnailImageGenerator() = default;
    explicit CLIThumbnailImageGenerator(const std::vector<std::string>& input_files)
    {
        if (input_files.size() == 1 && boost::iends_with(input_files[0], ".3mf")) {
            m_filename = input_files[0];
        }
    }

    std::future<Slicing::ThumbnailImageResults> enqueue_thumbnail_requests(
        const Slicing::ThumbnailImageRequests& requests
    ) override
    {
        std::promise<Slicing::ThumbnailImageResults> promise;
        std::future<Slicing::ThumbnailImageResults> result{promise.get_future()};
        if (m_filename.empty()) {
            promise.set_value(Slicing::ThumbnailImageResults{});
            return result;
        }

        std::vector<Domain::Size> sizes;
        for (const auto& request : requests) {
            for (const auto& size : request.params.sizes) {
                sizes.emplace_back(size);
            }
        }

        std::vector<Domain::Image> source_images = get_thumbnail_images_from_3mf(m_filename, sizes);

        if (source_images.empty() || source_images.size() != sizes.size()
         || std::any_of(source_images.begin(), source_images.end(),
             [](const Domain::Image& img) { return img.width() == 0 || img.height() == 0; })
            ) {
            promise.set_value(Slicing::ThumbnailImageResults{});
            return result;
        }

        Slicing::ThumbnailImageResults results;
        size_t j=0;
        for (const auto& request : requests) {
            Slicing::ThumbnailImageResult thumbnail_result;
            thumbnail_result.type = request.type;
            thumbnail_result.project_id = request.params.project_id;
            thumbnail_result.bed_instance_id = request.params.bed_instance_id;

            for (size_t i = 0; i < request.params.sizes.size(); ++i) {
                thumbnail_result.images.push_back(source_images[j++]);
                ASSERT(thumbnail_result.images.back().width() == request.params.sizes[i].width);
                ASSERT(thumbnail_result.images.back().height() == request.params.sizes[i].height);
            }
            results.push_back(std::move(thumbnail_result));
        }
        ASSERT(j == source_images.size());
        promise.set_value(std::move(results));
        return result;
    }

    void handle_enqueued_requests() override {}
private:
    std::string m_filename;
};

} // namespace

static std::string output_filename_and_path(
    const Project& project,
    const ConfigContainer& config_container,
    const std::optional<std::string>& output_path
)
{
    const boost::filesystem::path model_proposed_filename_and_path{
        Algorithms::Model::propose_export_file_name_and_path(project.model())
    };

    const ConfigPack config_pack{config_container.build_print_config()};
    const std::string extension = [&config_pack]() -> std::string
    {
        if (std::holds_alternative<ConfigPackSLA>(config_pack)) {
            return ".sl1";
        } else {
            const ConfigPackFDM& config_pack_fdm = std::get<ConfigPackFDM>(config_pack);
            if (config_pack_fdm.printer.items.opt("binary_gcode").get<bool>()) {
                return ".bgcode";
            } else {
                return ".gcode";
            }
        }
    }();

    const boost::filesystem::path output_dir =
        [&output_path, &model_proposed_filename_and_path]() -> boost::filesystem::path
    {
        if (output_path.has_value()) {
            const boost::filesystem::path path(output_path.value());
            if (boost::filesystem::is_directory(path)) {
                return path;
            } else if (!path.parent_path().empty()) {
                return path.parent_path();
            }
        }

        if (!model_proposed_filename_and_path.parent_path().empty()) {
            return model_proposed_filename_and_path.parent_path();
        }

        return fs::current_path();
    }();

    const std::string filename =
        [&project, &output_path, &model_proposed_filename_and_path]() -> std::string
    {
        if (output_path.has_value()) {
            const boost::filesystem::path path(output_path.value());
            if (!boost::filesystem::is_directory(path) && !path.filename().empty()) {
                return path.filename().string();
            }
        }

        if (!project.file_name().empty()) {
            return project.file_name();
        }

        return model_proposed_filename_and_path.filename().string();
    }();

    return (output_dir / filename).replace_extension(extension).string();
}

/**
 * @brief Slices the selected bed of the selected project and exports the result.
 */
static std::optional<std::string>
slice_and_export_selected_bed(CLIRuntime& runtime, const std::optional<std::string>& output_path)
{
    ProjectInteractor& project_interactor = runtime.project_interactor();
    SlicingInteractor& slicing_interactor = project_interactor.slicing_interactor();
    StatusCache& status_cache             = project_interactor.status_cache();

    const SlicingId slicing_id = project_interactor.selected_bed_slicing_id();
    std::string dest_path      = output_filename_and_path(
        project_interactor.selected_project(),
        project_interactor.selected_config_container(),
        output_path
    );

    const BedInstance* selected_bed_instance =
        project_interactor.selected_project().find_bed_instance_by_id(slicing_id.bed_instance_id);
    if (selected_bed_instance == nullptr) {
        return "No bed is selected for " + dest_path + ".";
    }

    Project& project                        = project_interactor.selected_project();
    const ConfigContainer& config_container = project_interactor.selected_config_container();
    slicing_interactor.update_process(
        project.model(),
        project.metadata(),
        config_container.selected_preset().metadata(),
        config_container.build_print_config(),
        *selected_bed_instance
    );

    const std::function<bool()> terminal_status_predicate = [&status_cache, slicing_id]()
    {
        const std::optional<Slicing::Status> current_status = status_cache.get_status(slicing_id);
        return current_status.has_value()
            && (current_status->code == Slicing::StatusCode::Empty
                || current_status->code == Slicing::StatusCode::Removed
                || current_status->code == Slicing::StatusCode::Finished
                || current_status->code == Slicing::StatusCode::InvalidData);
    };

    slicing_interactor.slice_bed(slicing_id);
    runtime.wait_until(terminal_status_predicate);

    const Slicing::Status current_status = status_cache.get_status(slicing_id).value();
    if (current_status.code == Slicing::StatusCode::Empty) {
        return "All objects are outside of the print volume.";
    } else if (current_status.code == Slicing::StatusCode::Removed) {
        return "The slicing input for " + dest_path + " was removed.";
    } else if (current_status.code == Slicing::StatusCode::InvalidData) {
        return format_slicing_errors(current_status.errors);
    } else {
        ASSERT(current_status.code == Slicing::StatusCode::Finished);
    }

    PlatformServices& platform_services = PlatformServices::instance();
    ExportFinishedJobManagerStatusListener export_finished_listener;
    platform_services.job_manager().add_listener<IJobManagerStatusChangedListener>(
        &export_finished_listener
    );

    ExportNameParser::ExportNameData name_data;
    try {
        name_data = ExportNameParser::parse_export_name(project_interactor);
    } catch (const Slic3r::PlaceholderParserError& parser_error) {
        boost::nowide::cerr
            << "Failed to parse output filename: "
            << parser_error.what()
            << std::endl;
    }

    boost::filesystem::path export_path;
    if (name_data.filename.empty()) {
        export_path = boost::filesystem::path(dest_path);
    } else {
        export_path = boost::filesystem::path(dest_path).parent_path() / name_data.filename;
        // Store back to dest_path, it is used later.
        dest_path = export_path.string();
    }

    project_interactor.do_result_export(slicing_id, export_path);

    runtime.wait_until([&export_finished_listener]()
                       { return export_finished_listener.export_finished; });
    platform_services.job_manager().remove_listener<IJobManagerStatusChangedListener>(
        &export_finished_listener
    );

    if (export_finished_listener.export_error.has_value()) {
        return export_finished_listener.export_error;
    }

    const std::string dest_path_final = [&output_path, &dest_path]() -> std::string
    {
        if (output_path.has_value()) {
            const boost::filesystem::path path(output_path.value());
            if (!boost::filesystem::is_directory(path)) {
                return path.string();
            }
        }

        return dest_path;
    }();

    if (dest_path != dest_path_final) {
        if (Utils::rename_file(dest_path, dest_path_final)) {
            return "Renaming file " + dest_path + " to " + dest_path_final + " failed";
        }

        boost::nowide::cout << "Slicing result exported to " << dest_path_final << std::endl;
    } else {
        boost::nowide::cout << "Slicing result exported to " << dest_path << std::endl;
    }

    return std::nullopt;
}

static bool has_profile_sharing_action(const InitParams& init_params)
{
    return init_params.action.query_printer_models
        || init_params.action.query_print_tool_filament_profiles;
}

bool has_full_config_from_profiles(const InitParams& init_params)
{
    const InputParams& input = init_params.input;

    return !has_profile_sharing_action(init_params)
        && ((input.print_profile_preset.has_value() && !input.print_profile_preset->empty())
            || !input.material_profile_presets.empty()
            || !input.tool_profile_presets.empty()
            || (input.printer_profile_preset.has_value()
                && !input.printer_profile_preset->empty()));
}

bool process_profiles_sharing(CLIRuntime& runtime, const InitParams& init_params)
{
    if (!has_profile_sharing_action(init_params)) {
        return false;
    }

    const Bundle& preset_bundle = runtime.project_interactor().workbench().preset_bundle();

    std::string ret;
    if (init_params.action.query_printer_models) {
        ret = get_json_printer_models(preset_bundle);
    } else if (init_params.action.query_print_tool_filament_profiles) {
        if (init_params.input.printer_profile_preset.has_value()) {
            const std::string& printer_profile = init_params.input.printer_profile_preset.value();
            ret = get_json_print_tool_filament_profiles(preset_bundle, printer_profile);
            if (ret.empty()) {
                boost::nowide::cerr
                    << "query-print-tool-filament-profiles error: Printer profile '"
                    << printer_profile
                    << "' wasn't found among installed printers."
                    << std::endl
                    << "Or the request can be wrong."
                    << std::endl;
                return true;
            }
        } else {
            boost::nowide::cerr
                << "query-print-tool-filament-profiles error: This action requires set 'printer-profile' option"
                << std::endl;
            return true;
        }
    }

    if (ret.empty()) {
        boost::nowide::cerr << "Wrong request" << std::endl;
        return true;
    }

    // use --output when available
    if (init_params.misc.output.has_value()) {
        const std::string& cmdline_param = init_params.misc.output.value();
        // if we were supplied a directory, use it and append our automatically generated filename
        boost::filesystem::path cmdline_path(cmdline_param);
        boost::filesystem::path proposed_path =
            boost::filesystem::path(resources_dir()) / "out.json";
        if (boost::filesystem::is_directory(cmdline_path)) {
            proposed_path = (cmdline_path / proposed_path.filename());
        } else if (cmdline_path.extension().empty()) {
            proposed_path = cmdline_path.replace_extension("json");
        } else {
            proposed_path = cmdline_path;
        }

        const std::string file = proposed_path.string();
        boost::nowide::ofstream c;
        c.open(file, std::ios::out | std::ios::trunc);
        c << ret << std::endl;
        c.close();

        boost::nowide::cout << "Output for your request is written into " << file << std::endl;
    } else {
        boost::nowide::cout << ret;
    }

    return true;
}

bool process_plugin_subcommand(CLIRuntime& runtime, InitParams& init_params)
{
    if (auto* action = std::get_if<PluginInitActionParams>(&init_params.action.subcommand_action)) {
        Lua::plugin_init(*action);
        return true;
    }
    if (auto* action = std::get_if<PluginKeygenActionParams>(&init_params.action.subcommand_action)) {
        Lua::plugin_keygen(*action);
        return true;
    }
    if (auto* action = std::get_if<PluginSignActionParams>(&init_params.action.subcommand_action)) {
        Lua::plugin_sign(*action);
        return true;
    }

    return false;
}

namespace IO {
enum ExportFormat : int
{
    OBJ,
    STL,
    // SVG,
    TMF,
    Gcode
};
} // namespace IO

static std::string
output_filepath(const Project& project, IO::ExportFormat format, const std::string& cmdline_param)
{
    std::string ext;
    switch (format) {
    case IO::OBJ:
        ext = ".obj";
        break;
    case IO::STL:
        ext = ".stl";
        break;
    case IO::TMF:
        ext = ".3mf";
        break;
    default:
        assert(false);
        break;
    };

    boost::filesystem::path proposed_path = boost::filesystem::path(
        Algorithms::Model::propose_export_file_name_and_path(project.model(), ext)
    );

    if (!project.file_name().empty() && !proposed_path.parent_path().empty()) {
        proposed_path = proposed_path.parent_path() / (project.file_name() + ext);
    }

    // use --output when available
    if (!cmdline_param.empty()) {
        // if we were supplied a directory, use it and append our automatically generated filename
        boost::filesystem::path cmdline_path(cmdline_param);
        if (boost::filesystem::is_directory(cmdline_path)) {
            proposed_path = cmdline_path / proposed_path.filename();
        } else {
            proposed_path = cmdline_path;
        }
    }

    return proposed_path.string();
}

static bool export_projects(
    CLIRuntime& runtime,
    const std::vector<SelectionId>& project_ids,
    IO::ExportFormat format,
    const std::string& cmdline_param
)
{
    ProjectInteractor& project_interactor = runtime.project_interactor();

    for (const SelectionId project_id : project_ids) {
        Project& project       = project_interactor.project(project_id);
        const std::string path = output_filepath(project, format, cmdline_param);
        bool success           = false;
        switch (format) {
        case IO::OBJ: {
            success = store_obj(path.c_str(), &project.model());
            break;
        }
        case IO::STL: {
            success = store_stl(path, Algorithms::Model::flatten_to_mesh(project.model()), true);
            break;
        }
        case IO::TMF: {
            try {
                project_interactor.select_project(project_id);
                project_interactor.save_selected_project(
                    boost::filesystem::path{path},
                    Store3mfParam{.fullpath_sources = false}
                );

                success = true;
            } catch (boost::filesystem::filesystem_error&) {
                success = false;
            }

            break;
        }
        default: {
            assert(false);
            break;
        }
        }

        if (success) {
            boost::nowide::cout << "File exported to " << path << std::endl;
        } else {
            boost::nowide::cerr << "File export to " << path << " failed" << std::endl;
            return false;
        }
    }

    return true;
}

/**
 * @brief Prints the model statistics (--info) of every project.
 */
static bool perform_model_info(CLIRuntime& runtime, const std::vector<SelectionId>& project_ids)
{
    if (project_ids.empty()) {
        boost::nowide::cerr << "Cannot show info for empty projects." << std::endl;
        return false;
    }

    ProjectInteractor& project_interactor = runtime.project_interactor();
    for (const SelectionId project_id : project_ids) {
        Algorithms::Model::print_info(project_interactor.project(project_id).model());
    }

    return true;
}

/**
 * @brief Dumps the configuration model (--dump-json-model) into a JSON file.
 */
static void perform_config_model_dump(const MiscParams& misc)
{
    dump_config_model(misc.config_model_json_file.value_or("config-model.json"));
}

/**
 * @brief Generates the binary preset bundle cache (--generate-preset-cache) in
 * data_dir()/cache/bundle_cache.
 */
static void perform_preset_cache_generation(CLIRuntime& runtime)
{
    try {
        // Strip build metadata from Slic3r::VERSION so that build server builds produce a cache
        // compatible with local debug builds.
        Semver semver(Slic3r::VERSION);
        semver.set_metadata(nullptr);

        const std::string cache_file =
            fs::path(fs::path(Slic3r::data_dir()) / "cache" / "bundle_cache").string();
        Preset::IO::serialize_bundle(
            cache_file,
            runtime.project_interactor().workbench().preset_bundle(),
            BundlePaths::make_standard_runtime(),
            semver.to_string()
        );

        boost::nowide::cout << "Preset cache generated: " << cache_file << std::endl;
    } catch (const std::exception& ex) {
        boost::nowide::cerr << "Failed to generate preset cache: " << ex.what() << std::endl;
    }
}

/**
 * @brief Saves the selected configuration (--save) as a JSON with the preset metadata
 * and the configuration boxes (the same format as the project JSON stored in 3MF).
 */
static bool perform_configuration_save(CLIRuntime& runtime, const MiscParams& misc)
{
    const std::string config_save_path =
        misc.output.has_value() ? misc.output.value() : "config.json";

    const ConfigContainer& config_container =
        runtime.project_interactor().selected_config_container();

    nlohmann::ordered_json config_json;
    config_json[PRESET_METADATA] =
        nlohmann::ordered_json(config_container.selected_preset().metadata());

    const ConfigPack config_pack{config_container.build_print_config()};
    if (std::holds_alternative<ConfigPackFDM>(config_pack)) {
        config_json[CONFIGURATION] =
            nlohmann::ordered_json(Domain::as_boxes(std::get<ConfigPackFDM>(config_pack)));
    } else if (std::holds_alternative<ConfigPackSLA>(config_pack)) {
        config_json[CONFIGURATION] =
            nlohmann::ordered_json(Domain::as_boxes(std::get<ConfigPackSLA>(config_pack)));
    } else {
        PANIC("Unexpected config type!");
    }

    boost::nowide::ofstream config_file;
    config_file.open(config_save_path, std::ios::out | std::ios::trunc);
    if (config_file.is_open()) {
        config_file << config_json.dump() << std::endl;
        config_file.close();
    } else {
        boost::nowide::cerr
            << "Cannot open file "
            << config_save_path
            << " for writing"
            << std::endl;
        return false;
    }

    return true;
}

/**
 * @brief Exports the model of every project (--export-stl/--export-obj/--export-3mf).
 */
static bool perform_model_exports(
    CLIRuntime& runtime,
    const InitParams& init_params,
    const std::vector<SelectionId>& project_ids
)
{
    if (project_ids.empty()) {
        boost::nowide::cerr << "Cannot export empty projects." << std::endl;
        return false;
    }

    const ActionParams& action = init_params.action;
    const std::string output =
        init_params.misc.output.has_value() ? init_params.misc.output.value() : "";

    if (action.export_stl) {
        if (!export_projects(runtime, project_ids, IO::STL, output)) {
            return false;
        }
    }

    if (action.export_obj) {
        if (!export_projects(runtime, project_ids, IO::OBJ, output)) {
            return false;
        }
    }

    if (action.export_3mf) {
        if (!export_projects(runtime, project_ids, IO::TMF, output)) {
            return false;
        }
    }

    return true;
}

/**
 * @brief Slices every project and exports the result (--slice/--export-gcode/
 * --export-sla).
 */
static bool perform_slicing_exports(
    CLIRuntime& runtime,
    const InitParams& init_params,
    const std::vector<SelectionId>& project_ids
)
{
    const ActionParams& action            = init_params.action;
    const TransformParams& transform      = init_params.transform;
    ProjectInteractor& project_interactor = runtime.project_interactor();

    for (const SelectionId project_id : project_ids) {
        project_interactor.select_project(project_id);

        const PrinterTechnology printer_technology =
            project_interactor.selected_config_container().selected_preset().technology();
        if (action.export_gcode && printer_technology == PrinterTechnology::SLA) {
            boost::nowide::cerr
                << "Error: Cannot export G-code for an FFF configuration."
                << std::endl;
            return false;
        } else if (action.export_sla && printer_technology == PrinterTechnology::FFF) {
            boost::nowide::cerr
                << "error: Cannot export SLA slices for a SLA configuration."
                << std::endl;
            return false;
        }

        // If all objects have defined instances, their relative positions will be
        // honored when printing (they will be only centered, unless --dont-arrange
        // is supplied).
        if (!transform.dont_arrange.has_value() || !transform.dont_arrange.value()) {
            arrange_and_wait(runtime, project_id);
            if (transform.center.has_value()) {
                center_selected_project_around_point(runtime, transform.center.value());
            }
        }

        const std::optional<std::string> slicing_error =
            slice_and_export_selected_bed(runtime, init_params.misc.output);
        if (slicing_error.has_value()) {
            boost::nowide::cerr << slicing_error.value() << std::endl;
            return false;
        }
    }

    return true;
}

/**
 * @brief Runs the preset updater actions (--preset-updater-*).
 */
static void perform_preset_updater_actions(
    CLIRuntime& runtime,
    const ActionParams& action,
    const MiscParams& misc
)
{
    if (!misc.loglevel) {
        Slic3r::set_log_level(0);
    }

    const std::string additional_data =
        misc.output.has_value() ? misc.output.value() : std::string();

    // Every method of PresetUpdaterInteractor works only on data in the filesystem.
    PresetUpdaterCLI preset_updater_cli(runtime.project_interactor().preset_updater_interactor());
    preset_updater_cli.start(action, additional_data);
    runtime.wait_until([&preset_updater_cli]() { return preset_updater_cli.has_result(); });
}

bool process_actions(
    CLIRuntime& runtime,
    const InitParams& init_params,
    const std::vector<SelectionId>& project_ids
)
{
    if (!init_params.action.has_any_action()) {
        return true;
    }

    const ActionParams& action = init_params.action;
    const MiscParams& misc     = init_params.misc;

    if (action.model_info) {
        if (!perform_model_info(runtime, project_ids)) {
            return true;
        }
    }

    if (action.dump_json_model) {
        perform_config_model_dump(misc);
    }

    if (action.generate_preset_cache) {
        perform_preset_cache_generation(runtime);
    }

    if (action.configuration_save) {
        if (!perform_configuration_save(runtime, misc)) {
            return false;
        }
    }

    if (action.export_stl || action.export_obj || action.export_3mf) {
        if (!perform_model_exports(runtime, init_params, project_ids)) {
            return true;
        }
    }

    if (action.slice || action.export_gcode || action.export_sla) {
        Biz::ProjectInteractor& project_interactor = runtime.project_interactor();
        Biz::Platform::IMainThreadDispatcher& dispatcher = runtime.dispatcher();

        PresetUpdaterApprovalListener approval_listener;
        project_interactor.preset_updater_interactor().add_listener<Biz::PresetUpdater::IPresetUpdaterResultListener>(&approval_listener);

        struct ScopedListenerGuard {
            Biz::PresetUpdater::PresetUpdaterInteractor& interactor;
            PresetUpdaterApprovalListener& listener;
            ~ScopedListenerGuard() {
                interactor.remove_listener<Biz::PresetUpdater::IPresetUpdaterResultListener>(&listener);
            }
        } listener_guard{project_interactor.preset_updater_interactor(), approval_listener};

        project_interactor.preset_updater_interactor().check_forced_reconfigurations();

        std::future<bool> future_approval = approval_listener.promise_approved.get_future();
        bool is_approved = false;

        try {
            const auto timeout_limit = std::chrono::steady_clock::now() + std::chrono::seconds(30);
            bool timed_out = false;

            while (future_approval.wait_for(std::chrono::milliseconds(100)) != std::future_status::ready) {
                dispatcher.dispatch_enqueued();

                if (std::chrono::steady_clock::now() > timeout_limit) {
                    SPDLOG_ERROR("Timeout waiting for preset updater approval. Aborting loop.");
                    timed_out = true;
                    break;
                }
            }

            if (!timed_out) {
                is_approved = future_approval.get();
            }
        } catch (const std::future_error& e) {
            SPDLOG_ERROR("Future error during preset updater check (promise likely abandoned): {}", e.what());
        }

        if (!is_approved) {
            boost::nowide::cerr << "Execution blocked: Pending forced preset reconfigurations or updater error."
                << std::endl
                << "Run --preset-update-list to list these reconfigurations or --preset-update to install."
                << std::endl;
            return false;
        }

        if (!perform_slicing_exports(runtime, init_params, project_ids)) {
            return true;
        }
    }

    if (action.has_preset_updater_action()) {
        perform_preset_updater_actions(runtime, action, misc);
        return true;
    }

    return true;
}
} // namespace Slic3r::App::CLI
