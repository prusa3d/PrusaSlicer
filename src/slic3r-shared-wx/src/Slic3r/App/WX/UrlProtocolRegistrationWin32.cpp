#include "Slic3r/App/WX/UrlProtocolRegistration.hpp"

#include "Slic3r/App/WX/StringConversions.hpp"
#include "Slic3r/Log.hpp"

#include <boost/dll/runtime_symbol_info.hpp>
#include <boost/filesystem.hpp>
#include <wx/msw/registry.h>

namespace Slic3r::App::WX {

void register_prusaslicer_url()
{
    boost::filesystem::path binary_path(boost::filesystem::canonical(boost::dll::program_location()));
    wxString command = L"\"" + from_u8(binary_path.string()) + L"\" \"--single-instance\" \"%1\"";

    wxRegKey key_scheme(wxRegKey::HKCU, L"Software\\Classes\\prusaslicer");
    wxRegKey key_command(wxRegKey::HKCU, L"Software\\Classes\\prusaslicer\\shell\\open\\command");

    wxString current_command;
    if (key_command.Exists() && key_command.HasValue(L"") && key_command.QueryValue(L"", current_command)
        && current_command == command)
    {
        return;
    }

    SPDLOG_INFO("Downloader registration: Path of binary: {}", into_u8(command));
    if (!key_scheme.Exists()) {
        key_scheme.Create(false);
    }
    key_scheme.SetValue(L"URL Protocol", L"");
    if (!key_command.Exists()) {
        key_command.Create(false);
    }
    if (!key_command.SetValue(L"", command)) {
        SPDLOG_ERROR("Failed to write registry value.");
    }
}

} // namespace Slic3r::App::WX
