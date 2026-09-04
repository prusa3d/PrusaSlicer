#pragma once

#include <boost/filesystem/path.hpp>
#include "Slic3r/Biz/FileDownloader/FileDownloaderJobData.hpp"

namespace Slic3r::Biz::FileDownloader {

class IFileDownloaderListener
{
public:
    virtual ~IFileDownloaderListener() = default;

    virtual void on_files_downloaded(const std::vector<boost::filesystem::path>& path, std::shared_ptr<FileDownloaderTag> tag) = 0;
};

} // namespace Slic3r::App::Scene#pragma once