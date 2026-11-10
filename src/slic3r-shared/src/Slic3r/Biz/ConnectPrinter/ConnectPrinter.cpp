#include "Slic3r/Biz/ConnectPrinter/ConnectPrinter.hpp"

#include "Slic3r/Log.hpp"

#include <map>
#include <nlohmann/json.hpp>

namespace Slic3r::Biz::ConnectPrinter {

std::optional<ConnectPrinterState> parse_connect_printer_state(std::string_view state)
{
    static const std::map<std::string_view, ConnectPrinterState> state_table = {
        {"OFFLINE"  , ConnectPrinterState::Offline},
        {"PRINTING" , ConnectPrinterState::Printing},
        {"PAUSED"   , ConnectPrinterState::Paused},
        {"STOPPED"  , ConnectPrinterState::Stopped},
        {"IDLE"     , ConnectPrinterState::Idle},
        {"FINISHED" , ConnectPrinterState::Finished},
        {"READY"    , ConnectPrinterState::Ready},
        {"ATTENTION", ConnectPrinterState::Attention},
        {"BUSY"     , ConnectPrinterState::Busy},
        {"ERROR"    , ConnectPrinterState::Error},
    };

    if (auto it = state_table.find(state); it != state_table.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::string connect_printer_state_to_string(ConnectPrinterState state)
{
    switch (state) {
    case ConnectPrinterState::Unknown:   return "Unknown";
    case ConnectPrinterState::Offline:   return "Offline";
    case ConnectPrinterState::Printing:  return "Printing";
    case ConnectPrinterState::Paused:    return "Paused";
    case ConnectPrinterState::Stopped:   return "Stopped";
    case ConnectPrinterState::Idle:      return "Idle";
    case ConnectPrinterState::Finished:  return "Finished";
    case ConnectPrinterState::Ready:     return "Ready";
    case ConnectPrinterState::Attention: return "Attention";
    case ConnectPrinterState::Busy:      return "Busy";
    case ConnectPrinterState::Error:     return "Error";
    }
    return {};
}

namespace {
std::string string_field(const nlohmann::json& j, const char* key)
{
    if (auto it = j.find(key); it != j.end() && it->is_string()) {
        return it->get<std::string>();
    }
    return {};
}
} // namespace

std::optional<nlohmann::json> extract_connect_printers_array(const std::string& body)
{
    nlohmann::json json;
    try {
        json = nlohmann::json::parse(body);
    } catch (const nlohmann::json::parse_error& e) {
        SPDLOG_ERROR("Could not parse Connect printer list. {}", e.what());
        return std::nullopt;
    }

    if (json.is_object()) {
        if (auto it = json.find("printers"); it != json.end() && it->is_array()) {
            return std::move(*it);
        }
    }

    SPDLOG_ERROR("Unexpected format of Connect printer list.");
    return std::nullopt;
}

std::optional<std::vector<ConnectPrinter>> parse_connect_printer_list(const std::string& body)
{
    const std::optional<nlohmann::json> printers_json = extract_connect_printers_array(body);
    if (!printers_json) {
        return std::nullopt;
    }

    std::vector<ConnectPrinter> printers;
    for (const nlohmann::json& printer_json : *printers_json) {
        if (!printer_json.is_object()) {
            continue;
        }
        ConnectPrinter printer;
        printer.uuid = string_field(printer_json, "uuid");
        if (printer.uuid.empty()) {
            continue;
        }
        printer.name       = string_field(printer_json, "name");
        printer.model      = string_field(printer_json, "model");
        printer.base_model = string_field(printer_json, "base_model");
        printer.state      = parse_connect_printer_state(string_field(printer_json, "connect_state"))
                            .value_or(ConnectPrinterState::Unknown);
        printer.json       = printer_json.dump();
        printers.push_back(std::move(printer));
    }
    return printers;
}

} // namespace Slic3r::Biz::ConnectPrinter
