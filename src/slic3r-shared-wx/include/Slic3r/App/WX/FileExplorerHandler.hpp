#pragma once

#include "Slic3r/App/Platform/IFileExplorerHandler.hpp"
#include "Slic3r/Directories.hpp"

namespace Slic3r::App::WX {

class FileExplorerHandler : public Platform::IFileExplorerHandler
{
public:
    void open_folder(const boost::filesystem::path& path) override;

    void open_datadir_folder() override
    {
        open_folder(boost::filesystem::path(data_dir()));
    }

private:
    /**
     * @brief Hands the folder over to the system file manager.
     * @param path An existing directory, already validated by open_folder().
     * @return False when the file manager could not be launched.
     */
    static bool launch_file_manager(const boost::filesystem::path& path);
};
} // namespace Slic3r::App::WX
