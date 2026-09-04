#pragma once

#include "Slic3r/Domain/ConfigPhysical.hpp"
#include "Slic3r/Biz/ObservableListWithSelection.hpp"
#include "Slic3r/Domain/Preset/HwConfig.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"

namespace Slic3r::Biz::PhysicalPrinter {

struct ConnectUpload {
    std::string team_id;
    std::string printer_uuid;
    std::string access_token; // needs to be filled before upload
};

struct PrinterUpload {
    Domain::PrintHostType type;
    std::string api_key;
    std::string username;
    std::string password;
    std::string ca_file;
    std::string port;
    Domain::PrintHostAuthType auth_type{Domain::PrintHostAuthType::None};
    bool ssl_revoke_best_effort{false};
};

struct FileSystemExport {
    bool prefer_removable;
};

struct PhysicalPrinterConfig {
    std::variant<FileSystemExport, ConnectUpload, PrinterUpload> payload;
    std::string host;
    std::string name;
    std::string uuid;
    Domain::Preset::HwPrinterConfig hw_config;
};

PhysicalPrinterConfig filesystem_export_local();
PhysicalPrinterConfig filesystem_export_removable();
PhysicalPrinterConfig connect_upload_generic();

std::string physical_printer_type_to_string(const PhysicalPrinterConfig& data);

bool is_physical_printer_compatible(const PhysicalPrinterConfig& physical_config, const Domain::Preset::HwPrinterConfig& hw_config);

using PhysicalPrinterObservableList = ObservableListWithSelection<PhysicalPrinterConfig>;

} //namespace Slic3r::Biz::PhysicalPrinter