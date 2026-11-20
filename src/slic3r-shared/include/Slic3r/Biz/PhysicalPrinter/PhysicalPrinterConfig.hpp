#pragma once

#include "Slic3r/Domain/ConfigPhysical.hpp"
#include "Slic3r/Domain/Preset/HwConfig.hpp"
#include "Slic3r/Biz/ConnectPrinter/ConnectPrinter.hpp"

#include <functional>
#include <string>

namespace Slic3r::Biz::PhysicalPrinter {

/**
 * @brief Which Prusa Connect upload API the print host speaks.
 *
 * SlicerRest is the cloud: file records of an organization and the printer queue. Legacy is
 * the team uploads API, the only one Prusa Connect Local serves.
 */
enum class ConnectUploadApi
{
    SlicerRest,
    Legacy
};

struct ConnectUpload
{
    ConnectUploadApi api{ConnectUploadApi::SlicerRest};
    /** @brief Required by ConnectUploadApi::SlicerRest. */
    std::string organization_id;
    /** @brief Required by ConnectUploadApi::Legacy. */
    std::string team_id;
    std::string printer_uuid;
    std::string access_token; // needs to be filled before upload
    /**
     * @brief The account's token at the time of a request, called on the upload thread. An
     * upload can outlast the token it started with. Without it, or when it has none,
     * access_token is used.
     */
    std::function<std::string()> current_access_token;
    /**
     * @brief Where a file record is created (POST) and, with its id appended, finished (PATCH).
     * Required by ConnectUploadApi::SlicerRest. The URLs are those of the service the upload
     * was started with, whatever the app switches to meanwhile.
     */
    std::string files_url;
    /**
     * @brief Where an uploaded file is sent to the printer or its queue (POST). Required by
     * ConnectUploadApi::SlicerRest.
     */
    std::string queue_url;
    /** @brief The page of the printer in the Connect web app, empty when there is none. */
    std::string printer_page_url;
    /**
     * @brief Called (on the upload thread) with the token the server refused (HTTP 401).
     */
    std::function<void(const std::string& access_token)> on_unauthorized;
    ConnectPrinter::ConnectPrinterState state{ConnectPrinter::ConnectPrinterState::Unknown};

    std::string bearer_token() const
    {
        if (current_access_token) {
            std::string token = current_access_token();
            if (!token.empty()) {
                return token;
            }
        }
        return access_token;
    }

    void report_unauthorized(unsigned status, const std::string& refused_token) const
    {
        if (status == 401 && on_unauthorized) {
            on_unauthorized(refused_token);
        }
    }
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

bool is_connect_printer(const PhysicalPrinterConfig& printer);

/**
 * @brief Load external hw config from Connect JSON
 * @param json Connect JSON describing the printer
 * @return External hw config (containing only info available in Connect JSON).
 */
std::optional<Domain::Preset::HwPrinterConfig> hw_config_from_connect_json(std::string_view json);

/// Connect printer whose data did not resolve to a known printer model, so it is unknown which
/// logical printer preset belongs to it.
bool has_unresolved_model(const PhysicalPrinterConfig& printer);

std::string physical_printer_type_to_string(const PhysicalPrinterConfig& data);

bool is_physical_printer_compatible(
    const PhysicalPrinterConfig& physical_config,
    const Domain::Preset::HwPrinterConfig& hw_config
);

} // namespace Slic3r::Biz::PhysicalPrinter
