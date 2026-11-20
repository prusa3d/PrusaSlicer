#pragma once

#include <string>
#include <utility>

namespace Slic3r::App::Browser {

enum class BrowserLogicCommandType
{
    None,
    LoadURL,
    LoadRequest,
    RunScript,
    EndModalOK,
    EndModalCancel,
    DeleteCookies,
    DeleteCookiesWithCounter,
    Veto,
    DoReload,
    AddUserScript,
    LoadResourcesPage,
    OpenExternalBrowser,
    OpenExternalBrowserForced,
    RegisterPrusaSlicerURL,
    SetLoadDefaultURLOnErrorTrue,
    SetLoadDefaultURLOnErrorFalse,
    SwitchToSlicing,
    ClearBasicAuth,
    SetBasicAuth,
    /** @brief Deletes one cookie, data is cookie_command_data(). */
    DeleteCookie,
    /** @brief Sets one cookie, data is cookie_set_command_data() (BrowserCookie.hpp). */
    SetCookie
};

/** @brief Data of BrowserLogicCommandType::DeleteCookie: the cookie name and the server URL. */
inline std::string cookie_command_data(const std::string& name, const std::string& url)
{
    return name + "\n" + url;
}

inline std::pair<std::string, std::string> parse_cookie_command_data(const std::string& data)
{
    const size_t pos = data.find('\n');
    if (pos == std::string::npos) {
        return {};
    }
    return {data.substr(0, pos), data.substr(pos + 1)};
}

struct BrowserLogicCommand
{
    BrowserLogicCommandType type;
    std::string data;

    BrowserLogicCommand(BrowserLogicCommandType t, const std::string& d)
        : type(t), data(d) {}

    BrowserLogicCommand(BrowserLogicCommandType t, std::string&& d)
        : type(t), data(std::move(d)) {}
};
}