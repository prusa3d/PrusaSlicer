#pragma once

#include "Slic3r/App/Browser/BrowserLogicCommand.hpp"
#include "Slic3r/Assert.hpp"
#include "Slic3r/Log.hpp"

#include <vector>
#include <string>

namespace Slic3r::App::Browser {

class AbstractBrowserLogicCommandHandler
{
public:
    AbstractBrowserLogicCommandHandler() = default;
    virtual ~AbstractBrowserLogicCommandHandler() = default;

protected:
    virtual bool handle_logic_command_LoadURL(const std::string& data) = 0;
    virtual bool handle_logic_command_LoadRequest(const std::string& data) = 0;
    virtual bool handle_logic_command_RunScript(const std::string& data) = 0;
    virtual bool handle_logic_command_EndModalOK(const std::string& data) = 0;
    virtual bool handle_logic_command_EndModalCancel(const std::string& data) = 0;
    virtual bool handle_logic_command_DeleteCookies(const std::string& data) = 0;
    virtual bool handle_logic_command_DeleteCookiesWithCounter(const std::string& data) = 0;
    virtual bool handle_logic_command_Veto(const std::string& data) = 0;
    virtual bool handle_logic_command_DoReload(const std::string& data) = 0;
    virtual bool handle_logic_command_AddUserScript(const std::string& data) = 0;
    virtual bool handle_logic_command_LoadResourcesPage(const std::string& data) = 0;
    virtual bool handle_logic_command_OpenExternalBrowser(const std::string& data) = 0;
    virtual bool handle_logic_command_RegisterPrusaSlicerURL(const std::string& data) = 0;
    virtual bool handle_logic_command_SetLoadDefaultURLOnErrorTrue(const std::string& data) = 0;
    virtual bool handle_logic_command_SetLoadDefaultURLOnErrorFalse(const std::string& data) = 0;
    virtual bool handle_logic_command_SwitchToSlicing(const std::string& data) = 0;
    virtual bool handle_logic_command_SetBasicAuth(const std::string& data) = 0;
    virtual bool handle_logic_command_ClearBasicAuth(const std::string& data) = 0;

    bool process_logic_command_vector(std::vector<BrowserLogicCommand>&& command)
    {
        bool ret_val = true;
        for (const BrowserLogicCommand& cmd : command) {        
            ret_val &= handle_logic_command(cmd);          
        }
        return ret_val;
    }

    bool handle_logic_command(const BrowserLogicCommand& command)
    {
        switch (command.type)
        {
        case BrowserLogicCommandType::None:                          return true;
        case BrowserLogicCommandType::LoadURL:                       return handle_logic_command_LoadURL(command.data);
        case BrowserLogicCommandType::LoadRequest:                   return handle_logic_command_LoadRequest(command.data);
        case BrowserLogicCommandType::RunScript:                     return handle_logic_command_RunScript(command.data);
        case BrowserLogicCommandType::EndModalOK:                    return handle_logic_command_EndModalOK(command.data);
        case BrowserLogicCommandType::EndModalCancel:                return handle_logic_command_EndModalCancel(command.data);
        case BrowserLogicCommandType::DeleteCookies:                 return handle_logic_command_DeleteCookies(command.data);
        case BrowserLogicCommandType::DeleteCookiesWithCounter:      return handle_logic_command_DeleteCookiesWithCounter(command.data);
        case BrowserLogicCommandType::Veto:                          return handle_logic_command_Veto(command.data);
        case BrowserLogicCommandType::DoReload:                      return handle_logic_command_DoReload(command.data);
        case BrowserLogicCommandType::AddUserScript:                 return handle_logic_command_AddUserScript(command.data);
        case BrowserLogicCommandType::LoadResourcesPage:             return handle_logic_command_LoadResourcesPage(command.data);
        case BrowserLogicCommandType::OpenExternalBrowser:           return handle_logic_command_OpenExternalBrowser(command.data);
        case BrowserLogicCommandType::RegisterPrusaSlicerURL:        return handle_logic_command_RegisterPrusaSlicerURL(command.data);
        case BrowserLogicCommandType::SetLoadDefaultURLOnErrorTrue:  return handle_logic_command_SetLoadDefaultURLOnErrorTrue(command.data);
        case BrowserLogicCommandType::SetLoadDefaultURLOnErrorFalse: return handle_logic_command_SetLoadDefaultURLOnErrorFalse(command.data);
        case BrowserLogicCommandType::SwitchToSlicing:               return handle_logic_command_SwitchToSlicing(command.data);
        case BrowserLogicCommandType::SetBasicAuth:                  return handle_logic_command_SetBasicAuth(command.data);
        case BrowserLogicCommandType::ClearBasicAuth:                return handle_logic_command_ClearBasicAuth(command.data);
        }
        DEBUG_ASSERT(false, "Missing BrowserLogicCommand handling");
        return true;
    }
};


}