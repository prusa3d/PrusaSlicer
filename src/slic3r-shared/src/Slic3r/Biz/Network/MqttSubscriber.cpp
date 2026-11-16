#include "Slic3r/Biz/Network/MqttSubscriber.hpp"

#include "Slic3r/Log.hpp"
#include "Slic3r/Uuid.hpp"

#include "Slic3r/Version.hpp"
#include "mqtt/async_client.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <iterator>
#include <mutex>
#include <set>
#include <thread>
#include <utility>
#include <vector>


namespace Slic3r::Biz::Network::Mqtt {

namespace {
constexpr std::chrono::seconds CONNECT_TIMEOUT{10};
constexpr std::chrono::seconds REQUEST_TIMEOUT{10};
constexpr std::chrono::seconds DISCONNECT_TIMEOUT{1};
constexpr std::chrono::milliseconds STOP_POLL_INTERVAL{100};
constexpr std::chrono::seconds MIN_RETRY_DELAY{1};
constexpr std::chrono::seconds MAX_RETRY_DELAY{60};
} // namespace

class MQTTImpl : public mqtt::callback
{
public:
    MQTTImpl(
        std::string address,
        std::string username,
        std::string password,
        MQTTCallback cb,
        std::function<void()> on_reconnected
    ) :
        m_address(std::move(address)),
        m_cb(std::move(cb)),
        m_on_reconnected(std::move(on_reconnected)),
        m_username(std::move(username)),
        m_password(std::move(password))
    {
        m_worker = std::thread([this] { run(); });
    }

    ~MQTTImpl() override
    {
        {
            std::lock_guard lock(m_mutex);
            m_stop = true;
        }
        m_cv.notify_all();
        m_worker.join();
    }

    MQTTImpl(const MQTTImpl&)            = delete;
    MQTTImpl& operator=(const MQTTImpl&) = delete;

    void set_topic(const std::string& topic, bool subscribed)
    {
        {
            std::lock_guard lock(m_mutex);
            const bool changed = subscribed ? m_desired_topics.insert(topic).second :
                                              m_desired_topics.erase(topic) > 0;
            if (!changed) {
                return;
            }
            m_changed = true;
        }
        m_cv.notify_all();
    }

    void set_credentials(const std::string& username, const std::string& password)
    {
        {
            std::lock_guard lock(m_mutex);
            m_username            = username;
            m_password            = password;
            m_credentials_changed = true;
        }
        m_cv.notify_all();
    }

private:
    void message_arrived(mqtt::const_message_ptr msg) override
    {
        try {
            m_cb(msg->get_topic(), msg->to_string());
        } catch (const std::exception& exc) {
            SPDLOG_ERROR("MQTT message handler failed: {}", exc.what());
        }
    }

    void connection_lost(const std::string& cause) override
    {
        SPDLOG_WARN("MQTT connection lost: {}", cause);
        {
            std::lock_guard lock(m_mutex);
            m_connection_lost = true;
        }
        m_cv.notify_all();
    }

    void run()
    {
        std::chrono::seconds wait_delay = MIN_RETRY_DELAY;
        std::chrono::seconds next_delay = MIN_RETRY_DELAY;
        bool retry_pending              = false;

        while (true) {
            bool connection_lost = false;
            {
                std::unique_lock lock(m_mutex);
                const auto can_retry_now = [this] { return m_stop || m_credentials_changed || m_connection_lost; };
                const auto has_work      = [this, &can_retry_now] { return can_retry_now() || m_changed; };
                if (retry_pending) {
                    m_cv.wait_for(lock, wait_delay, can_retry_now);
                } else {
                    m_cv.wait(lock, has_work);
                }
                if (m_stop) {
                    break;
                }
                m_changed             = false;
                m_credentials_changed = false;
                connection_lost       = std::exchange(m_connection_lost, false);
            }

            if (!connect_and_sync(connection_lost)) {
                if (m_stop) {
                    break;
                }
                retry_pending = true;
                wait_delay    = next_delay;
                next_delay    = std::min(next_delay * 2, MAX_RETRY_DELAY);
                SPDLOG_WARN("MQTT: retrying in {} s", wait_delay.count());
                continue;
            }
            retry_pending = false;
            next_delay    = MIN_RETRY_DELAY;
        }
        shutdown();
    }

    bool connect_and_sync(bool connection_lost)
    {
        try {
            const bool connected = !connection_lost && m_client && m_client->is_connected();
            return (connected || connect()) && sync_topics();
        } catch (const std::exception& exc) {
            SPDLOG_ERROR("MQTT worker error: {}", exc.what());
            m_client.reset();
            return false;
        }
    }

    bool wait_for_token(const mqtt::token_ptr& token, std::chrono::milliseconds timeout)
    {
        const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + timeout;
        while (!token->wait_for(STOP_POLL_INTERVAL)) {
            if (m_stop || std::chrono::steady_clock::now() >= deadline) {
                return false;
            }
        }
        return true;
    }

    bool connect()
    {
        m_subscribed_topics.clear();

        mqtt::connect_options options;
        {
            std::lock_guard lock(m_mutex);
            options.set_user_name(m_username);
            options.set_password(m_password);
        }
        options.set_clean_session(true);
        options.set_connect_timeout(CONNECT_TIMEOUT);

        mqtt::ssl_options ssl_options;
        ssl_options.set_enable_server_cert_auth(false);
        options.set_ssl(ssl_options);

        try {
            if (!m_client) {
                m_client = std::make_unique<mqtt::async_client>(m_address, m_client_id);
                m_client->set_callback(*this);
            }
            if (!wait_for_token(m_client->connect(options), CONNECT_TIMEOUT + REQUEST_TIMEOUT)) {
                if (!m_stop) {
                    SPDLOG_WARN("MQTT connect timed out.");
                }
                m_client.reset();
                return false;
            }
        } catch (const mqtt::exception& exc) {
            SPDLOG_WARN("MQTT Connect Error: {} [{}]", exc.what(), exc.get_reason_code());
            m_client.reset();
            return false;
        }

        SPDLOG_INFO("MQTT connected.");
        if (m_was_connected && m_on_reconnected) {
            m_on_reconnected();
        }
        m_was_connected = true;
        return true;
    }

    bool sync_topics()
    {
        std::vector<std::string> added;
        std::vector<std::string> removed;
        {
            std::lock_guard lock(m_mutex);
            std::ranges::set_difference(m_desired_topics, m_subscribed_topics, std::back_inserter(added));
            std::ranges::set_difference(m_subscribed_topics, m_desired_topics, std::back_inserter(removed));
        }

        try {
            if (!removed.empty()) {
                if (!wait_for_token(m_client->unsubscribe(mqtt::string_collection::create(removed)), REQUEST_TIMEOUT)) {
                    SPDLOG_WARN("MQTT unsubscribe timed out.");
                    return false;
                }
                for (const std::string& topic : removed) {
                    m_subscribed_topics.erase(topic);
                }
                SPDLOG_INFO("MQTT: unsubscribed from {} topics ({} total)", removed.size(), m_subscribed_topics.size());
            }
            if (!added.empty()) {
                const mqtt::iasync_client::qos_collection qos(added.size(), 0);
                if (!wait_for_token(m_client->subscribe(mqtt::string_collection::create(added), qos), REQUEST_TIMEOUT)) {
                    SPDLOG_WARN("MQTT subscribe timed out.");
                    return false;
                }
                m_subscribed_topics.insert(added.begin(), added.end());
                SPDLOG_INFO("MQTT: subscribed to {} topics ({} total)", added.size(), m_subscribed_topics.size());
                for (const std::string& topic : added) {
                    SPDLOG_DEBUG("MQTT: subscribed to {}", topic);
                }
            }
        } catch (const mqtt::exception& exc) {
            SPDLOG_WARN("MQTT subscription Error: {} [{}]", exc.what(), exc.get_reason_code());
            return false;
        }
        return true;
    }

    void shutdown()
    {
        if (!m_client) {
            return;
        }
        try {
            if (m_client->is_connected()) {
                m_client->disconnect(DISCONNECT_TIMEOUT)->wait_for(DISCONNECT_TIMEOUT + STOP_POLL_INTERVAL);
            }
        } catch (const mqtt::exception& exc) {
            SPDLOG_WARN("MQTT disconnect Error: {} [{}]", exc.what(), exc.get_reason_code());
        } catch (const std::exception& exc) {
            SPDLOG_WARN("MQTT disconnect Error: {}", exc.what());
        }
        m_client.reset();
    }

    const std::string m_client_id = std::string(SLIC3R_BUILD_ID) + "-" + generate_uuid();
    const std::string m_address;
    const MQTTCallback m_cb;
    const std::function<void()> m_on_reconnected;

    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::set<std::string> m_desired_topics;
    std::string m_username;
    std::string m_password;
    bool m_changed{true};
    bool m_credentials_changed{false};
    bool m_connection_lost{false};
    std::atomic<bool> m_stop{false};

    std::unique_ptr<mqtt::async_client> m_client;
    std::set<std::string> m_subscribed_topics;
    bool m_was_connected{false};

    std::thread m_worker;
};

Subscriber::Subscriber(const std::string& address,
                       const std::string& username,
                       const std::string& password,
                       MQTTCallback cb,
                       std::function<void()> on_reconnected) :
    priv(std::make_unique<MQTTImpl>(address, username, password, std::move(cb), std::move(on_reconnected)))
{
}


Subscriber::~Subscriber()
{
}

void Subscriber::subscribe(const std::string& topic)
{
    priv->set_topic(topic, true);
}

void Subscriber::unsubscribe(const std::string& topic)
{
    priv->set_topic(topic, false);
}

void Subscriber::update_credentials(const std::string& username, const std::string& password)
{
    priv->set_credentials(username, password);
}

} // namespace Slic3r::Biz::Network::Mqtt
