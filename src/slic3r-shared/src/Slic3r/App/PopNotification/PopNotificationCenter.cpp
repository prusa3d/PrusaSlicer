#include "Slic3r/App/PopNotification/PopNotificationCenter.hpp"
#include "Slic3r/App/AppServices.hpp"
#include "Slic3r/App/Platform/IFileExplorerHandler.hpp"
#include "Slic3r/Biz/FileDownloader/FileDownloaderJob.hpp"
#include "Slic3r/App/DisplayStrings.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"

#include <ranges>
#include <boost/filesystem/operations.hpp>

using Slic3r::Biz::Platform::JobManager::JobManagerStatus;
using Slic3r::Biz::Platform::JobManager::Progress;
using Slic3r::Domain::JobStatus;
using SlicingStatusCode = Slic3r::Biz::Slicing::StatusCode;
using Slic3r::Biz::PrintHost::PrintHostJobProgressPayload;
using Slic3r::Biz::PrintHost::PrintHostJobProgressState;
using Slic3r::Biz::PrintHost::PrintHostJobInfoTag;
using Slic3r::Biz::FileDownloader::FileDownloaderJobProgressPayload;
using Slic3r::Domain::SlicingId;

using namespace Slic3r::Biz;

namespace Slic3r::App::PopNotification {

PopNotificationCenter::PopNotificationCenter(Biz::ProjectInteractor& project_interactor) :
    m_removable_drive_service(project_interactor.removable_drive_service()),
    m_project_interactor(project_interactor)
{
    m_project_interactor.status_cache().add_listener<Biz::IStatusCacheChangedListener>(this);
    m_project_interactor.add_listener<Biz::IProjectsChangedListener>(this);
    m_project_interactor.arrange_interactor().add_listener<Biz::IArrangeEventsListener>(this);
    m_list_sort_filter.set_source_model(&m_notification_list);

    auto sort_fn = [](const PopNotificationData& lhs, const PopNotificationData& rhs)
    {
        if (lhs.level == PopNotificationLevel::ProgressNoClose || lhs.level == PopNotificationLevel::ProgressWithClose) {
            return false;
        }
        if (rhs.level == PopNotificationLevel::ProgressNoClose || rhs.level == PopNotificationLevel::ProgressWithClose) {
            return true;
        }
        return false;
    };
    m_list_sort_filter.set_sort_fn(sort_fn);
}

void PopNotificationCenter::upsert_notification(PopNotificationData data, PopNotificationObservableList::Matcher matcher)
{
    m_notification_list.upsert_notification(std::move(data), matcher);
}

struct JobNotificationSpec
{
    std::string key_prefix;
    std::string started_header;
    std::string finished_header;
    std::string failed_header;
    Render::Icon icon{Render::Icon::None};
};
void PopNotificationCenter::close_notifications_of_type(PopNotificationType type)
{
    m_notification_list.close_notifications_of_type(type);
}

namespace {
const JobNotificationSpec* find_job_spec(const std::string& job_key)
{
    static const std::vector<JobNotificationSpec> specs{
        JobNotificationSpec{
            .key_prefix = "arrange",
            // TRN Header of an arrange notification while the arrange runs.
            .started_header = L("Arranging..."),
            // TRN Header of a finished arrange notification.
            .finished_header = L("Arrange Finished"),
            // TRN Header of a failed arrange notification.
            .failed_header = L("Arrange Failed"),
            .icon = Render::Icon::Layout
        },
        JobNotificationSpec{
            .key_prefix = "SimplifyJob",
            // TRN Header of a simplify notification while the simplification runs.
            .started_header = L("Simplifying..."),
            // TRN Header of a finished simplify notification.
            .finished_header = L("Simplify Finished"),
            // TRN Header of a failed simplify notification.
            .failed_header = L("Simplify Failed"),
            .icon = Render::Icon::Simplify
        },
        JobNotificationSpec{
            .key_prefix = "RepairObjectMesh",
            // TRN Header of a repair notification while the mesh repair runs.
            .started_header = L("Repairing..."),
            // TRN Header of a finished repair notification.
            .finished_header = L("Repair Finished"),
            // TRN Header of a failed repair notification.
            .failed_header = L("Repair Failed")
        }
    };

    for (const JobNotificationSpec& spec : specs) {
        if (job_key.starts_with(spec.key_prefix)) {
            return &spec;
        }
    }
    return nullptr;
}

std::string job_status_header(const JobStatus& status, const JobNotificationSpec& spec)
{
    switch (status) {
    case JobStatus::Finished:
        return _u8(spec.finished_header);
    case JobStatus::Failed:
        return _u8(spec.failed_header);
    case JobStatus::Started:
        return _u8(spec.started_header);
    case JobStatus::None:
    default:
        break;
    }

    return {};
}
} // namespace

template <typename T>
std::function<bool(const PopNotificationPayload&, const PopNotificationPayload&)> cmp(
    std::function<bool(const T&, const T&)> comparator
)
{
    return [=](const PopNotificationPayload& a, const PopNotificationPayload& b)
    {
        const auto* a_payload{std::get_if<T>(&a)};
        const auto* b_payload{std::get_if<T>(&b)};
        if (a_payload == nullptr || b_payload == nullptr) {
            return false;
        }
        return comparator(*a_payload, *b_payload);
    };
}

void PopNotificationCenter::on_job_manager_status_changed(const JobManagerStatus& status)
{
    // Every new job, that should be displayed
    for (const auto& [job_name, progress] : status) {
        ASSERT(!job_name.empty());

        if (job_name.starts_with("printhost")) {
            on_job_print_host(job_name, progress);
        } else if (job_name.starts_with("file_download")) {
            on_download_job_status_changed(job_name, progress);
        } else if (job_name.starts_with("arrange")) {
            on_arrange_job_progress(job_name, progress);
        } else if (const JobNotificationSpec* spec{find_job_spec(job_name)}) {
            on_job_progress(*spec, job_name, progress);
        }
#ifndef NDEBUG
        else {
            // Runtime text, translation of an unknown msgid passes it through unchanged.
            const std::string debug_header{"DEBUG ONLY: " + job_name};
            on_job_progress(
                JobNotificationSpec{
                    .started_header = debug_header,
                    .finished_header = debug_header,
                    .failed_header = debug_header
                },
                job_name,
                progress
            );
        }
#endif
    }

    m_notification_list.erase_notification_by_predicate(
        [&status](const PopNotificationData& data)
        {
            if (data.type != PopNotificationType::JobProgress) {
                return false;
            }
            const auto* payload{std::get_if<JobProgressNotificationData>(&data.payload)};
            // finished and failed notifications are meant to stay for their timeout
            return payload != nullptr
                && payload->progress.status == JobStatus::Started
                && !status.contains(payload->job_name);
        }
    );
}

void PopNotificationCenter::on_job_progress(
    const JobNotificationSpec& spec,
    const std::string& job_key,
    const Biz::Platform::JobManager::Progress& progress)
{
    const std::string header{job_status_header(progress.status, spec)};

    if (header.empty()) {
        return;
    }

    if (progress.project_id != Domain::INVALID_ID
        && !m_project_interactor.project_exists(progress.project_id))
    {
        return;
    }

    PopNotificationLayout layout;
    if (progress.status == JobStatus::Started && progress.percent) {
        int perc = (int) (progress.percent.value().value * 100);
        layout   = PopNotificationLayoutHeaderProgress(header, perc, spec.icon);
    } else {
        layout = PopNotificationLayoutHeader(header, spec.icon);
    }

    PopNotificationData notification{
        PopNotificationType::JobProgress,
        progress.status == JobStatus::Started ? PopNotificationLevel::ProgressNoClose :
                                                PopNotificationLevel::ProgressWithClose,
        progress.status == JobStatus::Started ? 0s : 5s,
        layout,
        JobProgressNotificationData{job_key, progress},
        progress.project_id
    };

    using Payload = JobProgressNotificationData;
    const auto matcher{cmp<Payload>( //
        [](const Payload& a, const Payload& b) { return a.job_name == b.job_name; }
    )};
    m_notification_list.upsert_notification(std::move(notification), matcher);
}

void PopNotificationCenter::on_arrange_job_progress(
    const std::string& job_key,
    const Biz::Platform::JobManager::Progress& progress)
{
    if (progress.status == JobStatus::Started && progress.percent == std::nullopt) {
        m_notification_list.erase_notification_by_predicate(
            [](const PopNotificationData& data)
            { return data.type == PopNotificationType::ArrangeEvent; }
        );
    }

    if (const JobNotificationSpec* spec{find_job_spec(job_key)}) {
        on_job_progress(*spec, job_key, progress);
    }
}

void PopNotificationCenter::on_job_print_host(const std::string& job_key, const Progress& progress)
{

    size_t payload_id;
    PrintHostJobInfoTag payload_tag;
    std::string payload_message;
    if (const auto* payload = std::any_cast<PrintHostJobProgressPayload>(&progress.progress_detail.payload))
    {
        payload_id = payload->id;
        payload_tag = payload->tag;
        payload_message = payload->message;
    } else {
        // Ignore all progress without payload. It should be only first started status.
        ASSERT(progress.status == JobStatus::Started);
        return;
    }

    switch ((PrintHostJobProgressState) progress.progress_detail.info) {
    case PrintHostJobProgressState::None:
        if (progress.status == JobStatus::Finished)
        {
            on_print_host_done(payload_id);
        } else {
            if (progress.percent)
            {
                on_print_host_progress(payload_id, (int) (progress.percent.value().value * 100));
            }   
        }
        
        break;
    case PrintHostJobProgressState::Info: {
        on_print_host_info(payload_id, payload_tag, payload_message);
    } break;
    case PrintHostJobProgressState::Error: {
        on_print_host_error(payload_id, payload_message);
    } break;
    default:
        ASSERT(false, "Missing PrintHostJobProgressState handling.");
        break;
    }
    
}

const auto download_job_matcher{cmp<DownloadProgressNotificationData>( //
    [](const DownloadProgressNotificationData& a, const DownloadProgressNotificationData& b)
    { return a.download_id == b.download_id; }
)};

PopNotificationLayout PopNotificationCenter::download_finished_layout(
    const std::string& body,
    const boost::filesystem::path& dest_path,
    const std::string& printables_url,
    bool is_loaded
)
{
    // TRN Header of a finished file download notification.
    const std::string header{_u8L("Download Finished")};

    // The file has not been loaded into the scene yet: let the user pick where to open it.
    if (!is_loaded) {
        return PopNotificationLayoutHeaderTextButtons{
            header,
            body,
            // TRN Button on a finished download notification: load the file into the current project.
            {{_u8L("Load"),
              [this, dest_path]()
              {
                  m_project_interactor.open_downloaded_file(dest_path, false);
                  return true;
              }},
             // TRN Button on a finished download notification: load the file into a new project.
             {_u8L("Load as New Project"),
              [this, dest_path]()
              {
                  m_project_interactor.open_downloaded_file(dest_path, true);
                  return true;
              }}},
            Render::Icon::Printables
        };
    }

    // The file was already loaded: offer follow-up actions instead.
    std::vector<PopNotificationButtonData> buttons;
    if (!printables_url.empty()) {
        buttons.emplace_back(PopNotificationButtonData{
            // TRN Button on a finished download notification: open the Printables tab.
            _u8L("Printables"),
            [this, printables_url]()
            {
                if (m_switch_left_tab_fn) {
                    m_switch_left_tab_fn(LeftBarTabs::Printables, printables_url);
                }
                return true;
            }
        });
    }
    if (!dest_path.empty()) {
        buttons.emplace_back(PopNotificationButtonData{
            // TRN Button on a finished download notification: open the download folder.
            _u8L("Open Folder"),
            [dest_path]()
            {
                ASSERT(!dest_path.empty() && dest_path.has_parent_path());
                AppServices::instance().file_explorer_handler().open_folder(
                    dest_path.parent_path().string()
                );
                return true;
            }
        });
    }

    if (buttons.empty()) {
        return PopNotificationLayoutHeaderText{header, body, Render::Icon::Printables};
    }
    return PopNotificationLayoutHeaderTextButtons{
        header, body, std::move(buttons), Render::Icon::Printables
    };
}

void PopNotificationCenter::on_download_job_status_changed(
    const std::string& job_key,
    const Biz::Platform::JobManager::Progress& progress
)
{
    const auto* payload =
        std::any_cast<FileDownloaderJobProgressPayload>(&progress.progress_detail.payload);
    if (payload == nullptr) {
        // Ignore progress without payload
        return;
    }
    const size_t download_id{payload->download_id};
    const boost::filesystem::path dest_path{payload->final_path};
    const std::string printables_url{payload->project_url};
    const bool is_loaded{payload->load_count > 0};
    const size_t total_files{payload->total_files};

    // While downloading, the payload always describes a single file. The aggregated "N files"
    // wording is only meaningful once a multi-file download has finished.
    const bool single_file = total_files <= 1
        || progress.status == JobStatus::Started
        || progress.status == JobStatus::None;
    const std::string body = single_file ?
        payload->filename :
        // TRN Body of a multi-file download notification, {} is number of files (2 or more).
        fmt::format(fmt::runtime(_u8L("{} files")), total_files);

    PopNotificationLayout layout;
    PopNotificationLevel level;
    std::chrono::seconds timeout;

    switch (progress.status) {
    case JobStatus::Finished:
        layout  = download_finished_layout(body, dest_path, printables_url, is_loaded);
        level   = PopNotificationLevel::ProgressWithClose;
        timeout = 20s;
        timeout = is_loaded ? 20s : 0s;
        break;
    case JobStatus::Failed:
        // TRN Header of a failed file download notification.
        layout  = PopNotificationLayoutHeaderText{_u8L("Download Failed"), body};
        level   = PopNotificationLevel::Error;
        timeout = 20s;
        break;
    case JobStatus::Started:
    case JobStatus::None:
    default: {
        // TRN Header of an in-progress file download notification.
        const std::string header{_u8L("Downloading...")};
        if (progress.percent) {
            const int perc = (int) (progress.percent.value().value * 100);
            layout = PopNotificationLayoutHeaderTextProgress{
                header, body, perc, Render::Icon::Printables
            };
            level  = PopNotificationLevel::ProgressNoClose;
        } else {
            layout = PopNotificationLayoutHeaderText{header, body, Render::Icon::Printables};
            level  = PopNotificationLevel::ProgressWithClose;
        }
        timeout = 0s;
        break;
    }
    }

    m_notification_list.upsert_notification(
        PopNotificationData{
            PopNotificationType::DownloadProgress,
            level,
            timeout,
            std::move(layout),
            DownloadProgressNotificationData(download_id)
        },
        download_job_matcher
    );
}

namespace {
std::string slicing_status_to_string(const SlicingStatusCode status)
{
    switch (status) {
    case SlicingStatusCode::Empty:
        // TRN Slicing status label.
        return _u8L("Empty");
    case SlicingStatusCode::Updating:
        // TRN Slicing status label.
        return _u8L("Updating");
    case SlicingStatusCode::Running:
        // TRN Slicing status label.
        return _u8L("Slicing");
    case SlicingStatusCode::Finished:
        // TRN Slicing status label.
        return _u8L("Slicing Finished");
    case SlicingStatusCode::Modified:
        // TRN Slicing status label.
        return _u8L("Modified");
    case SlicingStatusCode::Stopping:
        // TRN Slicing status label.
        return _u8L("Slicing Stopped");
    case SlicingStatusCode::Removed:
        // TRN Slicing status label.
        return _u8L("Removed");
    case SlicingStatusCode::InvalidData:
        // TRN Slicing status label.
        return _u8L("Invalid settings");
    default:
        return "Unknown";
    }
}

} // namespace


const auto slicing_matcher{cmp<SlicingStatusNotificationData>( //
    [](const SlicingStatusNotificationData& a, const SlicingStatusNotificationData& b)
    { return a.slicing_id.project_id == b.slicing_id.project_id; }
)};

void PopNotificationCenter::on_status_cache_status_code_changed(const SlicingId slicing_id)
{
    auto optional_status{m_project_interactor.status_cache().get_status(slicing_id)};
    if (!optional_status) {
        return;
    }
    const Biz::Slicing::Status status{std::move(*optional_status)};
    if (status.code != SlicingStatusCode::Running
        && status.code != SlicingStatusCode::Finished
        && status.code != SlicingStatusCode::Stopping
        && status.code != SlicingStatusCode::InvalidData
        // The partial slicing ends back in the Modified state, so the notification has to be erased too.
        && status.code != SlicingStatusCode::Modified)
    {
        return;
    }

    using Payload = SlicingStatusNotificationData;
    if (status.code != SlicingStatusCode::Running) {
        m_notification_list.erase_notification_by_predicate(
            [slicing_id](const PopNotificationData& notification)
            {
                const auto payload{std::get_if<Payload>(&notification.payload)};
                if (payload == nullptr) {
                    return false;
                }
                return payload->slicing_id == slicing_id;
            }
        );
        return;
    }

    if (m_project_interactor.workbench().find_project_by_id(slicing_id.project_id) == nullptr) {
        // The project may have been deleted before the queued callback was executed.
        return;
    }

    const std::string header{m_project_interactor.get_project_name(slicing_id.project_id)};
    const std::string text{slicing_status_to_string(status.code)};

    m_notification_list.upsert_notification(
        PopNotificationData{
            PopNotificationType::SlicingProgress,
            status.code == SlicingStatusCode::Running ? PopNotificationLevel::ProgressNoClose : PopNotificationLevel::ProgressWithClose,
            status.code == Biz::Slicing::StatusCode::Finished ? 5s : 0s,
            PopNotificationLayoutHeaderText{header, text},
            SlicingStatusNotificationData{slicing_id},
            slicing_id.project_id
        },
        slicing_matcher
    );
}

void PopNotificationCenter::on_status_cache_progress_changed(const Domain::SlicingId slicing_id) {
    auto optional_status{m_project_interactor.status_cache().get_status(slicing_id)};
    if (!optional_status) {
        return;
    }
    const Biz::Slicing::Status status{std::move(*optional_status)};
    if (!status.progress) {
        return;
    }

    if (m_project_interactor.workbench().find_project_by_id(slicing_id.project_id) == nullptr) {
        // The project may have been deleted before the queued callback was executed.
        return;
    }

    const std::string header{m_project_interactor.get_project_name(slicing_id.project_id)};
    const std::string text{to_display_string(status.progress->progress_info)};

    const int progress{static_cast<int>(std::round(status.progress->progress.value))};
    m_notification_list.upsert_notification(
        PopNotificationData{
            PopNotificationType::SlicingProgress,
            status.code == SlicingStatusCode::Running ? PopNotificationLevel::ProgressNoClose : PopNotificationLevel::ProgressWithClose,
            0s,
            PopNotificationLayoutHeaderTextProgress{header, text, progress},
            SlicingStatusNotificationData{slicing_id},
            slicing_id.project_id
        },
        slicing_matcher
    );
}

void PopNotificationCenter::on_status_cache_errors_changed(const Domain::SlicingId slicing_id) {
    using Biz::Slicing::Error;
    using Biz::Slicing::ErrorCode;
    using Payload = SlicingErrorNotificationData;

    auto optional_status{m_project_interactor.status_cache().get_status(slicing_id)};
    if (!optional_status) {
        return;
    }

    std::set<ErrorCode> present_error_codes;
    for (const Error& error : optional_status->errors) {
        present_error_codes.insert(error.code);
    }
    m_notification_list.erase_notification_by_predicate(
        [&](const PopNotificationData& notification)
        {
            const auto payload{std::get_if<Payload>(&notification.payload)};
            if (payload == nullptr) {
                return false;
            }
            return payload->slicing_id == slicing_id
                && !present_error_codes.contains(payload->error_code);
        }
    );

    if (m_project_interactor.workbench().find_project_by_id(slicing_id.project_id) == nullptr) {
        // The project may have been deleted before the queued callback was executed.
        return;
    }

    const std::vector<Error> errors{
        m_project_interactor.status_cache().extract_latest_errors(slicing_id)
    };

    const auto matcher{cmp<Payload>( //
        [](const Payload& a, const Payload& b)
        {
            return a.slicing_id.project_id == b.slicing_id.project_id
                && a.error_code == b.error_code;
        }
    )};
    const std::string header{m_project_interactor.get_project_name(slicing_id.project_id)};
    const Domain::Project& project{m_project_interactor.workbench().project(slicing_id.project_id)};
    for (const Error& error : errors) {
        m_notification_list.upsert_notification(
            PopNotificationData{
                PopNotificationType::SlicingError,
                PopNotificationLevel::Error,
                0s,
                PopNotificationLayoutHeaderTextButtons{
                    header,
                    to_display_string(error, project),
                    {
                        PopNotificationButtonData{
                            _u8L("See more"),
                            [this]()
                            {
                                if (m_open_invalid_data_dialog_fn) {
                                    m_open_invalid_data_dialog_fn();
                                }
                                return true;
                            }
                        }
                    }
                },
                Payload{error.code, slicing_id},
                slicing_id.project_id
            },
            matcher
        );
    }
}

void PopNotificationCenter::on_status_cache_warnings_changed(const Domain::SlicingId slicing_id) {
    using Biz::Slicing::Warning;
    using Biz::Slicing::WarningCode;
    using Biz::Slicing::WarningSeverity;
    using Payload = SlicingWarningNotificationData;

    auto optional_status{m_project_interactor.status_cache().get_status(slicing_id)};
    if (!optional_status) {
        return;
    }

    std::set<WarningCode> present_warning_codes;
    for (const Warning& warning : optional_status->warrnings) {
        present_warning_codes.insert(warning.code);
    }
    m_notification_list.erase_notification_by_predicate(
        [&](const PopNotificationData& notification)
        {
            const auto payload{std::get_if<Payload>(&notification.payload)};
            if (payload == nullptr) {
                return false;
            }
            return payload->slicing_id == slicing_id
                && !present_warning_codes.contains(payload->warning_code);
        }
    );

    if (m_project_interactor.workbench().find_project_by_id(slicing_id.project_id) == nullptr) {
        // The project may have been deleted before the queued callback was executed.
        return;
    }

    const std::vector<Warning> warnings{
        m_project_interactor.status_cache().extract_latest_warnings(slicing_id)
    };

    const auto matcher{cmp<Payload>( //
        [](const Payload& a, const Payload& b)
        {
            return a.slicing_id.project_id == b.slicing_id.project_id
                && a.warning_code == b.warning_code;
        }
    )};
    const std::string header{m_project_interactor.get_project_name(slicing_id.project_id)};
    const Domain::Project& project{m_project_interactor.workbench().project(slicing_id.project_id)};
    for (const Warning& warning : warnings) {
        m_notification_list.upsert_notification(
            PopNotificationData{
                PopNotificationType::SlicingWarning,
                warning.severity == WarningSeverity::LOW ? PopNotificationLevel::Warning : PopNotificationLevel::Error,
                0s,
                PopNotificationLayoutHeaderText{header, to_display_string(warning, project)},
                Payload{warning.code, slicing_id},
                slicing_id.project_id
            },
            matcher
        );
    }
}

namespace {
/*
enum class  PrintHostJobStatus
{
    None,
    Started,
    Finished,
    Failed
};
*/

PopNotificationLayout upload_layout(const PrintHostProgressNotificationData& data)
{
    switch (data.status) {
    case PrintHostJobStatus::None: {
        std::string msg;
        if (data.filename.empty() && data.target.empty()) {
            msg = "Upload is starting.";
        } else if (data.target.empty()) {
            msg = fmt::format("Upload of {} is starting.", data.filename);
        } else if (data.filename.empty()) {
            msg = fmt::format("Upload to {} is starting.", data.target);
        } else {
            msg = fmt::format("Upload of {} to {} is starting.", data.filename, data.target);
        }
        return PopNotificationLayoutText(std::move(msg));
    }
    case PrintHostJobStatus::Started: {
        std::string msg;
        if (data.filename.empty() && data.target.empty()) {
            msg = "Uploading.";
        } else if (data.target.empty()) {
            msg = fmt::format("Uploading {}.", data.filename);
        } else if (data.filename.empty()) {
            msg = fmt::format("Uploading to {}.", data.target);
        } else {
            msg = fmt::format("Uploading {} to {}.", data.filename, data.target);
        }
        return PopNotificationLayoutTextProgress(std::move(msg), data.progress);
    }
    case PrintHostJobStatus::Finished: {
        std::string msg;
        if (data.filename.empty() && data.target.empty()) {
            msg = "Uploading has finished.";
        } else if (data.target.empty()) {
            msg = fmt::format("Uploading {} has finished.", data.filename);
        } else if (data.filename.empty()) {
            msg = fmt::format("Uploading to {} has finished.", data.target);
        } else {
            msg = fmt::format("Uploading {} to {} has finished.", data.filename, data.target);
        }
        return PopNotificationLayoutText(std::move(msg));
    }
    case PrintHostJobStatus::Failed: {
        std::string msg;
        if (data.filename.empty() && data.target.empty()) {
            msg = fmt::format("Uploading has Failed. {}", data.additional_msg);
        } else if (data.target.empty()) {
            msg = fmt::format("Uploading {} has Failed. {}", data.filename, data.additional_msg);
        } else if (data.filename.empty()) {
            msg = fmt::format("Uploading to {} has Failed. {}", data.target, data.additional_msg);
        } else {
            msg = fmt::format(
                "Uploading {} to {} has Failed. {}",
                data.filename,
                data.target,
                data.additional_msg
            );
        }
        return PopNotificationLayoutText(std::move(msg));
    }
    default:
        ASSERT(false);
    }
    return PopNotificationLayoutText("");
}

PopNotificationLayout storage_resolve_layout(const PrintHostProgressNotificationData& data)
{
    switch (data.status) {
    case PrintHostJobStatus::None: {
        // TRN PrusaLink storage resolve notification.
        return PopNotificationLayoutText(_u8L("Resolving PrusaLink storage."));
    }
    case PrintHostJobStatus::Started: {
        // TRN PrusaLink storage resolve notification.
        return PopNotificationLayoutTextProgress(_u8L("Resolving PrusaLink storage."), data.progress);
    }
    case PrintHostJobStatus::Finished: {
        // TRN PrusaLink storage resolve notification.
        return PopNotificationLayoutText(_u8L("Resolving storage has finished."));
    }
    case PrintHostJobStatus::Failed: {
        // TRN PrusaLink storage resolve notification.
        std::string translatable_part = _u8L("Resolving storage has Failed.");
        std::string msg = fmt::format("{} {}", translatable_part, data.additional_msg);       
        return PopNotificationLayoutText(std::move(msg));
    }
    default:
        ASSERT(false);
    }
    return PopNotificationLayoutText("");
}

PopNotificationLayout export_layout(const PrintHostProgressNotificationData& data)
{
    std::string header;
    // Fallback to filename if target path is empty, otherwise show the target path
    std::string body = data.target.empty() ? data.filename : data.target;

    switch (data.status) {
    case PrintHostJobStatus::None:
        // TRN Header of an export notification.
        header = _u8L("Export Starting");
        return PopNotificationLayoutHeaderText(header, body);

    case PrintHostJobStatus::Started:
        // TRN Header of an export notification.
        header = _u8L("Exporting...");
        return PopNotificationLayoutHeaderTextProgress(header, body, data.progress);

    case PrintHostJobStatus::Finished: {
        // TRN Header of an export notification.
        header = _u8L("Export Finished");
        
        if (data.target.empty()) {
            return PopNotificationLayoutHeaderText(header, body);
        }

        std::vector<PopNotificationButtonData> buttons;
        // TRN Button on a finished export notification: open the target folder.
        buttons.push_back({_u8L("Open folder"), [data]() {
            boost::filesystem::path target_path(data.target);
            ASSERT(!target_path.empty() && target_path.has_parent_path());
            AppServices::instance().file_explorer_handler().open_folder(
                target_path.parent_path().string()
            );
            return false;
        }});

        if (data.eject_fn != nullptr) {
            // TRN Button on a finished export notification: eject the removable drive.
            buttons.push_back({_u8L("Eject"), [data]() {
                data.eject_fn(data.target);
                return true;
            }});
        }

        return PopNotificationLayoutHeaderTextButtons(header, body, std::move(buttons));
    }
    case PrintHostJobStatus::Failed:
        // TRN Header of an export notification.
        header = _u8L("Export Failed");
        body = body.empty() ? data.additional_msg : fmt::format("{}\n{}", body, data.additional_msg);
        return PopNotificationLayoutHeaderText(header, body);
        
    default:
        ASSERT(false);
        return PopNotificationLayoutText("");
    }
}

PopNotificationLayout print_host_layout(const PrintHostProgressNotificationData& data)
{
    if (data.is_storage_resolve) {
        return storage_resolve_layout(data);
    }
    if (data.is_upload) {
        return upload_layout(data);
    } else {
        return export_layout(data);
    }
}

const auto print_host_matcher{cmp<PrintHostProgressNotificationData>( //
    [](const PrintHostProgressNotificationData& a, const PrintHostProgressNotificationData& b)
    { return a.print_host_id == b.print_host_id; }
)};

} // namespace

void PopNotificationCenter::on_print_host_progress(size_t print_host_id, int progress)
{
    using namespace std::chrono_literals;

    using Payload = PrintHostProgressNotificationData;
    const Payload* previous_payload{m_notification_list.get_notification_payload<Payload>(
        [=](const Payload& payload) { return payload.print_host_id == print_host_id; }
    )};
    Payload payload{previous_payload == nullptr ? Payload{print_host_id} : *previous_payload};
    payload.status   = PrintHostJobStatus::Started;
    payload.progress = progress;
    auto layout{print_host_layout(payload)};

    m_notification_list.upsert_notification(
        PopNotificationData{
            PopNotificationType::PrintHostProgress,
            PopNotificationLevel::ProgressNoClose,
            0s,
            layout,
            std::move(payload)
        },
        print_host_matcher
    );
}

void PopNotificationCenter::on_print_host_error(size_t print_host_id, const std::string& msg)
{
    using Payload = PrintHostProgressNotificationData;
    const Payload* previous_payload{m_notification_list.get_notification_payload<Payload>(
        [=](const Payload& payload) { return payload.print_host_id == print_host_id; }
    )};
    Payload payload{previous_payload == nullptr ? Payload{print_host_id} : *previous_payload};
    payload.status         = PrintHostJobStatus::Failed;
    payload.progress       = -1;
    payload.additional_msg = msg;
    auto layout            = print_host_layout(payload);
    m_notification_list.upsert_notification(
        PopNotificationData{
            PopNotificationType::PrintHostProgress,
            PopNotificationLevel::Error,
            0s,
            std::move(layout),
            std::move(payload)
        },
        print_host_matcher
    );
}

void PopNotificationCenter::on_print_host_cancel(size_t print_host_id)
{
    m_notification_list.erase_notification_by_predicate(
            [print_host_id](const PopNotificationData& notification)
            {
                const auto payload{std::get_if<PrintHostProgressNotificationData>(&notification.payload)};
                if (payload == nullptr) {
                    return false;
                }
                return payload->print_host_id == print_host_id;
            }
        );
}

void PopNotificationCenter::on_print_host_done(size_t print_host_id)
{
    using Payload = PrintHostProgressNotificationData;
    const Payload* previous_payload{m_notification_list.get_notification_payload<Payload>(
        [=](const Payload& payload) { return payload.print_host_id == print_host_id; }
    )};
    Payload payload{previous_payload == nullptr ? Payload{print_host_id} : *previous_payload};
    if (payload.is_storage_resolve)
    {
        // close notification
        m_notification_list.erase_notification_by_predicate(
            [print_host_id](const PopNotificationData& notification)
            {
                const auto payload{
                    std::get_if<PrintHostProgressNotificationData>(&notification.payload)
                };
                return payload ? payload->print_host_id == print_host_id : false;
            }
        );
        return;
    }
    payload.status   = PrintHostJobStatus::Finished;
    payload.progress = 100;
    auto layout      = print_host_layout(payload);
    bool simple = false;
    if (std::holds_alternative<PopNotificationLayoutText>(layout)) {
        simple = true;
    }
    m_notification_list.upsert_notification(
        PopNotificationData{
            PopNotificationType::PrintHostProgress,
            PopNotificationLevel::ProgressWithClose,
            simple ? 10s : 60s,
            std::move(layout),
            std::move(payload)
        },
        print_host_matcher
    );
}

void PopNotificationCenter::on_print_host_info(
    size_t print_host_id,
    const PrintHostJobInfoTag& tag,
    const std::string& msg
)
{
    using Payload = PrintHostProgressNotificationData;
    const Payload* previous_payload{m_notification_list.get_notification_payload<Payload>(
        [=](const Payload& payload) { return payload.print_host_id == print_host_id; }
    )};
    Payload payload{previous_payload == nullptr ? Payload{print_host_id} : *previous_payload};
    if (tag == PrintHostJobInfoTag::Filename) {
        payload.filename = msg;
    }
    if (tag == PrintHostJobInfoTag::Resolve) {
        payload.target = msg;
        if (m_removable_drive_service.is_path_on_removable_drive(boost::filesystem::path(msg))) {
            payload.eject_fn = [this](const boost::filesystem::path& path)
                { 
                    m_removable_drive_service.eject_drive(path); 
                };
        }
    }
    if (tag == PrintHostJobInfoTag::OperationType && msg == "export") {
        payload.is_upload = false;
    }
    if (tag == PrintHostJobInfoTag::OperationType && msg == "storage") {
        payload.is_storage_resolve = true;
    }
    
    auto layout = print_host_layout(payload);
    m_notification_list.upsert_notification(
        PopNotificationData{
            PopNotificationType::PrintHostProgress,
            payload.status == PrintHostJobStatus::Started ? PopNotificationLevel::ProgressNoClose : PopNotificationLevel::ProgressWithClose,
            0s,
            std::move(layout),
            std::move(payload)
        },
        print_host_matcher
    );
}

namespace {
std::string removable_drive_status_to_string(
    const boost::filesystem::path& drive_path,
    Biz::RemovableDrive::RemovableDriveStatus status
)
{
    switch (status) {
    case Slic3r::Biz::RemovableDrive::RemovableDriveStatus::Inserted:
        return {};
    case Slic3r::Biz::RemovableDrive::RemovableDriveStatus::Ejecting:
        return fmt::format("Ejecting {}.", drive_path.string());

    case Slic3r::Biz::RemovableDrive::RemovableDriveStatus::Removed:
        return fmt::format("Ejecting {} done.", drive_path.string());
        break;
    case Slic3r::Biz::RemovableDrive::RemovableDriveStatus::Failed:
        return fmt::format("Ejecting {} has failed.", drive_path.string());
        break;
    default:
        ASSERT(false, "Missing status handling.");
    }
    return {};
}
} // namespace

void PopNotificationCenter::on_removable_drive_status_changed(
    const boost::filesystem::path& drive_path,
    Biz::RemovableDrive::RemovableDriveStatus status
)
{
    // Ignore inserted state.
    if (status == Slic3r::Biz::RemovableDrive::RemovableDriveStatus::Inserted) {
        return;
    }

    using Payload = EjectNotificationData;
    const auto matcher{cmp<Payload>( //
        [](const Payload& a, const Payload& b) { return a.drive_path == b.drive_path; }
    )};
    m_notification_list.upsert_notification(
        PopNotificationData{
            PopNotificationType::Eject,
            status != Slic3r::Biz::RemovableDrive::RemovableDriveStatus::Failed ?
                PopNotificationLevel::Regular :
                PopNotificationLevel::Warning,
            status == Slic3r::Biz::RemovableDrive::RemovableDriveStatus::Ejecting ? 0s : 10s,
            PopNotificationLayoutText(removable_drive_status_to_string(drive_path, status)),
            EjectNotificationData(drive_path, status)
        },
        matcher
    );
}

const auto user_account_login_matcher{cmp<UserAccountLoginNotificationData>( //
    [](const UserAccountLoginNotificationData&, const UserAccountLoginNotificationData&)
    { return true; }
)};

void PopNotificationCenter::on_user_account_id_success(bool is_refresh, const std::string& username)
{
    if (is_refresh) {
        return;
    }
    m_notification_list.close_notifications_of_type(PopNotificationType::UserAccountLogin);

    const boost::filesystem::path avatar{m_project_interactor.user_account_interactor().avatar()};
    const std::string image_path{boost::filesystem::exists(avatar) ? avatar.string() : std::string{}};

    m_notification_list.upsert_notification(
        PopNotificationData{
            PopNotificationType::UserAccountLogin,
            PopNotificationLevel::Regular,
            10s,
            PopNotificationLayoutImageHeader(image_path,
                // TRN User account login notification, {} is the user name.
                fmt::format(fmt::runtime(_u8L("Signed in as {}")), username)),
            UserAccountLoginNotificationData{}
        },
        user_account_login_matcher
    );
}

void PopNotificationCenter::on_avatar_downloaded()
{
    auto& user_account = m_project_interactor.user_account_interactor();
    if (!user_account.is_logged_in()) {
        return;
    }
    // Only update an already shown login notification; avoid popping a new one when the
    // avatar is re-downloaded outside of a fresh login (e.g. during a token refresh).
    std::function<bool(const UserAccountLoginNotificationData&)> any =
        [](const UserAccountLoginNotificationData&) { return true; };
    if (!m_notification_list.get_notification_payload<UserAccountLoginNotificationData>(any)) {
        return;
    }

    const boost::filesystem::path avatar{user_account.avatar()};
    if (!boost::filesystem::exists(avatar)) {
        return;
    }

    m_notification_list.upsert_notification(
        PopNotificationData{
            PopNotificationType::UserAccountLogin,
            PopNotificationLevel::Regular,
            10s,
            PopNotificationLayoutImageHeader(avatar.string(),
                // TRN User account login notification, {} is the user name.
                fmt::format(fmt::runtime(_u8L("Signed in as {}")), user_account.username())),
            UserAccountLoginNotificationData{}
        },
        user_account_login_matcher
    );
}

void PopNotificationCenter::on_user_account_logged_out()
{
    m_notification_list.close_notifications_of_type(PopNotificationType::UserAccountLogin);
    m_notification_list.close_notifications_of_type(PopNotificationType::UserAccountTransientError);

    const boost::filesystem::path avatar{m_project_interactor.user_account_interactor().avatar()};
    const std::string image_path{boost::filesystem::exists(avatar) ? avatar.string() : std::string{}};

    m_notification_list.upsert_notification(
        PopNotificationData{
            PopNotificationType::UserAccountLogin,
            PopNotificationLevel::Regular,
            10s,
            // TRN User account logout notification.
            PopNotificationLayoutImageHeader(image_path, _u8L("Successfully signed out")),
            UserAccountLoginNotificationData{}
        },
        never_equal_matcher
    );
}

void PopNotificationCenter::on_user_account_will_refresh()
{ /*unused*/
}

void PopNotificationCenter::on_user_account_action_retry(
    const Biz::Network::IHttp::Retry& retry,
    std::function<void(void)> cancel_callback
)
{
    ASSERT(cancel_callback);
    m_notification_list.close_notifications_of_type(PopNotificationType::UserAccountLogin);
    m_notification_list.close_notifications_of_type(PopNotificationType::UserAccountTransientError);

    std::string text = fmt::format(
        // TRN Prusa Account notification shown while a request is being retried. {} is the number of the current attempt.
        fmt::runtime(_u8L("Communication is taking longer than expected. Retrying. Attempt {}.")),
        std::to_string(retry.attempt)
    );
    m_notification_list.upsert_notification(
        PopNotificationData{
            PopNotificationType::UserAccountTransientError,
            PopNotificationLevel::Warning,
            0s,
            PopNotificationLayoutHeaderTextButtons(
                // TRN Header of the Prusa Account retry notification.
                _u8L("Prusa Account"),
                text,
                // TRN Button on the Prusa Account retry notification. Cancels the retried request.
                {{_u8L("Cancel"),
                  [cancel_callback]()
                  {
                      cancel_callback();
                      return true;
                  }}}
            )
        },
        never_equal_matcher
    );
}

void PopNotificationCenter::on_user_account_action_retry_finished()
{
    m_notification_list.close_notifications_of_type(PopNotificationType::UserAccountTransientError);
}

void PopNotificationCenter::on_project_load_failed(const std::string& error)
{
    upsert_notification(
        PopNotificationData{
            PopNotificationType::LoadError,
            PopNotificationLevel::Warning,
            0s,
            PopNotificationLayoutText{error}
        },
        never_equal_matcher
    );
}

void PopNotificationCenter::on_project_will_be_removed(const Domain::SelectionId project_id)
{
    m_notification_list.erase_notification_by_predicate(
        [project_id](const PopNotificationData& notification)
        { return notification.project_id == project_id; }
    );
}

std::optional<int>
get_instance_index(const Domain::ModelObject& model_object, const Domain::SelectionId instance_id)
{
    for (int index = 0; index < int(model_object.instances.size()); ++index) {
        const Domain::ModelInstance* model_instance{model_object.instances[index]};
        ASSERT(model_instance);
        if (model_instance->id().id == instance_id) {
            return index;
        }
    }
    return std::nullopt;
}

void PopNotificationCenter::on_elements_not_arranged(
    Domain::SelectionId project_id,
    const Domain::ElementRefs& elements
)
{
    std::map<std::string, std::vector<int>> instances;
    for (const Domain::ElementRef& element_ref : elements) {
        ASSERT(element_ref.has_instance());
        const Domain::Project& project{m_project_interactor.workbench().project(project_id)};
        const Domain::ModelObject* object{project.find_object_by_id(element_ref.object_id)};
        if (!object) {
            continue;
        }
        const std::optional<int> instance_index{
            get_instance_index(*object, element_ref.instance_id)
        };
        if (!instance_index) {
            instances[object->name];
        } else {
            instances[object->name].push_back(*instance_index);
        }
    }

    // TRN Header of the arrange notification listing objects that could not be placed.
    const std::string header{_u8L("Some objects could not be arranged")};

    std::string text;
    for (const auto& [name, instance_indicies] : instances) {
        text += name + "\n";
        if (instance_indicies.size() > 1) {
            for (int instance_index : instance_indicies) {
                // TRN Arrange notification, label of an object instance followed by its number.
                text += "  "+_u8L("Instance")+ " " + std::to_string(instance_index + 1) + "\n";
            }
        }
    }
    ASSERT(!text.empty());
    ASSERT(text.back() == '\n');
    text.pop_back();

    upsert_notification(
        PopNotificationData{
            PopNotificationType::ArrangeEvent,
            PopNotificationLevel::Warning,
            0s,
            PopNotificationLayoutHeaderText{header, text},
            ArrangeEventType::ElementsNotArranged,
            project_id,
        },
        [](const PopNotificationPayload& a, const PopNotificationPayload& b)
        {
            const auto a_event_type{std::get_if<ArrangeEventType>(&a)};
            const auto b_event_type{std::get_if<ArrangeEventType>(&b)};
            if (a_event_type == nullptr || b_event_type == nullptr) {
                return false;
            }
            return *a_event_type == *b_event_type;
        }
    );
}

void PopNotificationCenter::on_fatal_arrange_error(
    Domain::SelectionId project_id
)
{
    // TRN Header of the fatal arrange error notification.
    const std::string header{_u8L("Fatal arrange error")};
    const std::string text{
        // TRN Body of the fatal arrange error notification.
        _u8L("Arrange failed, this is usually caused by an object being larger than slicer limits.")
    };

    upsert_notification(
        PopNotificationData{
            PopNotificationType::ArrangeEvent,
            PopNotificationLevel::Error,
            0s,
            PopNotificationLayoutHeaderText{header, text},
            ArrangeEventType::FatalError,
            project_id,
        },
        [](const PopNotificationPayload& a, const PopNotificationPayload& b)
        {
            const auto a_event_type{std::get_if<ArrangeEventType>(&a)};
            const auto b_event_type{std::get_if<ArrangeEventType>(&b)};
            if (a_event_type == nullptr || b_event_type == nullptr) {
                return false;
            }
            return *a_event_type == *b_event_type;
        }
    );
}

void PopNotificationCenter::on_connect_handler_error(const std::string& msg)
{
    // TRN Header of the Prusa Connect error notification.
    const std::string header{_u8L("Prusa Connect Error")};

    upsert_notification(
        PopNotificationData{
            PopNotificationType::ConnectError,
            PopNotificationLevel::Error,
            300s,
            PopNotificationLayoutHeaderText{header, msg}
        },
        never_equal_matcher
    );
}

void PopNotificationCenter::on_file_explorer_error(
    const boost::filesystem::path& path,
    Platform::FileExplorerError reason
)
{
    // TRN Header of an error notification shown when a folder could not be opened in the system file manager (Explorer, Finder, ...).
    const std::string header{_u8L("Cannot Open Folder")};

    std::string body;
    switch (reason) {
    case Platform::FileExplorerError::EmptyPath:
        // TRN Body of an error notification shown when the application tried to open a folder but no path was given.
        body = _u8L("No folder path was given.");
        break;
    case Platform::FileExplorerError::DoesNotExist:
        // TRN Body of an error notification. {} is replaced by the full path of a folder that does not exist.
        body = fmt::format(fmt::runtime(_u8L("The folder does not exist: {}")), path.string());
        break;
    case Platform::FileExplorerError::NotADirectory:
        // TRN Body of an error notification. {} is replaced by a full path that points to a file, not to a folder.
        body = fmt::format(fmt::runtime(_u8L("This path is not a folder: {}")), path.string());
        break;
    case Platform::FileExplorerError::CheckFailed:
        // TRN Body of an error notification. {} is replaced by the full path of a folder that could not be checked, for example because it is on a network drive that cannot be reached.
        body = fmt::format(fmt::runtime(_u8L("The folder could not be accessed: {}")), path.string());
        break;
    case Platform::FileExplorerError::LaunchFailed:
        // TRN Body of an error notification. {} is replaced by the full path of a folder that the system file manager refused to open.
        body = fmt::format(
            fmt::runtime(_u8L("Failed to open the folder in the file manager: {}")), path.string()
        );
        break;
    default:
        ASSERT(false, "Missing FileExplorerError handling.");
        return;
    }

    upsert_notification(
        PopNotificationData{
            PopNotificationType::FileExplorerError,
            PopNotificationLevel::Error,
            60s,
            PopNotificationLayoutHeaderText{header, body}
        },
        never_equal_matcher
    );
}

void PopNotificationCenter::on_plugin_installation_error(const std::string& error_message)
{
    m_notification_list.close_notifications_of_type(PopNotificationType::PluginInstallationError);
    m_notification_list.close_notifications_of_type(PopNotificationType::PluginInstallationSuccess);

    const std::string header{_u8L("Plugin installation error")};
    upsert_notification(
        PopNotificationData{
            PopNotificationType::PluginInstallationError,
            PopNotificationLevel::Error,
            0s,
            PopNotificationLayoutHeaderText{header, error_message}
        },
        never_equal_matcher
    );

}

void PopNotificationCenter::on_plugin_installation_succeeded(
    const Lua::PluginBundleMeta& plugin_bundle_meta
)
{
    m_notification_list.close_notifications_of_type(PopNotificationType::PluginInstallationSuccess);
    m_notification_list.close_notifications_of_type(PopNotificationType::PluginInstallationError);

    const std::string header{_u8L("Plugin installation")};
    upsert_notification(
        PopNotificationData{
            PopNotificationType::PluginInstallationSuccess,
            PopNotificationLevel::Regular,
            5s,
            PopNotificationLayoutHeaderText{
                header,
                fmt::format(
                    // TRN {} is a plugin identifier
                    fmt::runtime(_u8L("Plugin {} installed successfully")),
                    plugin_bundle_meta.id
                )
            }
        },
        never_equal_matcher
    );

}

} // namespace Slic3r::App::PopNotification
