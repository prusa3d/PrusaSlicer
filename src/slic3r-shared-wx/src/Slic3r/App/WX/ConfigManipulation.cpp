#include "Slic3r/App/WX/ConfigManipulation.hpp"
#include "Slic3r/App/WX/StringConversions.hpp"
#include "Slic3r/App/WX/MsgDialog.hpp"

#include <wx/window.h>
#include <wx/string.h>

namespace Slic3r::App::WX {

void ConfigManipulation::warn_user(const std::string& title, const std::string& message)
{
    MessageDialog dialog(m_msg_dlg_parent, from_u8(message), from_u8(title), wxICON_WARNING | wxOK);
    dialog.ShowModal();
}

bool ConfigManipulation::ask_user(const std::string& title, const std::string& question)
{
    MessageDialog dialog(m_msg_dlg_parent, from_u8(question), from_u8(title), wxICON_WARNING | wxYES | wxNO);

    auto answer = dialog.ShowModal();
    return answer == wxID_YES;
}

}