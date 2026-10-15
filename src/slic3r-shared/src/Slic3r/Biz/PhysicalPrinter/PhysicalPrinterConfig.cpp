#include "Slic3r/Biz/PhysicalPrinter/PhysicalPrinterConfig.hpp"

#include "Slic3r/Biz/ConnectPrinter/ConnectPrinter.hpp"
#include "Slic3r/Biz/Preset/IO/HwConfigConnectJson.hpp"

namespace Slic3r::Biz::PhysicalPrinter {

std::string physical_printer_type_to_string(const PhysicalPrinterConfig& data)
{
    return std::visit(
        [](const auto& auth) -> std::string
        {
            using T = std::remove_cvref_t<decltype(auth)>;
            if constexpr (std::is_same_v<T, PrinterUpload>) {
                return print_host_type_to_string(auth.type);
            } else if constexpr (std::is_same_v<T, ConnectUpload>) {
                return {};
            } else {
                return {};
            }
        },
        data.payload
    );
}

PhysicalPrinterConfig connect_printer_config(const ConnectPrinter::ConnectPrinter& printer)
{
    ConnectUpload upload;
    upload.printer_uuid = printer.uuid;
    upload.state        = ConnectPrinter::connect_printer_state_to_string(printer.state);
    return {
        std::move(upload),
        {},
        printer.name.empty() ? printer.model : printer.name,
        printer.uuid,
        hw_config_from_connect_json(printer.json)
    };
}

std::optional<Domain::Preset::HwPrinterConfig> hw_config_from_connect_json(std::string_view json)
{
    auto result = Preset::IO::load_external_hw_config(json);
    if (!result.has_value()) {
        SPDLOG_ERROR("Failed load external hw_config from json: {}", result.error());
        return std::nullopt;
    }
    return result.value();
}

bool is_physical_printer_compatible(
    const PhysicalPrinterConfig& physical_config,
    const Domain::Preset::HwPrinterConfig& hw_config
)
{
    if (!physical_config.hw_config.has_value()) {
        return false;
    }

    const auto& phy_hw_config = physical_config.hw_config.value();
    bool compatible = phy_hw_config.technology == hw_config.technology
        && phy_hw_config.model == hw_config.model
        && phy_hw_config.tool_count == hw_config.tool_count
        && phy_hw_config.tools == hw_config.tools
        ;
    return compatible;
}

} // namespace Slic3r::Biz::PhysicalPrinter
