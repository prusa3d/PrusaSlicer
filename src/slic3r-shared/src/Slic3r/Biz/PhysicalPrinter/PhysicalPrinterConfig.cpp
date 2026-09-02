#include "Slic3r/Biz/PhysicalPrinter/PhysicalPrinterConfig.hpp"

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

PhysicalPrinterConfig filesystem_export_local()
{
    return {
        FileSystemExport{false},
        {},
        _u8L("Local Drive"),
        std::string(LOCAL_DRIVE_UUID),
    };
}

PhysicalPrinterConfig filesystem_export_removable()
{
    return {
        FileSystemExport{true},
        {},
        _u8L("Removable Drive"),
        std::string(REMOVABLE_DRIVE_UUID),
    };
}

PhysicalPrinterConfig connect_upload_generic()
{
    return {
        ConnectUpload{},
        {},
        _u8L("Prusa Connect"),
        std::string(PRUSA_CONNECT_UUID),
    };
}

bool is_reserved_uuid(const std::string& uuid)
{
    return uuid == LOCAL_DRIVE_UUID || uuid == REMOVABLE_DRIVE_UUID || uuid == PRUSA_CONNECT_UUID;
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
