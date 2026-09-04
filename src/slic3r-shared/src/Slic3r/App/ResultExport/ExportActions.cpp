#include "Slic3r/App/ResultExport/ExportActions.hpp"
#include "Slic3r/App/ResultExport/ExportPathSelect.hpp"
#include "Slic3r/App/Browser/BrowserLogicConnectSelect.hpp"
#include "Slic3r/App/AppServices.hpp"
#include "Slic3r/App/IDialogManager.hpp"

#include <Slic3r/Biz/Platform/PlatformServices.hpp>
#include "Slic3r/Biz/PhysicalPrinter/PhysicalPrinterInteractor.hpp"
#include "Slic3r/Biz/PrintHost/PrintHostJobData.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/UserAccount/ConnectUtils.hpp"

#include <memory>
#include <variant>

namespace Slic3r::App::ExportActions {

using Biz::Platform::IMainThreadDispatcher;
using Biz::Platform::PlatformServices;

bool can_export(Biz::ProjectInteractor& project_interactor)
{
    if (project_interactor.scene_interactor().bed_selection().empty()) {
        return false;
    }
    const Domain::SlicingId slicing_id{ project_interactor.selected_bed_slicing_id() };
    const auto optional_status{ project_interactor.status_cache().get_status(slicing_id) };
    if (!optional_status) {
        return false;
    }
    const Biz::Slicing::Status status{ *optional_status };
    return status.code == Biz::Slicing::StatusCode::Finished;
}

static std::function<void()>
make_export(Biz::ProjectInteractor& project_interactor, bool to_flash)
{
    auto call_do_export{[pi_raw = &project_interactor, to_flash]()
                        {
                            ExportPathSelect::show_export_modal_dialog(
                                *pi_raw,
                                to_flash,
                                [=](bool result,
                                    const std::vector<boost::filesystem::path>& file_paths)
                                {
                                    if (result) {
                                        pi_raw->do_result_export(
                                            pi_raw->selected_bed_slicing_id(),
                                            file_paths.front()
                                        );
                                    }
                                }
                            );
                        }};

    return [=]()
    {
        IMainThreadDispatcher& dispatcher{PlatformServices::instance().main_thread_dispatcher()};
        if (!dispatcher.dispatch_on_main_thread_after(call_do_export)) {
            SPDLOG_INFO("Export request not dispatched!");
        }
    };
}

std::function<void()> export_gcode(Biz::ProjectInteractor& project_interactor)
{
    return make_export(project_interactor, false);
}

std::function<void()> export_gcode_to_flash(Biz::ProjectInteractor& project_interactor)
{
    return make_export(project_interactor, true);
}

std::function<void()> send_gcode_to_connect(Biz::ProjectInteractor& project_interactor)
{
    auto send_to_connect{
        [pi_raw = &project_interactor]()
        {
            bool bgcode_allowed = true;
            const auto& cbox = pi_raw->preset_interactor().selected_printer_preset().printer.config_box();
            if (const auto* item = cbox.find("binary_gcode").item; item) {
                bgcode_allowed = item->get<bool>();
            }

            auto wrapped_callback = [pi_raw, bgcode_allowed](bool result, const std::string& data) {
                if (!result || data.empty()) {
                    return;
                }
                std::string filename = Biz::UserAccount::ConnectUtils::filename_from_json(data);
                ExportPathSelect::validate_bgcode_extension(
                    boost::filesystem::path(filename),
                    bgcode_allowed,
                    [pi_raw, data](const boost::filesystem::path& safe_path) {
                        pi_raw->do_result_upload_connect(pi_raw->selected_bed_slicing_id(), data, safe_path.string());
                    },
                    [pi_raw]() {
                        // Re-trigger the dialog on retry
                        send_gcode_to_connect(*pi_raw)(); 
                    }
                );
            };

            AppServices::instance().dialog_manager().show_upload_webview_dialog(
                std::make_unique<Browser::BrowserLogicConnectSelect>(*pi_raw),
                pi_raw,
                wrapped_callback
            );
        }
    };
    return [=]()
    {
        IMainThreadDispatcher& dispatcher{PlatformServices::instance().main_thread_dispatcher()};
        if (!dispatcher.dispatch_on_main_thread_after(send_to_connect)) {
            SPDLOG_INFO("Send to connect not dispatched!");
        }
    };
}

std::function<void()> upload_gcode_to_print_host(Biz::ProjectInteractor& project_interactor)
{
    auto upload_fn{
        [pi_raw = &project_interactor]()
        {
            const auto& phys_printer =
                pi_raw->physical_printer_interactor().selected_physical_printer_data();
            const auto* payload =
                std::get_if<Biz::PhysicalPrinter::PrinterUpload>(&phys_printer.payload);
            if (payload == nullptr) {
                SPDLOG_INFO(
                    "Upload to Print Host aborted - the selected destination is no longer a print host."
                );
                return;
            }

            auto print_host_config =
                std::make_shared<const Biz::PhysicalPrinter::PhysicalPrinterConfig>(phys_printer);

            ExportPathSelect::show_upload_modal_dialog(
                *pi_raw,
                Biz::PrintHost::get_post_upload_actions(payload->type),
                [pi_raw, print_host_config](
                    const std::string& file_path,
                    Biz::PrintHost::PrintHostAfterUploadAction post_action
                ) {
                    if (file_path.empty()) {
                        return;
                    }
                    pi_raw->do_result_upload(
                        pi_raw->selected_bed_slicing_id(),
                        file_path,
                        post_action,
                        *print_host_config
                    );
                }
            );
        }
    };

    return [=]()
    {
        IMainThreadDispatcher& dispatcher{PlatformServices::instance().main_thread_dispatcher()};
        if (!dispatcher.dispatch_on_main_thread_after(upload_fn)) {
            SPDLOG_INFO("Upload to Print Host not dispatched!");
        }
    };
}

} // namespace Slic3r::App::ExportActions
