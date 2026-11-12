#include <Slic3r/App/WX/FileExplorerHandler.hpp>

#include "Slic3r/Log.hpp"

#include <boost/filesystem/operations.hpp>

namespace Slic3r::App::WX {

void FileExplorerHandler::open_folder(const boost::filesystem::path& path)
{
    const auto notify_error = [this, &path](Platform::FileExplorerError reason)
    {
        invoke_listeners<Platform::IFileExplorerErrorListener>(
            [&path, reason](Platform::IFileExplorerErrorListener* listener)
            {
                listener->on_file_explorer_error(path, reason);
            }
        );
    };

    if (path.empty()) {
        SPDLOG_ERROR("Cannot open folder, the path is empty.");
        notify_error(Platform::FileExplorerError::EmptyPath);
        return;
    }

    boost::system::error_code ec;
    const boost::filesystem::file_status status = boost::filesystem::status(path, ec);

    if (status.type() == boost::filesystem::file_not_found) {
        SPDLOG_ERROR("Cannot open folder, it does not exist: {}", path.string());
        notify_error(Platform::FileExplorerError::DoesNotExist);
        return;
    }
    if (ec) {
        SPDLOG_ERROR("Cannot open folder, failed to query {}: {}", path.string(), ec.message());
        notify_error(Platform::FileExplorerError::CheckFailed);
        return;
    }
    if (!boost::filesystem::is_directory(status)) {
        SPDLOG_ERROR("Cannot open folder, not a directory: {}", path.string());
        notify_error(Platform::FileExplorerError::NotADirectory);
        return;
    }

    if (!launch_file_manager(path)) {
        SPDLOG_ERROR("Failed to launch the file manager for folder: {}", path.string());
        notify_error(Platform::FileExplorerError::LaunchFailed);
    }
}

} // namespace Slic3r::App::WX
