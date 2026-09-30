#include "Slic3r/Biz/PhysicalPrinter/PhysicalPrinterConfig.hpp"

#include "Slic3r/Biz/ConnectPrinter/ConnectPrinter.hpp"
#include "Slic3r/Domain/ConfigPhysical.hpp"

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

PhysicalPrinterConfig no_printer()
{
    return {
        NoPrinter{},
        {},
        // TRN Physical printer list entry: no specific printer selected.
        _u8L("No printer"),
        std::string(NO_PRINTER_UUID),
    };
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
    };
}

bool is_reserved_uuid(const std::string& uuid)
{
    return uuid == NO_PRINTER_UUID;
}

bool is_physical_printer_compatible(
    const PhysicalPrinterConfig& physical_config,
    const Domain::Preset::HwPrinterConfig& hw_config
)
{
    bool compatible = physical_config.hw_config.technology == hw_config.technology
        && physical_config.hw_config.model == hw_config.model
        && physical_config.hw_config.tool_count == hw_config.tool_count
        && physical_config.hw_config.tools == hw_config.tools
        ;
    return compatible;
}

} // namespace Slic3r::Biz::PhysicalPrinter
