#pragma once

#include "Slic3r/Domain/ConfigPhysical.hpp"
#include "Slic3r/Domain/Preset/HwConfig.hpp"
#include "Slic3r/Biz/ConnectPrinter/ConnectPrinter.hpp"

namespace Slic3r::Biz::PhysicalPrinter {

struct ConnectUpload
{
    std::string team_id;
    std::string printer_uuid;
    std::string access_token; // needs to be filled before upload
    ConnectPrinter::ConnectPrinterState state{ConnectPrinter::ConnectPrinterState::Unknown};
};

struct PrinterUpload
{
    Domain::PrintHostType type;
    std::string api_key;
    std::string username;
    std::string password;
    std::string ca_file;
    std::string port;
    Domain::PrintHostAuthType auth_type{Domain::PrintHostAuthType::None};
    bool ssl_revoke_best_effort{false};
};

struct FileSystemExport
{
    bool prefer_removable;
};

struct PhysicalPrinterConfig
{
    std::variant<FileSystemExport, ConnectUpload, PrinterUpload> payload;
    std::string host;
    std::string name;
    std::string uuid;
    std::optional<Domain::Preset::HwPrinterConfig> hw_config;
    /// Created for a project loaded from 3mf, never stored nor editable.
    bool is_runtime{false};
};

/// Name for the UI, marks runtime printers.
std::string ui_name(const PhysicalPrinterConfig& printer);

/**
 * @brief Load external hw config from Connect JSON
 * @param json Connect JSON describing the printer
 * @return External hw config (containing only info available in Connect JSON).
 */
std::optional<Domain::Preset::HwPrinterConfig> hw_config_from_connect_json(std::string_view json);

std::string physical_printer_type_to_string(const PhysicalPrinterConfig& data);

bool is_physical_printer_compatible(
    const PhysicalPrinterConfig& physical_config,
    const Domain::Preset::HwPrinterConfig& hw_config
);

} // namespace Slic3r::Biz::PhysicalPrinter
