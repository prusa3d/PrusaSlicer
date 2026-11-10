#pragma once

#include <memory>
#include <string>
#include <functional>

namespace Slic3r::Biz::Network::Mqtt {

class MQTTImpl;

using MQTTCallback = std::function<void(const std::string&, const std::string&)>;

class Subscriber {
public:
    /**
     * @brief Starts a worker thread that connects to the broker and keeps the subscriptions.
     * No method blocks on the network. Both callbacks are called IN A WORKER THREAD.
     * on_reconnected is called after a lost connection is restored, not after the first connect.
     */
    Subscriber(const std::string& address,
                       const std::string& username,
                       const std::string& password,
                       MQTTCallback cb,
                       std::function<void()> on_reconnected = {});

    void subscribe(const std::string& topic);

    void unsubscribe(const std::string& topic);

    /**
     * @brief Stores credentials for the next connect. A live connection is kept.
     * A pending retry uses the new credentials immediately.
     */
    void update_credentials(const std::string& username, const std::string& password);

    ~Subscriber();


private:

    std::unique_ptr<MQTTImpl> priv;

};

} // Slic3r::Biz::Network::MQTT
