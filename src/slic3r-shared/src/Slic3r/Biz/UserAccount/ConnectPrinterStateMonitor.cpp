#include "Slic3r/Biz/UserAccount/ConnectPrinterStateMonitor.hpp"
#include "Slic3r/Biz/Network/ServiceConfig.hpp"
#include "Slic3r/Biz/Network/Jwt.hpp"

#include "Slic3r/Log.hpp"
#include "Slic3r/Assert.hpp"

#include <functional>
#include <nlohmann/json.hpp>

namespace Slic3r::Biz::UserAccount {

const std::string TOPIC_PREFIX = "v1/devices/printers/";
const std::string TOPIC_SUFFIX = "/data/state";

void ConnectPrinterStateMonitor::on_logged_in(const std::string& usr, const std::string jwt)
{
    std::string address = "wss://mqtt.prusa3d.com:8084/mqtt";
    try {
        m_mqtt_subscriber = std::make_unique<Network::Mqtt::Subscriber>(
            std::move(address),
            usr,
            jwt,
            [this](const std::string& topic, const std::string& data)
            { on_mqqt_callback(topic, data); }
        );
    } catch (...) {
        m_mqtt_subscriber.reset();
        m_uuid_to_state_map.clear();
        return;
    }

    for (const auto& pair : m_uuid_to_state_map) {
        const std::string TOPIC(  "");
        m_mqtt_subscriber->subscribe(TOPIC_PREFIX+ pair.first +TOPIC_SUFFIX);
    }
}

void ConnectPrinterStateMonitor::on_logged_out() 
{
    m_mqtt_subscriber.reset();
    m_uuid_to_state_map.clear();
}

namespace {
std::string extract_uuid(std::string_view path) {
    if (!path.starts_with(TOPIC_PREFIX) || !path.ends_with(TOPIC_SUFFIX)) {
        return {};
    }
    if (path.size() > TOPIC_PREFIX.size() + TOPIC_SUFFIX.size()) {
        return std::string(path.substr(
            TOPIC_PREFIX.size(),
            path.size() - TOPIC_PREFIX.size() - TOPIC_PREFIX.size()
        ));
    }
    return {};
}
}

void ConnectPrinterStateMonitor::on_mqqt_callback(const std::string& topic, const std::string& data)
{
    SPDLOG_DEBUG("MQTT: {} - {}", topic, data);
    ConnectPrinterState state;
    if (auto it = printer_state_table.find(data); it != printer_state_table.end()) {
        state = it->second;
    } else {
        DEBUG_ASSERT(false, "printer_state_table needs update");
        return;
    }
    std::string uuid = extract_uuid(topic);
    if (uuid.empty()) {
        SPDLOG_ERROR("Failed to extract uuid from mqtt topic {}", topic);
        return;
    }
    m_uuid_to_state_map[uuid] = state;
}

void ConnectPrinterStateMonitor::on_full_printer_states(const std::string& data, const std::string& usr, const std::string jwt)
{
    SPDLOG_DEBUG("Prusa Connect printers message: {}", data);

    // tree format: 
    /*
    [{
        "printer_uuid": "972d2ce7-0967-4555-bff2-330c7fa0a4e1",
            "printer_state" : "IDLE"
    }, {
        "printer_uuid": "15d160fd-c7e4-4b5a-9748-f0e531c7e0f5",
        "printer_state" : "OFFLINE"
    }]
    */

    nlohmann::json json_data;
    try {
        json_data = nlohmann::json::parse(data);
    }
    catch (const nlohmann::json::parse_error& e) {
        SPDLOG_ERROR("Could not parse prusaconnect message. {}", e.what());
        return;
    }

    if (!json_data.is_array()) {
        SPDLOG_ERROR("Uexpected JSON format when reading Printers state msg. JSON is not array.");
        return;
    }

    
    for (const auto& printer_obj : json_data) {
        if (!printer_obj.contains("printer_uuid") || !printer_obj.contains("printer_state")) {
            continue;
        }
        if (!printer_obj["printer_uuid"].is_string() || !printer_obj["printer_state"].is_string()) {
            continue;
        }
        const std::string printer_uuid = printer_obj.at("printer_uuid").get<std::string>();
        const std::string printer_state_str = printer_obj.at("printer_state").get<std::string>();
    
        ConnectPrinterState state;
        if (auto it = printer_state_table.find(printer_state_str); it != printer_state_table.end()) {
            state = it->second;
        } else {
            DEBUG_ASSERT(false, "printer_state_table needs update");
            continue;
        }
    
        m_uuid_to_state_map[printer_uuid] = state;
    }

    on_logged_in(usr, jwt);
}

} // namespace Slic3r::Biz::UserAccount
