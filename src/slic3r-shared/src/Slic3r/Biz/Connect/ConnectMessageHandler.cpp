#include "Slic3r/Biz/Connect/ConnectMessageHandler.hpp"

#include "Slic3r/Biz/PhysicalPrinter/PhysicalPrinterInteractor.hpp"
#include "Slic3r/Biz/ExportDestination/ExportDestinationInteractor.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/ConnectPrinter/ConnectPrinterService.hpp"
#include "Slic3r/Biz/Preset/PrinterPresetSelector.hpp"

#include "Slic3r/Log.hpp"
#include "nlohmann/json.hpp"
#include "jthread/JThread.hpp"

namespace Slic3r::Biz::Connect {

ConnectMessageHandler::ConnectMessageHandler(
        Platform::IMainThreadDispatcher& dispatcher,
        Preset::PresetInteractor& preset_interactor,
        ConnectPrinter::ConnectPrinterService& connect_printer_service,
    PhysicalPrinter::PhysicalPrinterInteractor& physical_printer_interactor,
    ExportDestination::ExportDestinationInteractor& export_destination_interactor
    ) :
    m_dispatcher(dispatcher),
    m_preset_interactor(preset_interactor),
    m_connect_printer_service(connect_printer_service),
    m_physical_printer_interactor(physical_printer_interactor),
    m_export_destination_interactor(export_destination_interactor)
{}

void ConnectMessageHandler::dispatch_error(std::string msg)
{
    m_dispatcher.dispatch_on_main_thread([this, msg = std::move(msg)]() {
        this->invoke_listeners<IConnectHandlerListener>([&msg](auto* listener) {
            listener->on_connect_handler_error(msg);
        });
    });
}

namespace {
std::string extract_printer_json_by_uuid(const std::string& message_json, const std::string& uuid)
{
    try {
        auto j = nlohmann::json::parse(message_json);
        if (j.contains("printers") && j["printers"].is_array()) {
            for (const auto& printer : j["printers"]) {
                if (printer.value("uuid", "") == uuid) {
                    return printer.dump();
                }
            }
        }
    } catch (const nlohmann::json::exception& e) {
        SPDLOG_ERROR("Failed to parse Connect printers JSON: {}", e.what());
    }
    return {};
}
} // namespace

void ConnectMessageHandler::handle_select_printer_message(
    const std::string& message_json
)
{
    std::string uuid;
    try {
        nlohmann::json j = nlohmann::json::parse(message_json);
        j.at("uuid").get_to(uuid);
    } catch (const nlohmann::json::exception& e) {
        SPDLOG_ERROR("JSON parsing error: {}", e.what());
        dispatch_error("Invalid payload: Failed to parse Connect message JSON.");
    }
    if (uuid.empty()) {
        SPDLOG_ERROR("Failed to select printer from Connect: Failed to parse Connect message.");
        dispatch_error("Failed to select printer from Connect: Missing UUID.");
        return;
    }

    auto succ_fn = [this, uuid](const std::string& body)
    {
        std::string printer_json = extract_printer_json_by_uuid(body, uuid);

        if (printer_json.empty()) {
            SPDLOG_WARN("Printer with UUID {} not found in Connect message.", uuid);
            dispatch_error("Printer with requested UUID not found in Connect response.");
            return;
        }

        m_dispatcher.dispatch_on_main_thread([this, uuid, printer_json = std::move(printer_json)]() {
            this->do_select_printer_from_connect(uuid, printer_json);
        });
    };


    auto fail_fn = [this](const std::string& error_msg)
    {
        this->dispatch_error(error_msg);
    };

    m_connect_printer_service.fetch_printer_list(std::move(succ_fn), std::move(fail_fn));
}

void ConnectMessageHandler::do_select_printer_from_connect(
    const std::string& uuid,
    const std::string& printer_json
)
{
    nlohmann::json parsed_printer_json;
    try {
        parsed_printer_json = nlohmann::json::parse(printer_json);
    } catch (const nlohmann::json::parse_error& e) {
        SPDLOG_ERROR("JSON parsing error: {}", e.what());
        dispatch_error("Failed to parse specific printer JSON.");
        return;
    }

    if (m_connect_printer_service.find(uuid)) {
        m_physical_printer_interactor.select_uuid_with_presets(uuid);
    } else if (m_physical_printer_interactor.printer_preset_selector().select_for_connect_printer(parsed_printer_json)) {
        m_physical_printer_interactor.select_default();
        m_export_destination_interactor.select(ExportDestination::ExportDestination::Connect);
    } else {
        return;
    }

    m_last_printer_json = printer_json;
}

std::string ConnectMessageHandler::uuid_for_upload()
{
    const auto* selected = m_physical_printer_interactor.selected_physical_printer_data();
    const auto* upload = selected ? std::get_if<PhysicalPrinter::ConnectUpload>(&selected->payload) : nullptr;
    if (upload && !upload->printer_uuid.empty()) {
        const ConnectPrinter::ConnectPrinter* printer = m_connect_printer_service.find(upload->printer_uuid);
        return printer ? uuid_if_compatible(printer->json) : std::string{};
    }
    return uuid_if_compatible(m_last_printer_json);
}

std::string ConnectMessageHandler::uuid_if_compatible(const std::string& printer_json)
{
    if (printer_json.empty()) {
        return {};
    }

    try {
        nlohmann::json j = nlohmann::json::parse(printer_json);
        const auto& current_config = m_preset_interactor.current_printer_config();

        std::string j_model        = j.value("model", "");
        std::string j_base_model   = j.value("base_model", "");
        uint8_t j_tool_count       = j.value("tool_count", static_cast<uint8_t>(0));
        size_t j_tools_array_size  = j.contains("tools") ? j["tools"].size() : 0;

        if (current_config.model.model == j_model
            && current_config.model.base_model == j_base_model
            && current_config.tool_count == j_tool_count
            && current_config.material_slot_count() == j_tools_array_size)
        {
            return j.value("uuid", "");
        }
    } catch (const nlohmann::json::exception& e) {
        SPDLOG_ERROR("Failed to parse printer json: {}", e.what());
    }

    return {};
}

} // namespace Slic3r::Biz::Connect
