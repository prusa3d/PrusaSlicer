#pragma once

#include <string>

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
    RegisterPrusaSlicerURL,
    SetLoadDefaultURLOnErrorTrue,
    SetLoadDefaultURLOnErrorFalse,
    SwitchToSlicing,
    ClearBasicAuth,
    SetBasicAuth
};

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