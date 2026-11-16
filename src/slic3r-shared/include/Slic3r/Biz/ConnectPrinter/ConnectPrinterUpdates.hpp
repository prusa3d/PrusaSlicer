#pragma once

#include "Slic3r/Biz/Lifetime.hpp"
#include "Slic3r/Biz/ConnectPrinter/ConnectPrinter.hpp"
#include "Slic3r/Biz/Platform/TimerQueue.hpp"

#include <chrono>
#include <functional>
#include <memory>
#include <string>

namespace Slic3r::Biz::Network::Mqtt {
class Subscriber;
} // namespace Slic3r::Biz::Network::Mqtt

namespace Slic3r::Biz::ConnectPrinter {

/**
 * @brief How ConnectPrinterService learns of changes after the initial printer list: pushed by
 * the server (MQTT) or by polling the list. One lives from login to logout.
 */
class IConnectPrinterUpdates
{
public:
    /**
     * @brief Where the updates go. Called on a worker thread or the main thread, the receiver
     * dispatches to the main thread itself.
     */
    struct Sink
    {
        std::function<void(const std::string& uuid, ConnectPrinterState state)> on_state;
        std::function<void()> on_list_changed;
    };

    virtual ~IConnectPrinterUpdates() = default;

    virtual void watch(const std::string& uuid)   = 0;
    virtual void unwatch(const std::string& uuid) = 0;
    virtual void set_credentials(const std::string& user_id, const std::string& access_token) = 0;

    /**
     * @brief True when printer states arrive through on_state: the fetched list is only the initial
     * state of a printer. False when the fetched list is the only source of the state.
     */
    virtual bool pushes_states() const = 0;
};

/** @brief Printer states and list changes pushed by the Prusa Connect MQTT broker. */
class MqttConnectPrinterUpdates final : public IConnectPrinterUpdates
{
public:
    MqttConnectPrinterUpdates(
        const std::string& address,
        const std::string& user_id,
        const std::string& access_token,
        Sink sink
    );
    ~MqttConnectPrinterUpdates() override;

    void watch(const std::string& uuid) override;
    void unwatch(const std::string& uuid) override;
    void set_credentials(const std::string& user_id, const std::string& access_token) override;

    bool pushes_states() const override
    {
        return true;
    }

private:
    void on_message(const std::string& topic, const std::string& payload);

    Sink m_sink;
    std::unique_ptr<Network::Mqtt::Subscriber> m_subscriber;
};

/** @brief Asks for the printer list again at a fixed interval (Prusa Connect Local has no MQTT). */
class PollingConnectPrinterUpdates final : public IConnectPrinterUpdates
{
public:
    PollingConnectPrinterUpdates(std::chrono::milliseconds interval, Sink sink);
    ~PollingConnectPrinterUpdates() override;

    void watch(const std::string&) override {}
    void unwatch(const std::string&) override {}
    void set_credentials(const std::string&, const std::string&) override {}

    bool pushes_states() const override
    {
        return false;
    }

private:
    Sink m_sink;
    Platform::TimerQueue::TimerID m_timer;
    /** @brief Timer callbacks hold a weak reference, they are dropped once this object is gone. */
    Biz::Lifetime m_lifetime;
};

} // namespace Slic3r::Biz::ConnectPrinter
