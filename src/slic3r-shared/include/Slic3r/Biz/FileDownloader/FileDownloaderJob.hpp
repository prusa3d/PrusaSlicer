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

/**
 * @brief Turns an untrusted name (from URL, HTTP headers or Printables) into a plain file name
 * without any path components. Returns an empty string if no usable name remains.
 */
std::string sanitize_filename(const std::string& filename);

FileDownloaderJobData perform_job(
    Biz::JThread::StopToken stop_token,
    Biz::Platform::IMainThreadDispatcher& dis,
    Biz::Platform::JobManager::ProgressTracker progress,
    FileDownloaderJobData job_data
);

} // namespace Slic3r::Biz::FileDownloader
