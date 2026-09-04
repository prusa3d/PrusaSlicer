#include "Slic3r/App/AppServices.hpp"
#include "Slic3r/App/CLI/CLIRuntime.hpp"
#include "Slic3r/App/CLI/LoadPrintData.hpp"
#include "Slic3r/App/CLI/ProcessActions.hpp"
#include "Slic3r/App/CLI/ProcessTransform.hpp"
#include "Slic3r/App/IDialogManager.hpp"
#include "Slic3r/App/Init.hpp"
#include "Slic3r/Biz/FileLoadingLogic.hpp"
#include "Slic3r/Biz/IMessageDialogProvider.hpp"
#include "Slic3r/Domain/SelectionId.hpp"

#include <algorithm>

using Slic3r::Biz::ProjectInteractor;
using Slic3r::Biz::StepLoadDialogResult;
using Slic3r::Biz::Preset::PresetInteractor;
using Slic3r::Biz::Preset::PresetSelectionNames;
using Slic3r::Domain::ConfigPack;
using Slic3r::Domain::SelectionId;
using Slic3r::Domain::Preset::PresetKind;

namespace Slic3r::Biz::Preset {
struct PresetSelectionNames;
} // namespace Slic3r::Biz::Preset

namespace Slic3r::App::CLI {

// Dummy implementation to pass 3MF loading and other steps.
class CLIDummyDialogManager : public IDialogManager
{
public:
    void show_file_dialog(
        FileDialogType dialog_type,
        const std::string& dialog_title,
        const boost::filesystem::path& default_folder,
        const std::string& default_file_name,
        const std::string& wildcards,
        const FileCallback& callback
    ) override
    {}

    std::string show_input_dialog(
        const std::string& title,
        const std::string& text,
        const std::string& default_value
    ) override
    {
        return {};
    }

    void show_input_dialog_with_buttons(
        const std::string& title,
        const std::string& text,
        const std::string& default_value,
        const std::vector<ButtonWithCallback>& buttons
    ) override
    {}

    std::string show_combo_dialog(
        const std::string& title,
        const std::string& text,
        const std::vector<std::string>& values
    ) override
    {
        return {};
    }

    void show_webview_dialog(
        std::unique_ptr<Browser::AbstractBrowserLogic>&& logic,
        ProjectInteractor* project_interactor
    ) override
    {}

    void show_upload_webview_dialog(
        std::unique_ptr<Browser::AbstractUploadBrowserLogic>&& logic,
        ProjectInteractor* project_interactor,
        const UploadCallback& callback
    ) override
    {}

    void show_yesno_dialog(
        const std::string& title,
        const std::string& text,
        const YesNoCallback& callback
    ) override
    {}

    void show_rich_yesno_dialog(
        const std::string& title,
        const std::string& text,
        const std::string& check_text,
        const YesNoCallback& callback,
        const CheckBoxCheckedCallback& checked_callback
    ) override
    {}

    void show_yesnocancel_dialog(
        const std::string& title,
        const std::string& text,
        const Button& yes,
        const Button& no,
        const Button& cancel
    ) override
    {}

    void show_diff_dialog(
        const PresetInteractor& preset_interactor,
        std::optional<PresetKind> kind
    ) override
    {}

    std::string show_ramming_dialog(const std::string& ramming_parameters) override
    {
        PANIC("Ramming dialog not implemented for CLI");
    }

    void open_in_browser(const std::string& link, int flag) override
    {
        PANIC("Open browser not implemented for CLI");
    }

    void show_info_dialog(
        const std::string& text,
        const std::string& title,
        bool is_marked = false
    ) override
    {
        SPDLOG_INFO("{}: {}", title, text);
    }

    void show_warning_dialog(const std::string& text, const std::string& title) override
    {
        SPDLOG_WARN("{}: {}", title, text);
    }

    void show_error_dialog(const std::string& text, const std::string& title) override
    {
        SPDLOG_ERROR("{}: {}", title, text);
    }

    PresetsSwitchStates show_unsaved_changes_dialog(
        const std::string& dialog_name,
        const ConfigPack& config_original,
        const ConfigPack& config_selected,
        ConfigPack* config_new_selected,
        const PresetSelectionNames& preset_names,
        const PresetSelectionNames& preset_names_new,
        const PresetInteractor& preset_interactor,
        bool new_printer_has_multiple_extruders
    ) override
    {
        return {};
    }

    std::optional<StepLoadDialogResult> show_load_step_dialog(
        const std::string& filename,
        double linear_precision,
        double angle_precision,
        bool multiple
    ) override
    {
        return std::nullopt;
    }

    std::string show_save_dialog(
        PresetKind kind,
        const std::string& original_name,
        const PresetInteractor& preset_interactor
    ) override
    {
        return "";
    }

    NamesPerKindMap show_save_print_tool_dialog(
        const NamesPerKindMap& original_names_per_kind,
        const PresetInteractor& preset_interactor
    ) override
    {
        return {};
    }
};

int run(InitParams& init_params)
{
    // Even CLI needs to initialize DialogManager to pass 3MF loading and other steps.
    AppServices& app_services = AppServices::instance();
    app_services.set_dialog_manager(std::make_unique<CLIDummyDialogManager>());

    CLIRuntime runtime{init_params};

    if (process_profiles_sharing(runtime, init_params)) {
        return EXIT_SUCCESS;
    }
    if (process_plugin_subcommand(runtime, init_params)) {
        return EXIT_SUCCESS;
    }

    // A loaded 3MF brings its own placement, so skip arrange.
    if (!init_params.transform.dont_arrange.value_or(false)
        && std::ranges::any_of(
            init_params.input.input_files,
            [](const std::string& input_file)
            { return Biz::FileLoadingLogic::is_project_file(input_file); }
        ))
    {
        init_params.transform.dont_arrange = true;
    }

    std::vector<SelectionId> project_ids;
    if (!load_print_data(runtime, project_ids, init_params)) {
        return EXIT_FAILURE;
    }

    if (is_needed_post_processing(runtime, project_ids)) {
        return EXIT_SUCCESS;
    }

    if (!process_transform(runtime, init_params, project_ids)) {
        return EXIT_FAILURE;
    }

    if (!process_actions(runtime, init_params, project_ids)) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
} // namespace Slic3r::App::CLI
