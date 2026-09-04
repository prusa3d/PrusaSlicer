#include <Slic3r/App/WX/FileExplorerHandler.hpp>

#include <wx/process.h>
#include <wx/utils.h>
#include <wx/string.h>

namespace Slic3r::App::WX {

bool FileExplorerHandler::launch_file_manager(const boost::filesystem::path& path)
{
    boost::filesystem::path native_path{path};
    native_path.make_preferred();
    const wxString widepath = native_path.wstring();
    const wchar_t* argv[]   = {L"explorer", widepath.GetData(), nullptr};
    return ::wxExecute(argv, wxEXEC_ASYNC, nullptr) != 0;
}

} // namespace Slic3r::App::WX
