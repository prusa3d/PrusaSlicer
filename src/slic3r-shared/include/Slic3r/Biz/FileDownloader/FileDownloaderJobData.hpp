#pragma once

#include <boost/filesystem/path.hpp>
#include <string>

namespace Slic3r::Biz::FileDownloader {

struct FileDownloaderJobProgressPayload
{
    size_t download_id;
    std::string filename;
    std::string project_url;
    size_t load_count;
    boost::filesystem::path final_path;
    size_t total_files;
};

struct FileDownloaderJobInput
{
    std::string file_name;
    std::string file_url;
    std::string project_name;
    std::string project_url;
    std::string image_url;
    size_t load_count {0};
    
    FileDownloaderJobInput() = default; 
    ~FileDownloaderJobInput() = default;
    FileDownloaderJobInput(const FileDownloaderJobInput& other) = default; 
    FileDownloaderJobInput& operator=(const FileDownloaderJobInput& other) = default;
    FileDownloaderJobInput(FileDownloaderJobInput&& other) = default;
    FileDownloaderJobInput& operator=(FileDownloaderJobInput&& other) = default;
};

struct FileDownloaderMultiTicket
{
    std::vector<FileDownloaderJobInput> jobs;
    bool new_project {false};
};

struct FileDownloaderJobData
{
    // Input
    FileDownloaderJobInput input_data;
    const size_t id;
    boost::filesystem::path dest_folder;
    double percent_from;
    double percent_to;
    size_t total_files;
    // Runtime
    boost::filesystem::path tmp_path;
    size_t written{0};
    size_t absolute_size{0};
    bool stopped{false};
    bool paused{false};
    bool finished{false};
};

struct MultiFileDownloaderJobData
{
    std::vector<FileDownloaderJobData> data;
    bool new_project;
};

} // namespace Slic3r::Biz::FileDownloader