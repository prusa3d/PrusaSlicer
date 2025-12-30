#include "Slic3r/Biz/Network/MqttSubscriber.hpp"

#include "Slic3r/Log.hpp"

#include "Slic3r/Version.hpp"
#include "mqtt/async_client.h"


namespace Slic3r::Biz::Network::Mqtt {

class callback : public virtual mqtt::callback
{
    void message_arrived(mqtt::const_message_ptr msg) override
    {
        m_cb(msg->get_topic(), msg->to_string());
    }
    MQTTCallback m_cb;

public:
    void set_callback(MQTTCallback cb) { m_cb = cb; }
};

class MQTTImpl
{
    const std::string CLIENT_ID = SLIC3R_BUILD_ID;
    std::unique_ptr<mqtt::async_client> m_client;
    callback m_cb;

    friend class Subscriber;
};

Subscriber::Subscriber(const std::string& address, 
                       const std::string& username, 
                       const std::string& password, 
                       MQTTCallback cb)
{
    try {
        priv = std::make_unique<MQTTImpl>();
        priv->m_client = std::make_unique<mqtt::async_client>(address, priv->CLIENT_ID);
        priv->m_cb.set_callback(cb);
        priv->m_client->set_callback(priv->m_cb);

        mqtt::connect_options connOpts;
        connOpts.set_clean_session(true);
        connOpts.set_user_name(username);
        connOpts.set_password(password);

        mqtt::ssl_options sslOpts;
        sslOpts.set_enable_server_cert_auth(false);

        connOpts.set_ssl(sslOpts);
        connOpts.set_clean_session(true);
        auto token = priv->m_client->connect(connOpts);
        token->wait();
    } catch (const mqtt::exception& exc) {
        SPDLOG_ERROR("MQTT Connect Error: {} [{}]", exc.what(), exc.get_reason_code());
        throw; 
    }
}


Subscriber::~Subscriber()
{
}

void Subscriber::subscribe(const std::string& topic)
{
    try {
        priv->m_client->subscribe(topic, 0)->wait();
    } catch (const mqtt::exception& exc) {
        SPDLOG_ERROR("MQTT subscribe Error: {} [{}]", exc.what(), exc.get_reason_code());
        return;
    }
    SPDLOG_INFO("Subscribed to {}", topic);
}

void Subscriber::unsubscribe(const std::string& topic)
{
    try {
        priv->m_client->unsubscribe(topic)->wait();
    } catch (const mqtt::exception& exc) {
        SPDLOG_ERROR("MQTT unsubscribe Error: {} [{}]", exc.what(), exc.get_reason_code());
    }
}

} // namespace Slic3r::Biz::Network::Mqtt

