///|/ Copyright (c) Prusa Research 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "SecretStore.hpp"

#include <boost/log/trivial.hpp>
#include <boost/nowide/convert.hpp>

#if wxUSE_SECRETSTORE
#include <wx/secretstore.h>
#endif

#include "libslic3r/libslic3r.h"
#include "libslic3r/PrintConfig.hpp"

#include "GUI.hpp"
#include "I18N.hpp"
#include "format.hpp"
#include "MsgDialog.hpp"

namespace Slic3r {
namespace GUI {
namespace SecretStore {

bool is_supported(wxString* errmsg)
{
#if wxUSE_SECRETSTORE
    wxSecretStore store = wxSecretStore::GetDefault();
    wxString local_errmsg;
    wxString* err_ptr = errmsg ? errmsg : &local_errmsg;
    if (!store.IsOk(err_ptr)) {
        BOOST_LOG_TRIVIAL(warning)
            << "wxSecretStore is not supported: " << *err_ptr;
        return false;
    }
    return true;
#else
    if (errmsg) {
        *errmsg = "wxUSE_SECRETSTORE not supported";
    }
    return false;
#endif
}

bool load_secret(const std::string& service_prefix,
                 const std::string& id,
                 const std::string& opt,
                 std::string& usr,
                 std::string& psswd)
{
    wxString errmsg;
    if (!is_supported(&errmsg)) {
        std::string msg = GUI::format(
            "%1% (%2%).",
            _u8L("This system doesn't support storing passwords "
                 "securely"),
            errmsg);
        BOOST_LOG_TRIVIAL(error) << msg;
        show_error(nullptr, msg);
        return false;
    }

#if wxUSE_SECRETSTORE
    // Build service name: "AppName/ServicePrefix/id/opt" or
    // "AppName/ServicePrefix/opt"
    wxString service;
    if (id.empty()) {
        service = GUI::format_wxstr(L"%1%/%2%/%3%",
                                    SLIC3R_APP_NAME,
                                    service_prefix,
                                    opt);
    } else {
        service = GUI::format_wxstr(L"%1%/%2%/%3%/%4%",
                                    SLIC3R_APP_NAME,
                                    service_prefix,
                                    id,
                                    opt);
    }

    wxSecretStore store = wxSecretStore::GetDefault();
    wxString username;
    wxSecretValue password;
    if (!store.Load(service, username, password)) {
        std::string msg(_u8L("Failed to load credentials from the "
                             "system password store."));
        BOOST_LOG_TRIVIAL(error) << msg;
        show_error(nullptr, msg);
        return false;
    }

    usr = into_u8(username);
    psswd = into_u8(password.GetAsString());
    return true;
#else
    return false;
#endif // wxUSE_SECRETSTORE
}

bool save_secret(const std::string& service_prefix,
                 const std::string& id,
                 const std::string& opt,
                 const std::string& usr,
                 const std::string& psswd)
{
    wxString errmsg;
    if (!is_supported(&errmsg)) {
        std::string msg = GUI::format(
            "%1% (%2%).",
            _u8L("This system doesn't support storing passwords "
                 "securely"),
            errmsg);
        BOOST_LOG_TRIVIAL(error) << msg;
        show_error(nullptr, msg);
        return false;
    }

#if wxUSE_SECRETSTORE
    // Build service name: "AppName/ServicePrefix/id/opt" or
    // "AppName/ServicePrefix/opt"
    wxString service;
    if (id.empty()) {
        service = GUI::format_wxstr(L"%1%/%2%/%3%",
                                    SLIC3R_APP_NAME,
                                    service_prefix,
                                    opt);
    } else {
        service = GUI::format_wxstr(L"%1%/%2%/%3%/%4%",
                                    SLIC3R_APP_NAME,
                                    service_prefix,
                                    id,
                                    opt);
    }

    wxSecretStore store = wxSecretStore::GetDefault();
    const wxString username = boost::nowide::widen(usr);
    const wxSecretValue password(boost::nowide::widen(psswd));
    if (!store.Save(service, username, password)) {
        std::string msg(_u8L("Failed to save credentials to the "
                             "system password store."));
        BOOST_LOG_TRIVIAL(error) << msg;
        show_error(nullptr, msg);
        return false;
    }

    return true;
#else
    return false;
#endif // wxUSE_SECRETSTORE
}

void load_printer_credentials(const std::string& printer_name,
                               DynamicPrintConfig* config)
{
    if (!config) {
        BOOST_LOG_TRIVIAL(error)
            << "load_printer_credentials: config is null";
        return;
    }

    // Load user/password if marked as "stored"
    if (config->opt_string("printhost_user") == "stored" &&
        config->opt_string("printhost_password") == "stored") {
        std::string user;
        std::string password;
        if (load_secret("PhysicalPrinter",
                        printer_name,
                        "printhost_password",
                        user,
                        password)) {
            if (!user.empty() && !password.empty()) {
                config->opt_string("printhost_user") = user;
                config->opt_string("printhost_password") = password;
            } else {
                config->opt_string("printhost_user") = std::string();
                config->opt_string("printhost_password") =
                    std::string();
            }
        } else {
            config->opt_string("printhost_user") = std::string();
            config->opt_string("printhost_password") = std::string();
        }
    }

    // Load apikey if marked as "stored"
    if (config->opt_string("printhost_apikey") == "stored") {
        std::string dummy;
        std::string apikey;
        if (load_secret("PhysicalPrinter",
                        printer_name,
                        "printhost_apikey",
                        dummy,
                        apikey) &&
            !apikey.empty()) {
            config->opt_string("printhost_apikey") = apikey;
        } else {
            config->opt_string("printhost_apikey") = std::string();
        }
    }
}

void save_printer_credentials(const std::string& printer_name,
                               DynamicPrintConfig* config)
{
    if (!config) {
        BOOST_LOG_TRIVIAL(error)
            << "save_printer_credentials: config is null";
        return;
    }

    // Save user/password if not empty
    const std::string& user = config->opt_string("printhost_user");
    const std::string& password =
        config->opt_string("printhost_password");
    if (!user.empty() && !password.empty()) {
        if (save_secret("PhysicalPrinter",
                        printer_name,
                        "printhost_password",
                        user,
                        password)) {
            config->opt_string("printhost_user", false) = "stored";
            config->opt_string("printhost_password", false) =
                "stored";
        }
    }

    // Save apikey if not empty
    const std::string& apikey = config->opt_string("printhost_apikey");
    if (!apikey.empty()) {
        if (save_secret("PhysicalPrinter",
                        printer_name,
                        "printhost_apikey",
                        "apikey",
                        apikey)) {
            config->opt_string("printhost_apikey", false) = "stored";
        }
    }
}

} // namespace SecretStore
} // namespace GUI
} // namespace Slic3r
