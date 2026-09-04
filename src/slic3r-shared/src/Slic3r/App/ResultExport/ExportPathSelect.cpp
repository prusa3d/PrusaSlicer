#include "Slic3r/App/ResultExport/ExportPathSelect.hpp"
#include "Slic3r/App/AppServices.hpp"
#include "Slic3r/App/IDialogManager.hpp"
#include "Slic3r/App/Wildcards.hpp"
#include "Slic3r/App/PopNotification/PopNotificationCenter.hpp"
#include "Slic3r/App/AppConfig.hpp"

#include "Slic3r/Biz/ProjectInteractor.hpp"
#include <Slic3r/Biz/Platform/PlatformServices.hpp>
#include "Slic3r/Biz/I18N/I18N.hpp"

#include "boost/filesystem/path.hpp"

using Slic3r::Biz::ExportNameParser::Technology;
using Slic3r::Biz::ExportNameParser::ExportNameData;

namespace Slic3r::App::ExportPathSelect {

namespace {

// Shortens placeholder parser error in case there is an arrow pointing to some variable.
// This is done because lines are very short in notification and it breaks the pretty error outcome.
// example:
// Parsing error at line 1: Not a variable name
// {input_filename_base}_{layer_height}mm_{printing_filament_types}_{printer_model}_{print_timeabcde}.gcode
//                                                                                   ^
// Parsing error at line 1: Not a variable name
// {print_timeabcde}

std::string shorten_error(const std::string& errorMsg) {
    std::istringstream stream(errorMsg);
    std::string header, content, pointer;
    
    // Parse the three lines
    if (!std::getline(stream, header) || 
        !std::getline(stream, content) || 
        !std::getline(stream, pointer)) {
        return errorMsg; 
    }

    size_t arrowPos = pointer.find('^');
    if (arrowPos == std::string::npos) return errorMsg;

    arrowPos = std::min(arrowPos, content.length() - 1);
    size_t start = content.rfind('{', arrowPos);    
    size_t end = (start != std::string::npos) ? content.find('}', start) : std::string::npos;
    if (start != std::string::npos && end != std::string::npos) {
        // Return Header + Newline + Variable token only
        return header + "\n" + content.substr(start, end - start + 1);
    }
    return errorMsg;
}

std::string post_upload_action_label(Biz::PrintHost::PrintHostAfterUploadAction action)
{
    using Biz::PrintHost::PrintHostAfterUploadAction;
    switch (action) {
    case PrintHostAfterUploadAction::StartPrint:
        // TRN Button on the print host upload dialog.
        return Biz::_u8L("Upload and Print");
    case PrintHostAfterUploadAction::StartSimulation:
        // TRN Button on the print host upload dialog.
        return Biz::_u8L("Upload and Simulate");
    case PrintHostAfterUploadAction::QueuePrint:
        // TRN Button on the print host upload dialog.
        return Biz::_u8L("Upload to Queue");
    case PrintHostAfterUploadAction::None:
        break;
    }
    ASSERT(false, "No button label for PrintHostAfterUploadAction::None");
    return {};
}

std::string gen_wildcards(const std::string& extension, Technology tech, bool bgcode_allowed)
{
    if (tech == Technology::Fdm) {
        // If bgcode not allowed in printer setting, show just gcode
        if (!bgcode_allowed) {
             return Wildcards::generate_wildcards(Wildcards::TypeFlag::GCode, Wildcards::TypeFlag::GCode);
        }
        // If bgcode is written in output options, use it as default selected wildcard
        if (extension == ".bgcode" || extension == ".BGCODE") {
            return Wildcards::generate_wildcards(Wildcards::TypeFlag::BinaryGCode | Wildcards::TypeFlag::GCode, Wildcards::TypeFlag::BinaryGCode);
        }
        return Wildcards::generate_wildcards(Wildcards::TypeFlag::GCode | Wildcards::TypeFlag::BinaryGCode, Wildcards::TypeFlag::GCode);
    } else if (tech == Technology::Sla) {
        if (extension == ".sl1" || extension == ".SL1") {
            return Wildcards::generate_wildcards(Wildcards::TypeFlag::Sl1);  
        } else if (extension == ".sl1s" || extension == ".SL1S") {
            return Wildcards::generate_wildcards(Wildcards::TypeFlag::Sl1S);  
        }
        return Wildcards::generate_wildcards(Wildcards::TypeFlag::Sl1 | Wildcards::TypeFlag::Sl1S);
    } else {
        ASSERT(false);
    }
    return {};
}

}

ExportNameData get_export_name_data(const Biz::ProjectInteractor& project_interactor)
{
    ExportNameData name_data;
    try {
        name_data = Biz::ExportNameParser::parse_export_name(project_interactor);
    } catch (const Slic3r::PlaceholderParserError& e) {
        SPDLOG_ERROR("Failed to parse output filename: {}", e.what());
        AppServices::instance().pop_notification_center().upsert_notification(
            {PopNotification::PopNotificationType::Custom,
             PopNotification::PopNotificationLevel::Error,
             0s,
             PopNotification::PopNotificationLayoutHeaderText(
                 "Failed to parse output filename",
                 shorten_error(e.what())
             ),
             {},
             project_interactor.selected_project_id()},
            [](const PopNotification::PopNotificationPayload&,
               const PopNotification::PopNotificationPayload&) { return false; }
        );
        // Retrieves some filename since parsing failed.
        name_data = Biz::ExportNameParser::error_state_export_name(project_interactor);
    }

    std::string ext = project_interactor.output_extension(
        project_interactor.selected_project_id(),
        AppServices::instance().app_config().get<std::string>("last_used_extension")
    );
    
    std::string last_used_ext_lower = ext;
    std::transform(last_used_ext_lower.begin(), last_used_ext_lower.end(), last_used_ext_lower.begin(), [](unsigned char c){ return std::tolower(c); });

    auto apply_extension = [&](const std::string& new_ext) {
        name_data.preferred_extension = new_ext;
        name_data.filename = boost::filesystem::path(name_data.filename).replace_extension(new_ext).string();
    };

    if (name_data.technology == Technology::Fdm) {
        const auto* item = project_interactor.preset_interactor().selected_printer_preset().printer.config_box().find("binary_gcode").item;
        bool bgcode_allowed = item ? item->get<bool>() : true;

        if (last_used_ext_lower.empty() && bgcode_allowed) {
            // We set extenseion here to bgcode because some systems (Linux) are using wildcards just to filter existing files.
            // While we would rather have output format set to our primary wildcard.
            apply_extension(".bgcode");
        } else if (last_used_ext_lower == ".gcode" || (bgcode_allowed && last_used_ext_lower == ".bgcode")) {
            apply_extension(ext);
        }

        // Fallback for when the parsed filename inherently contains .bgcode but binary gcode is disabled
        std::string preferred_extension_lower = name_data.preferred_extension;
        std::transform(
            preferred_extension_lower.begin(),
            preferred_extension_lower.end(),
            preferred_extension_lower.begin(),
            [](unsigned char c) { return std::tolower(c); }
        );

        if (!bgcode_allowed && preferred_extension_lower == ".bgcode") {
            apply_extension(".gcode");
        }

    } else if (last_used_ext_lower == ".sl1" || last_used_ext_lower == ".sl1s") { 
        apply_extension(ext);
    }

    return name_data;
}

void validate_bgcode_extension(
    const boost::filesystem::path& file_path,
    bool bgcode_allowed,
    const std::function<void(const boost::filesystem::path&)>& on_proceed,
    const std::function<void()>& on_retry)
{
    if (bgcode_allowed) {
        on_proceed(file_path);
        return;
    }

    std::string ext = file_path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });

    if (ext == ".bgcode") {
        AppServices::instance().dialog_manager().show_yesno_dialog(
            Biz::_u8L("Invalid File Format"),
            Biz::_u8L("Binary G-Code is disabled in the current printer settings. Save as standard G-Code instead?"),
            [file_path, on_proceed, on_retry](bool yes) {
                if (yes) {
                    auto new_path = file_path;
                    new_path.replace_extension(".gcode");
                    on_proceed(new_path);
                } else {
                    on_retry();
                }
            }
        );
    } else {
        on_proceed(file_path);
    }
}

void show_export_modal_dialog(
    const Biz::ProjectInteractor& project_interactor,
    bool default_path_at_removable,
    const std::function<void(bool result, const std::vector<boost::filesystem::path>& file_paths)>& callback,
    const std::string& wildcards_overide /*= std::string()*/
)
{
    boost::filesystem::path default_folder = project_interactor.output_dir(
        project_interactor.selected_project_id(),
        default_path_at_removable,
        AppServices::instance().app_config().get<std::string>("last_used_directory")
    );

    ExportNameData name_data = get_export_name_data(project_interactor);

    bool bgcode_allowed = true;
    const auto& cbox = project_interactor.preset_interactor().selected_printer_preset().printer.config_box();
    if (const auto* item = cbox.find("binary_gcode").item; item) {
        bgcode_allowed = item->get<bool>();
    }

    std::string wildcards = wildcards_overide.empty() ?
        gen_wildcards(name_data.preferred_extension, name_data.technology, bgcode_allowed) :
        wildcards_overide;

    std::string filename = name_data.filename;

    Biz::Platform::PlatformServices::instance().main_thread_dispatcher().dispatch_on_main_thread(
        [pi_raw = &project_interactor, default_path_at_removable, wildcards_overide, 
         default_folder, filename, wildcards, callback, bgcode_allowed]()
        {
            auto wrapped_callback = [callback, bgcode_allowed, pi_raw, default_path_at_removable, wildcards_overide](bool result, const std::vector<boost::filesystem::path>& file_paths) {
                if (!result || file_paths.empty()) {
                    callback(result, file_paths);
                    return;
                }

                validate_bgcode_extension(
                    file_paths.front(),
                    bgcode_allowed,
                    [callback, file_paths](const boost::filesystem::path& safe_path) {
                        auto new_paths = file_paths;
                        new_paths.front() = safe_path;
                        callback(true, new_paths);
                    },
                    [pi_raw, default_path_at_removable, callback, wildcards_overide]() {
                        show_export_modal_dialog(*pi_raw, default_path_at_removable, callback, wildcards_overide);
                    }
                );
            };

            AppServices::instance().dialog_manager().show_file_dialog(
                FileDialogType::Save,
                "Export as",
                default_folder,
                filename,
                wildcards,
                wrapped_callback
            );
        }
    );
}

void show_upload_modal_dialog(
    const Biz::ProjectInteractor& project_interactor,
    const std::vector<Biz::PrintHost::PrintHostAfterUploadAction>& post_actions,
    const std::function<void(const std::string&, Biz::PrintHost::PrintHostAfterUploadAction)>& callback
)
{
    ExportNameData name_data = get_export_name_data(project_interactor);

    bool bgcode_allowed = true;
    const auto& cbox = project_interactor.preset_interactor().selected_printer_preset().printer.config_box();
    if (const auto* item = cbox.find("binary_gcode").item; item) {
        bgcode_allowed = item->get<bool>();
    }

    std::string filename = name_data.filename;

    Biz::Platform::PlatformServices::instance().main_thread_dispatcher().dispatch_on_main_thread(
        [pi_raw = &project_interactor, filename, post_actions, callback, bgcode_allowed]()
        {
            auto wrapped_callback = [post_actions, callback, bgcode_allowed, pi_raw](
                const std::string& input_filename,
                Biz::PrintHost::PrintHostAfterUploadAction action
            ) {
                if (input_filename.empty()) {
                    callback(input_filename, action);
                    return;
                }

                validate_bgcode_extension(
                    boost::filesystem::path(input_filename),
                    bgcode_allowed,
                    [callback, action](const boost::filesystem::path& safe_path) {
                        callback(safe_path.string(), action);
                    },
                    [pi_raw, post_actions, callback]() {
                        show_upload_modal_dialog(*pi_raw, post_actions, callback);
                    }
                );
            };

            std::vector<IDialogManager::ButtonWithCallback> buttons;
            buttons.reserve(post_actions.size() + 1);
            const auto add_button = [&buttons, wrapped_callback](
                const std::string& label,
                Biz::PrintHost::PrintHostAfterUploadAction action
            ) {
                buttons.push_back(
                    {label,
                     [wrapped_callback, action](const std::string& input_filename)
                     { wrapped_callback(input_filename, action); }}
                );
            };

            // TRN Button on the print host upload dialog.
            add_button(Biz::_u8L("Upload"), Biz::PrintHost::PrintHostAfterUploadAction::None);
            for (const Biz::PrintHost::PrintHostAfterUploadAction action : post_actions) {
                add_button(post_upload_action_label(action), action);
            }

            AppServices::instance().dialog_manager().show_input_dialog_with_buttons(
                Biz::_u8L("Send G-Code to printer host"),
                Biz::_u8L("Upload to printer host with the following filename:"),
                filename,
                buttons
            );
        }
    );
}

} // namespace  Slic3r::App
