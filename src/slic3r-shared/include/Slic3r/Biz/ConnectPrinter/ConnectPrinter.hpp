#pragma once

#include <nlohmann/json_fwd.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Slic3r::Biz::ConnectPrinter {

enum class ConnectPrinterState {
    Unknown,
    Offline,
    Printing,
    Paused,
    Stopped,
    Idle,
    Finished,
    Ready,
    Attention,
    Busy,
    Error
};

struct ConnectPrinter
{
    std::string uuid;
    std::string name;
    std::string model;
    std::string base_model;
    ConnectPrinterState state{ConnectPrinterState::Unknown};
    std::string json;

    bool operator==(const ConnectPrinter&) const = default;
};

std::optional<ConnectPrinterState> parse_connect_printer_state(std::string_view state);

std::string connect_printer_state_to_string(ConnectPrinterState state);

/**
 * @brief Locates the array of printer objects in a printer list response body {"printers":[...]}.
 * @return The array, or nullopt when the body is not JSON or has no "printers" array.
 */
std::optional<nlohmann::json> extract_connect_printers_array(const std::string& body);

std::optional<std::vector<ConnectPrinter>> parse_connect_printer_list(const std::string& body);

} // namespace Slic3r::Biz::ConnectPrinter
