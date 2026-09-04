#include <Slic3r/App/WX/FileExplorerHandler.hpp>

#include <wx/process.h>
#include <wx/utils.h>
#include <wx/string.h>

#include <string>

namespace Slic3r::App::WX {

bool FileExplorerHandler::launch_file_manager(const boost::filesystem::path& path)
{
    const std::string path_str = path.string();
    const char* argv[]         = {"xdg-open", path_str.c_str(), nullptr};
    return ::wxExecute(argv, wxEXEC_ASYNC, nullptr, nullptr) != 0;
}

} // namespace Slic3r::App::WX
