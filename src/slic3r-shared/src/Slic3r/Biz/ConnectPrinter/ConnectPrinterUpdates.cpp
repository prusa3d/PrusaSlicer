#include "Slic3r/Biz/ConnectPrinter/ConnectPrinterUpdates.hpp"

#include "Slic3r/Biz/Network/MqttSubscriber.hpp"
#include "Slic3r/Biz/Platform/PlatformServices.hpp"
#include "Slic3r/Log.hpp"

#include <optional>
#include <string_view>

namespace Slic3r::Biz::ConnectPrinter {

namespace {
constexpr std::string_view STATE_TOPIC_PREFIX = "v1/devices/printers/";
constexpr std::string_view STATE_TOPIC_SUFFIX = "/data/state";

std::string state_topic(const std::string& uuid)
{
    return std::string(STATE_TOPIC_PREFIX) + uuid + std::string(STATE_TOPIC_SUFFIX);
}

std::string uuid_from_state_topic(std::string_view topic)
{
    if (!topic.starts_with(STATE_TOPIC_PREFIX)
        || !topic.ends_with(STATE_TOPIC_SUFFIX)
        || topic.size() <= STATE_TOPIC_PREFIX.size() + STATE_TOPIC_SUFFIX.size())
    {
        return {};
    }
    return std::string(topic.substr(
        STATE_TOPIC_PREFIX.size(),
        topic.size() - STATE_TOPIC_PREFIX.size() - STATE_TOPIC_SUFFIX.size()
    ));
}

constexpr std::string_view MPI_CHANGED_TOPIC_SUFFIX = "/data/tools/+/mpi/changed";
constexpr std::string_view MPI_CHANGED_TOPIC_END    = "/mpi/changed";

std::string mpi_changed_topic(const std::string& uuid)
{
    return std::string(STATE_TOPIC_PREFIX) + uuid + std::string(MPI_CHANGED_TOPIC_SUFFIX);
}

bool is_mpi_changed_topic(std::string_view topic)
{
    return topic.starts_with(STATE_TOPIC_PREFIX) && topic.ends_with(MPI_CHANGED_TOPIC_END);
}
} // namespace

MqttConnectPrinterUpdates::MqttConnectPrinterUpdates(
    const std::string& address,
    const std::string& user_id,
    const std::string& access_token,
    Sink sink
) :
    m_sink(std::move(sink)),
    m_subscriber(std::make_unique<Network::Mqtt::Subscriber>(
        address,
        user_id,
        access_token,
        [this](const std::string& topic, const std::string& payload) { on_message(topic, payload); },
        [this]()
        {
            if (m_sink.on_list_changed) {
                m_sink.on_list_changed();
            }
        }
    ))
{}

MqttConnectPrinterUpdates::~MqttConnectPrinterUpdates() = default;

void MqttConnectPrinterUpdates::watch(const std::string& uuid)
{
    m_subscriber->subscribe(state_topic(uuid));
    m_subscriber->subscribe(mpi_changed_topic(uuid));
}

void MqttConnectPrinterUpdates::unwatch(const std::string& uuid)
{
    m_subscriber->unsubscribe(state_topic(uuid));
    m_subscriber->unsubscribe(mpi_changed_topic(uuid));
}

void MqttConnectPrinterUpdates::
    set_credentials(const std::string& user_id, const std::string& access_token)
{
    m_subscriber->update_credentials(user_id, access_token);
}

void MqttConnectPrinterUpdates::on_message(const std::string& topic, const std::string& payload)
{
    SPDLOG_DEBUG("MQTT: {} - {}", topic, payload);
    if (is_mpi_changed_topic(topic)) {
        if (m_sink.on_list_changed) {
            m_sink.on_list_changed();
        }
        return;
    }
    const std::string uuid = uuid_from_state_topic(topic);
    if (uuid.empty()) {
        SPDLOG_ERROR("Failed to extract uuid from mqtt topic {}", topic);
        return;
    }
    const std::optional<ConnectPrinterState> state = parse_connect_printer_state(payload);
    if (!state) {
        SPDLOG_ERROR("Unknown Connect printer state '{}' for printer {}", payload, uuid);
        return;
    }
    if (m_sink.on_state) {
        m_sink.on_state(uuid, *state);
    }
}

PollingConnectPrinterUpdates::PollingConnectPrinterUpdates(
    std::chrono::milliseconds interval,
    Sink sink
) :
    m_sink(std::move(sink))
{
    SPDLOG_INFO("Connect printers: polling the list every {} ms", interval.count());
    m_timer = Platform::PlatformServices::instance().timer_queue().set_timer(
        interval,
        [this, lifetime = m_lifetime.token()]()
        {
            if (lifetime.expired() || !m_sink.on_list_changed) {
                return;
            }
            m_sink.on_list_changed();
        },
        true
    );
}

PollingConnectPrinterUpdates::~PollingConnectPrinterUpdates()
{
    m_lifetime.end();
    Platform::TimerQueue& timer_queue = Platform::PlatformServices::instance().timer_queue();
    if (timer_queue.is_timer_running(m_timer)) {
        timer_queue.cancel_timer(m_timer);
    }
    SPDLOG_INFO("Connect printers: polling stopped");
}

} // namespace Slic3r::Biz::ConnectPrinter
