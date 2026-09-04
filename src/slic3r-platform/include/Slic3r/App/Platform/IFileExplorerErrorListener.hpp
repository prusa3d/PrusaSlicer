#pragma once

#include <boost/filesystem/path.hpp>

namespace Slic3r::App::Platform {

enum class FileExplorerError
{
    EmptyPath,
    DoesNotExist,
    NotADirectory,
    CheckFailed,
    LaunchFailed,
};

class IFileExplorerErrorListener
{
public:
    virtual ~IFileExplorerErrorListener() = default;
    virtual void on_file_explorer_error(const boost::filesystem::path& path, FileExplorerError reason) {}
};
} // namespace Slic3r::App::Platform
