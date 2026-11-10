#include "Slic3r/Biz/UserAccount/ConnectUtils.hpp"

#include "Slic3r/Log.hpp"
#include "Slic3r/Assert.hpp"

#include <nlohmann/json.hpp>

namespace Slic3r::Biz::UserAccount::ConnectUtils {

bool config_from_json(const std::string& json, PhysicalPrinter::PhysicalPrinterConfig& config, std::string& filename, std::string& body_json)
{
    PhysicalPrinter::ConnectUpload* auth = std::get_if<PhysicalPrinter::ConnectUpload>(&config.payload);
    ASSERT(auth);

    SPDLOG_DEBUG("Connect message: {}", json);
    // {"action":"PRINT","set_ready":false,"team_id":12345,"printer_uuid":"12345-12345-12345-12345-12345","filename":"file.gcode","data":{"set_ready":false,"team_id":10533,"printer_uuid":"12345-12345-12345-12345-12345","filename":"file.gcode"}}
    try {
        nlohmann::json j = nlohmann::json::parse(json);

        if (j.contains("filename") && j["filename"].is_string()) {
            filename = j["filename"].get<std::string>();
        } else {
            SPDLOG_ERROR("Could not find filename in Connect message.");
            return false;
        }

        if (j.contains("printer_uuid") && j["printer_uuid"].is_string()) {
            auth->printer_uuid = j["printer_uuid"].get<std::string>();
        } else {
            SPDLOG_ERROR("Could not find printer_uuid in Connect message.");
            return false;
        }

        if (j.contains("data") && j["data"].is_object()) {
            body_json = j["data"].dump();
        } else {
            SPDLOG_ERROR("Could not find data subtree in Connect message.");
            return false;
        }

        if (j.contains("organization_id") && j["organization_id"].is_string()) {
            auth->organization_id = j["organization_id"].get<std::string>();
        } else {
            SPDLOG_WARN("Could not find organization_id in Connect message.");
        }
    } catch (const nlohmann::json::exception& e) {
        SPDLOG_ERROR("Could not parse Connect message: {}", e.what());
        return false;
    }


    return true;
}

std::string filename_from_json(const std::string& json_str)
{
    try {
        nlohmann::json j = nlohmann::json::parse(json_str);

        if (j.contains("filename") && j["filename"].is_string()) {
            return j["filename"].get<std::string>();
        } else {
            SPDLOG_ERROR("Could not find filename in Connect message.");
        }
    } catch (const nlohmann::json::exception& e) {
        SPDLOG_ERROR("Could not parse Connect message: {}", e.what());
    }

    return {};
}

} //namespace Slic3r::Biz::UserAccount