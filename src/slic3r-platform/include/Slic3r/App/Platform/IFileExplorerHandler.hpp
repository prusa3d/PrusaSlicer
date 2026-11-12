#pragma once

#include "Slic3r/App/Platform/IFileExplorerErrorListener.hpp"
#include "Slic3r/Biz/Platform/WithListeners.hpp"

#include <string>
#include <boost/filesystem/path.hpp>

namespace Slic3r::App::Platform {

class IFileExplorerHandler : public WithListeners<IFileExplorerErrorListener>
{
public:
    IFileExplorerHandler()          = default;
    virtual ~IFileExplorerHandler() = default;

    virtual void open_folder(const boost::filesystem::path& path) = 0;
    virtual void open_datadir_folder()                            = 0;
};
} // namespace Slic3r::App::Platform
