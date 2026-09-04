#include "Slic3r/Biz/FileDownloader/FileDownloaderJob.hpp"

#include "Slic3r/Biz/AppInstance/AppInstanceUtils.hpp"
#include "Slic3r/Biz/Network/IHttp.hpp"

#include <boost/filesystem.hpp>
#include <boost/system/error_code.hpp>
#include <boost/nowide/fstream.hpp>
#include <boost/nowide/cstdio.hpp>
#include <limits>
#include <regex>

namespace fs = boost::filesystem;

namespace Slic3r::Biz::FileDownloader {

const size_t DOWNLOAD_MAX_CHUNK_SIZE = 10 * 1'024 * 1'024;
const size_t DOWNLOAD_SIZE_LIMIT     = 1'024 * 1'024 * 1'024;

namespace {
std::string extract_filename_from_header(const std::string& headers) {
    // Split the headers into lines
    std::istringstream header_stream(headers);
    std::string line;

    while (std::getline(header_stream, line)) {
        if (line.find("content-disposition") != std::string::npos || line.find("Content-Disposition") != std::string::npos) {
            // Apply regex to extract filename from the content-disposition line
            std::regex filename_regex("filename\\s*=\\s*\"([^\"]+)\"", std::regex::icase);
            std::smatch match;

            if (std::regex_search(line, match, filename_regex) && match.size() > 1) {
                return match.str(1);
            }
        }
    }

    return {};
}
}

FileDownloaderJobData perform_job(
    Biz::JThread::StopToken stop_token,
    Biz::Platform::IMainThreadDispatcher& dis,
    Biz::Platform::JobManager::ProgressTracker job_progress,
    FileDownloaderJobData job_data
)
{
    boost::system::error_code ec;
    ASSERT(fs::is_directory(job_data.dest_folder, ec) && !ec);
    fs::path dest_path    = job_data.dest_folder / job_data.input_data.file_name;
    std::string extension = dest_path.extension().string();
    std::string just_filename =
        job_data.input_data.file_name.substr(0, job_data.input_data.file_name.size() - extension.size());
    std::string final_filename = just_filename;

    // Find unused filename
    try {
        size_t version                = 0;
        while ((boost::filesystem::exists(job_data.dest_folder / (final_filename + extension), ec)
                && !ec)
               || (boost::filesystem::exists(
                       job_data.dest_folder
                           / (final_filename
                              + extension
                              + "."
                              + std::to_string(AppInstance::get_current_pid())
                              + ".download"),
                       ec
                   )
                   && !ec))
        {
            ++version;
            if (version == std::numeric_limits<size_t>::max()) {
                throw FileDownloadFailed{""};
            }
            final_filename = fmt::format("{}({})", just_filename, std::to_string(version));
        }
    } catch (const boost::filesystem::filesystem_error&) {
        throw FileDownloadFailed{""};
    }

    job_data.input_data.file_name = final_filename + extension;
    job_data.tmp_path = job_data.dest_folder
        / (job_data.input_data.file_name + "." + std::to_string(AppInstance::get_current_pid()) + ".download");

    dest_path = job_data.dest_folder / job_data.input_data.file_name;

    SPDLOG_INFO(
        "Starting download from {} to {}. Temp path is {}",
        job_data.input_data.file_url,
        dest_path.string(),
        job_data.tmp_path.string()
    );

    Domain::ProgressDetail detail;
    FileDownloaderJobProgressPayload payload(job_data.id, job_data.input_data.file_name, job_data.input_data.project_url, job_data.input_data.load_count, dest_path, job_data.total_files);
    detail.payload = std::move(payload);
    job_progress.set_progress_detail(detail);

    FILE* file;
    // open file for writting
    if (job_data.written == 0) {
        file = boost::nowide::fopen(job_data.tmp_path.string().c_str(), "wb");
    } else {
        file = boost::nowide::fopen(job_data.tmp_path.string().c_str(), "ab");
    }

    if (file == NULL) {
        throw FileDownloadFailed{""};
    }

    auto retry_fn = [](Network::IHttp::Retry retry, bool& cancel)
    {
        SPDLOG_INFO(
            "Downloader retry attempt {}:{} ms to next attempt",
            retry.attempt,
            retry.ms_to_next_attempt
        );
    };

    std::string range_string    = std::to_string(job_data.written) + "-";
    size_t written_previously   = job_data.written;
    size_t written_this_session = 0;
    bool success                = false;
    Network::IHttp::create(Network::IHttp::RequestMethod::Get, job_data.input_data.file_url, retry_fn)
        ->size_limit(DOWNLOAD_SIZE_LIMIT) // more?
        .set_range(range_string)
        .on_progress(
            [&](Network::IHttp::Progress progress, bool& cancel)
            {
                // to prevent multiple calls into following ifs (m_cancel / m_pause)
                if (job_data.stopped) {
                    cancel = true;
                    return;
                }
                if (stop_token.stop_requested()) {
                    job_data.stopped = true;
                    fclose(file);
                    // remove canceled file
                    std::remove(job_data.tmp_path.string().c_str());
                    job_data.written = 0;
                    cancel           = true;
                    return;
                }

                if (job_data.absolute_size < progress.dltotal) {
                    job_data.absolute_size = progress.dltotal;
                }

                if (progress.dlnow != 0) {
                    if (progress.dlnow - written_this_session > DOWNLOAD_MAX_CHUNK_SIZE
                        || progress.dlnow == progress.dltotal)
                    {
                        try {
                            std::string part_for_write =
                                progress.buffer.substr(written_this_session, progress.dlnow);
                            fwrite(part_for_write.c_str(), 1, part_for_write.size(), file);
                        } catch (const std::exception&) {
                            // TODO: something went wrong - dispatch error
                            cancel = true;
                            return;
                        }
                        written_this_session = progress.dlnow;
                        job_data.written     = written_previously + written_this_session;
                    }
                    double percent_total = job_data.absolute_size == 0 ?
                        0.0 :
                        (written_previously + progress.dlnow) / job_data.absolute_size;
                    double aux = percent_total;
                    percent_total = job_data.percent_from + percent_total * (job_data.percent_to - job_data.percent_from);
                    //SPDLOG_DEBUG("percent: {} ({} + {} * {})", percent_total, job_data.percent_from ,aux, (job_data.percent_to - job_data.percent_from));
                    job_progress.set({percent_total});
                }
            }
        )
         .on_headers_read(
             [&](const std::string& headers)
             {
                 // we are looking for content-disposition header in response, to use it as correct filename
                 std::string new_filename = extract_filename_from_header(headers);
                 if (new_filename.empty()) {
                     return;
                 }
                 new_filename = Network::IHttp::unescape_string(new_filename);
                 // Find unused filename
                 boost::filesystem::path temp_dest_path = job_data.dest_folder / new_filename;
                 std::string extension                  = temp_dest_path.extension().string();
                 std::string just_filename =
                     new_filename.substr(0, new_filename.size() - extension.size());
                 std::string final_filename = just_filename;
                 try {
                     size_t version = 0;
                     while (boost::filesystem::exists(job_data.dest_folder / (final_filename + extension)))
                     {
                         ++version;
                         if (version == std::numeric_limits<size_t>::max()) {
                             SPDLOG_ERROR(
                                 "Failed to find suitable filename. Last name: {}.",
                                 (job_data.dest_folder / (final_filename + extension)).string()
                             );
                             return;
                         }
                         final_filename =
                             fmt::format("{}({})", just_filename, std::to_string(version));
                     }
                 } catch (const boost::filesystem::filesystem_error&) {
                     SPDLOG_ERROR("Failed to resolved filename from headers.");
                     return;
                 }
                 job_data.input_data.file_name = final_filename + extension;
                 dest_path  = job_data.dest_folder / job_data.input_data.file_name;

                 Domain::ProgressDetail detail;
                 FileDownloaderJobProgressPayload payload(job_data.id, job_data.input_data.file_name, job_data.input_data.project_url, job_data.input_data.load_count, dest_path, job_data.total_files);
                 detail.payload = std::move(payload);
                 job_progress.set_progress_detail(detail);
             }
         )
        .on_error(
            [&](std::string body, std::string error, unsigned http_status)
            {
                SPDLOG_ERROR("File Download has failed. body: {}, error: {}, status: {}", body, error, http_status);
                if (file != NULL)
                    fclose(file);
            }
        )
        .on_complete(
            [&](std::string body, unsigned /* http_status */)
            {
                try {
                    // If server is not sending Content-Length header, the progress function does not write all data to file.
                    // We need to write it now.
                    if (written_this_session < body.size()) {
                        std::string part_for_write = body.substr(written_this_session);
                        fwrite(part_for_write.c_str(), 1, part_for_write.size(), file);
                    }
                    fclose(file);
                    boost::filesystem::rename(job_data.tmp_path, dest_path);
                } catch (const std::exception& e) {
                    SPDLOG_ERROR("File Download has failed due to exception at on_complete callback: {}",e.what());
                    return;
                }
                double percent_total = job_data.percent_to;
                job_progress.set({percent_total});
                job_data.finished = true;
                success = true;
            }
        )
        .perform_sync();
    if (!success) {
        throw FileDownloadFailed{""};
    }
    return std::move(job_data);
}

} // namespace Slic3r::Biz::FileDownloader
