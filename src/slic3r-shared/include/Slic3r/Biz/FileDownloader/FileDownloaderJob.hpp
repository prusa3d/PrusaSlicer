#pragma once

#include "Slic3r/Biz/FileDownloader/FileDownloaderJobData.hpp"
#include "Slic3r/Biz/Platform/JobManager/ProgressTracker.hpp"
#include "Slic3r/Biz/Platform/IMainThreadDispatcher.hpp"

#include <jthread/JThread.hpp>

namespace Slic3r::Biz::FileDownloader {

struct FileDownloadFailed : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

FileDownloaderJobData perform_job(
    Biz::JThread::StopToken stop_token,
    Biz::Platform::IMainThreadDispatcher& dis,
    Biz::Platform::JobManager::ProgressTracker progress,
    FileDownloaderJobData job_data
);

} // namespace Slic3r::Biz::FileDownloader
