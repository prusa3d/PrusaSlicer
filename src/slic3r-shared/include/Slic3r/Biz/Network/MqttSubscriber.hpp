#pragma once

#include <memory>
#include <string>
#include <functional>

namespace Slic3r::Biz::Network::Mqtt {

class MQTTImpl;

using MQTTCallback = std::function<void(const std::string&, const std::string&)>;

class Subscriber {
public:
    // Connect to a broker, subscribe to one topic and register a callback.
    // The callback will be called  IN A WORKER THREAD !!!    
    Subscriber(const std::string& address, 
                       const std::string& username, 
                       const std::string& password, 
                       MQTTCallback cb);

    // Subscribe to a given topic. Multiple topics can be subscribed to.
    void subscribe(const std::string& topic);

    // Unsubscribe from a given topic.
    void unsubscribe(const std::string& topic);

    ~Subscriber();
    

private:

    std::unique_ptr<MQTTImpl> priv;

};

} // Slic3r::Biz::Network::MQTT
