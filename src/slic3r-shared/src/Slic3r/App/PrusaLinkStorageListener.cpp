#include "Slic3r/App/PrusaLinkStorageListener.hpp"

#include "Slic3r/App/AppServices.hpp"
#include "Slic3r/App/IDialogManager.hpp"
#include "Slic3r/Biz/ResultExport/ResultExportInteractor.hpp"
#include "Slic3r/Biz/PrintHost/PrintHostJob.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Log.hpp"

#include <nlohmann/json.hpp>
#include <libassert/assert.hpp>

namespace Slic3r::App::PrintHost {

struct PrintHostStorageInfo
{
    std::string name;
    std::string path;
    bool read_only       = false;
    long long free_space = -1;
};

namespace {
std::vector<PrintHostStorageInfo> get_storage_choices_from_json(const std::string& json_text)
{
    /*
    [{
        "name": "Storage",
        "path": "/mnt/storage",
        "read_only": false,
        "free_space": 123456789
    },
    {
        "name": "usb",
        "path": "/usb",
        "read_only": false
        "free_space": 123456789
    }]
    */
    std::vector<PrintHostStorageInfo> storage_list;
    try {
        nlohmann::json json = nlohmann::json::parse(json_text);

        for (const auto& item : json) {
            PrintHostStorageInfo storage;
            storage.name       = item.value("name", "");
            storage.path       = item.value("path", "");
            storage.read_only  = item.value("read_only", false);
            storage.free_space = item.value("free_space", -1);

            storage_list.emplace_back(std::move(storage));
        }
    } catch (const nlohmann::json::exception& e) {
        SPDLOG_ERROR("Error parsing JSON: {}", e.what());
    }
    return storage_list;
}
} // namespace

void PrusaLinkStorageListener::on_job_manager_status_changed(const Biz::Platform::JobManager::JobManagerStatus& status) 
{
    for (const auto& [job_name, progress] : status) {
        if (!job_name.starts_with("printhost")) {
            continue;
        }

        if (const auto* payload = std::any_cast<Biz::PrintHost::PrintHostJobProgressPayload>(&progress.progress_detail.payload)) {
            if ((Biz::PrintHost::PrintHostJobProgressState) progress.progress_detail.info != Biz::PrintHost::PrintHostJobProgressState::Info) {
                continue;
            }
            if (payload->tag != Biz::PrintHost::PrintHostJobInfoTag::Storage) {
                continue;
            }
            std::vector<PrintHostStorageInfo> storage_list = get_storage_choices_from_json(payload->message);
            if (storage_list.empty()) {
                continue;
            }
            
            std::vector<std::string> candidates;
            for (const PrintHostStorageInfo& info : storage_list) {
                // -1 in free_space means data not available
                if (info.free_space == 0 || info.read_only) {
                    continue;
                }
                candidates.emplace_back(info.path);
                
            }

            if (candidates.empty()) {
                // TODO: error notification
                SPDLOG_ERROR("Upload to PrusaLink has failed: No suitable storage found.");
                continue;
            }

            std::string storage = AppServices::instance().dialog_manager().show_combo_dialog(Biz::_u8L("Choose Storage"),Biz::_u8L("Choose storage for upload:"), candidates);

            if (storage.empty()) {
                // canceled by user
                continue;
            }
            m_project_interactor.result_export_interactor().on_storage_resolved(payload->id, storage);

        } else {
            // Ignore all progress without payload. It should be only first started status.
            ASSERT(progress.status == Domain::JobStatus::Started);
            continue;
        }
    }
}

} // namespace Slic3r::App