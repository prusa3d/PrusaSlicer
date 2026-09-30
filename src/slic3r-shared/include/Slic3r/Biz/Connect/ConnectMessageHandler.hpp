#pragma once

#include "Slic3r/Biz/Platform/IMainThreadDispatcher.hpp"
#include "Slic3r/Biz/Platform/WithListeners.hpp"
#include "Slic3r/Biz/Connect/IConnectHandlerListener.hpp"
#include "Slic3r/Biz/Network/IHttp.hpp"
#include "Slic3r/Log.hpp"

#include <string>
#include <nlohmann/json_fwd.hpp>

namespace Slic3r::Biz::Preset {
class PresetInteractor;
} // namespace Slic3r::Biz::Preset

namespace Slic3r::Biz::PhysicalPrinter {
class PhysicalPrinterInteractor;
} // namespace Slic3r::Biz::PhysicalPrinter

namespace Slic3r::Biz::ConnectPrinter {
class ConnectPrinterService;
} // namespace Slic3r::Biz::ConnectPrinter

namespace Slic3r::Biz::Connect {

class ConnectMessageHandler : public WithListeners<IConnectHandlerListener>
{
public:
    ConnectMessageHandler(
        Platform::IMainThreadDispatcher& dispatcher,
        Preset::PresetInteractor& preset_interactor,
        ConnectPrinter::ConnectPrinterService& connect_printer_service,
        PhysicalPrinter::PhysicalPrinterInteractor& physical_printer_interactor
    );

    void handle_select_printer_message(const std::string& message_json);

    std::string uuid_for_upload();

private:
    void do_select_printer_from_connect(const std::string& uuid, const std::string& printer_json);

    void select_printer_tools_from_connect(const nlohmann::json& j);

    void select_printer_materials_from_connect(const nlohmann::json& j);

    void dispatch_error(std::string msg);

private:
    Platform::IMainThreadDispatcher& m_dispatcher;
    Preset::PresetInteractor& m_preset_interactor;
    ConnectPrinter::ConnectPrinterService& m_connect_printer_service;
    PhysicalPrinter::PhysicalPrinterInteractor& m_physical_printer_interactor;

    std::string m_last_printer_json;
};
} // namespace Slic3r::Biz::Connect
