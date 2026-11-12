#pragma once

#include "Slic3r/Biz/Platform/JobManager/ProgressTracker.hpp"
#include "Slic3r/Biz/RemovableDrive/IRemovableDriveStatusListener.hpp"
#include "Slic3r/Biz/Slicing/BackgroundProcess.hpp"

#include <variant>
#include <boost/filesystem/path.hpp>

namespace Slic3r::App::PopNotification {

struct JobProgressNotificationData
{
    std::string job_name;
    Biz::Platform::JobManager::Progress progress;
};

struct SlicingStatusNotificationData
{
    Domain::SlicingId slicing_id;
};

struct SlicingErrorNotificationData
{
    Biz::Slicing::ErrorCode error_code;
    Domain::SlicingId slicing_id;
};

struct SlicingWarningNotificationData
{
    Biz::Slicing::WarningCode warning_code;
    Domain::SlicingId slicing_id;
};

struct DownloadProgressNotificationData
{
    size_t download_id;
};

enum class PrintHostJobStatus
{
    None,
    Started,
    Finished,
    Failed
};

struct PrintHostProgressNotificationData
{
    size_t print_host_id;
    PrintHostJobStatus status{PrintHostJobStatus::None};
    int progress{0};
    std::string filename;
    std::string target;
    bool is_upload{true};
    bool is_storage_resolve{false};
    std::string additional_msg;
    std::function<void(const boost::filesystem::path&)> eject_fn{nullptr};
};

struct EjectNotificationData
{
    boost::filesystem::path drive_path;
    Biz::RemovableDrive::RemovableDriveStatus status;
};

enum class ArrangeEventType {
    None,
    ElementsNotArranged,
    FatalError
};

struct UserAccountLoginNotificationData {};

// Define the variant type alias.
using PopNotificationPayload = std::variant<
    std::monostate,
    JobProgressNotificationData,
    SlicingStatusNotificationData,
    SlicingErrorNotificationData,
    SlicingWarningNotificationData,
    PrintHostProgressNotificationData,
    EjectNotificationData,
    DownloadProgressNotificationData,
    ArrangeEventType,
    UserAccountLoginNotificationData
>;

} // namespace Slic3r::App::PopNotification
