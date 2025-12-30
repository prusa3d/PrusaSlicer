#pragma once

#include "Slic3r/Biz/Network/MqttSubscriber.hpp"

#include <string>
#include <map>
#include <memory>

namespace Slic3r::Biz::UserAccount {

enum class ConnectPrinterState {
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

typedef std::map<std::string, ConnectPrinterState> UUIDStateMap;

class ConnectPrinterStateMonitor {
public:
    ConnectPrinterStateMonitor()  = default;
    ~ConnectPrinterStateMonitor() = default;

    ConnectPrinterStateMonitor(const ConnectPrinterStateMonitor&)             = delete;
    ConnectPrinterStateMonitor(ConnectPrinterStateMonitor&& other)            = delete;
    ConnectPrinterStateMonitor& operator=(const ConnectPrinterStateMonitor&)  = delete;
    ConnectPrinterStateMonitor& operator=(ConnectPrinterStateMonitor&& other) = delete;

    void on_logged_out();
    
    void on_mqqt_callback(const std::string& topic, const std::string& data);

    void on_full_printer_states(const std::string& data, const std::string& usr, const std::string jwt);

    const std::map<std::string, ConnectPrinterState>& state_map()
    {
        return m_uuid_to_state_map;
    }

private:
    void on_logged_in(const std::string& usr, const std::string jwt);

    std::unique_ptr<Network::Mqtt::Subscriber> m_mqtt_subscriber {nullptr};
    UUIDStateMap m_uuid_to_state_map;

    const std::map<std::string, ConnectPrinterState> printer_state_table = {
        {"OFFLINE"  , ConnectPrinterState::Offline},
        {"PRINTING" , ConnectPrinterState::Printing},
        {"PAUSED"   , ConnectPrinterState::Paused},
        {"STOPPED"  , ConnectPrinterState::Stopped},
        {"IDLE"     , ConnectPrinterState::Idle},
        {"FINISHED" , ConnectPrinterState::Finished},
        {"READY"    , ConnectPrinterState::Ready},
        {"ATTENTION", ConnectPrinterState::Attention},
        {"BUSY"     , ConnectPrinterState::Busy},
        {"ERROR"    , ConnectPrinterState::Error},
    };
};

}