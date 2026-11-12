#include "Slic3r/Biz/WX/SecretStore.hpp"

#include <Slic3r/App/WX/StringConversions.hpp>
#include "Slic3r/Log.hpp"

#include "Slic3r/LegacyFormat.hpp"

#include <wx/secretstore.h>

namespace Slic3r::Biz::WX {

bool SecretStore::save_secret(const std::string& opt, const std::string& usr, const std::string& psswd)
{ 
    wxSecretStore store = wxSecretStore::GetDefault();
    wxString errmsg;
    if (!store.IsOk(&errmsg)) {
        std::string msg = format("This system doesn't support storing passwords securely (%1%).", errmsg);
        SPDLOG_ERROR(msg);
        return false;
    }
    const wxString service = App::WX::from_u8(opt);
    const wxString username = App::WX::from_u8(usr);
    const wxSecretValue password(App::WX::from_u8(psswd));
    if (!store.Save(service, username, password)) {
        SPDLOG_ERROR("Failed to save secret to the system password store.");
        return false;
    }
    return true;
}

bool SecretStore::load_secret(const std::string& opt, std::string& usr, std::string& psswd)
{
    wxSecretStore store = wxSecretStore::GetDefault();
    wxString errmsg;
    if (!store.IsOk(&errmsg)) {
        std::string msg = format("This system doesn't support storing passwords securely (%1%).", errmsg);
        SPDLOG_ERROR(msg);
        return false;
    }
    const wxString service = App::WX::from_u8(opt);
    wxString username;
    wxSecretValue password;
    if (!store.Load(service, username, password)) {
        SPDLOG_ERROR("Failed to load credentials from the system password store.");
        return false;
    }
    usr = App::WX::into_u8(username);
    psswd = App::WX::into_u8(password.GetAsString());
    return true;
}

}
