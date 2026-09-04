#include "Slic3r/Biz/PhysicalPrinter/PhysicalPrinterConfig.hpp"

#include "Slic3r/Domain/ConfigPhysical.hpp"

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

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
        boost::uuids::to_string(boost::uuids::random_generator()()),

    };
}

PhysicalPrinterConfig filesystem_export_removable()
{
    return {
        FileSystemExport{true},
        {},
        _u8L("Removable Drive"),
        boost::uuids::to_string(boost::uuids::random_generator()()),
    };
}

PhysicalPrinterConfig connect_upload_generic()
{
    return {
        ConnectUpload{},
        {},
        _u8L("Prusa Connect"),
        boost::uuids::to_string(boost::uuids::random_generator()()),
    };
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
