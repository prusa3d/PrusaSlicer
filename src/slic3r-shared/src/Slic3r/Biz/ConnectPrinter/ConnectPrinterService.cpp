#include "Slic3r/Biz/ConnectPrinter/ConnectPrinterService.hpp"

#include "Slic3r/Biz/Network/MqttSubscriber.hpp"
#include "Slic3r/Biz/Network/ServiceConfig.hpp"
#include "Slic3r/Biz/UserAccount/UserAccountInteractor.hpp"

#include "Slic3r/Log.hpp"

#include <algorithm>
#include <string_view>
#include <thread>

namespace Slic3r::Biz::ConnectPrinter {

namespace {
const std::string MQTT_ADDRESS = "wss://mqtt.prusa3d.com:8084/mqtt";
constexpr std::string_view STATE_TOPIC_PREFIX = "v1/devices/printers/";
constexpr std::string_view STATE_TOPIC_SUFFIX = "/data/state";

std::string state_topic(const std::string& uuid)
{
    return std::string(STATE_TOPIC_PREFIX) + uuid + std::string(STATE_TOPIC_SUFFIX);
}

std::string uuid_from_state_topic(std::string_view topic)
{
    if (!topic.starts_with(STATE_TOPIC_PREFIX) || !topic.ends_with(STATE_TOPIC_SUFFIX)
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

ConnectPrinterService::ConnectPrinterService(
    Platform::IMainThreadDispatcher& dispatcher,
    UserAccount::UserAccountInteractor& user_account_interactor
) :
    m_dispatcher(dispatcher),
    m_user_account_interactor(user_account_interactor)
{}

ConnectPrinterService::~ConnectPrinterService() = default;

const ObservableList<ConnectPrinter>& ConnectPrinterService::observable_list() const
{
    return m_observable_list;
}

void ConnectPrinterService::add_printer_list_observer(IListObserver<ConnectPrinter>* observer)
{
    m_observable_list.add_listener<IListObserver<ConnectPrinter>>(observer);
}

void ConnectPrinterService::remove_printer_list_observer(IListObserver<ConnectPrinter>* observer)
{
    m_observable_list.remove_listener<IListObserver<ConnectPrinter>>(observer);
}

const ConnectPrinter* ConnectPrinterService::find(const std::string& uuid) const
{
    if (std::optional<size_t> index = find_index(uuid)) {
        return &m_observable_list.at(*index);
    }
    return nullptr;
}

void ConnectPrinterService::fetch_printer_list(
    std::function<void(const std::string&)> success_fn,
    std::function<void(const std::string&)> fail_fn,
    std::function<void(Network::IHttp::Retry, bool&)> retry_fn,
    std::function<void(Network::IHttp::Progress, bool&)> progress_fn
)
{
    std::thread(
        [this,
         generation  = m_session_generation,
         url         = Network::ServiceConfig::instance().connect_printer_list_url(),
         access_token = m_user_account_interactor.access_token(),
         success_fn  = std::move(success_fn),
         fail_fn     = std::move(fail_fn),
         retry_fn    = std::move(retry_fn),
         progress_fn = std::move(progress_fn)]() mutable
        {
            std::unique_ptr<Network::IHttp> http =
                Network::IHttp::create(Network::IHttp::RequestMethod::Get, url, retry_fn);

            if (!access_token.empty()) {
                http->header("Authorization", "Bearer " + access_token);
            }

            http->on_complete(
                [this, generation, success_fn = std::move(success_fn)](std::string body, unsigned status)
                {
                    if (std::optional<std::vector<ConnectPrinter>> printers = parse_connect_printer_list(body)) {
                        m_dispatcher.dispatch_on_main_thread(
                            [this, generation, printers = std::move(*printers)]() mutable
                            {
                                if (generation == m_session_generation) {
                                    apply_printer_list(std::move(printers));
                                }
                            });
                    }
                    if (success_fn) {
                        success_fn(body);
                    }
                });

            http->on_error(
                [fail_fn = std::move(fail_fn)](std::string body, std::string error, unsigned status)
                {
                    SPDLOG_ERROR("Connect HTTP request failed. Status: {}, Error: {}",
                                 status,
                                 error);
                    if (fail_fn) {
                        fail_fn("Connect HTTP request failed: " + error);
                    }
                });
            http->on_progress(
                [progress_fn = std::move(progress_fn)](Network::IHttp::Progress progress,
                                                       bool& cancel)
                {
                    if (progress_fn) {
                        progress_fn(progress, cancel);
                    }
                });

            http->perform_sync(Network::HttpRetryOpt::default_retry());
        })
        .detach();
}

void ConnectPrinterService::on_user_account_id_success(bool is_refresh, const std::string& username)
{
    if (!m_mqtt_subscriber) {
        start_mqtt();
    } else if (m_user_account_interactor.access_token() != m_mqtt_access_token) {
        update_mqtt_access_token();
    }

    if (!is_refresh) {
        fetch_printer_list({}, {});
    }
}

void ConnectPrinterService::on_user_account_logged_out()
{
    ++m_session_generation;
    m_refresh_in_flight = false;
    m_refresh_pending   = false;
    m_mqtt_subscriber.reset();
    m_mqtt_access_token.clear();
    if (m_observable_list.size() > 0) {
        m_observable_list.reset(std::vector<ConnectPrinter>{});
    }
}

void ConnectPrinterService::start_mqtt()
{
    const std::string access_token = m_user_account_interactor.access_token();
    try {
        m_mqtt_subscriber = std::make_unique<Network::Mqtt::Subscriber>(
            MQTT_ADDRESS,
            m_user_account_interactor.user_id(),
            access_token,
            [this, generation = m_session_generation](const std::string& topic, const std::string& payload)
            { on_mqtt_message(generation, topic, payload); }
        );
    } catch (...) {
        SPDLOG_ERROR("Failed to connect to Connect MQTT broker.");
        m_mqtt_subscriber.reset();
        return;
    }
    m_mqtt_access_token = access_token;

    for (size_t i = 0; i < m_observable_list.size(); ++i) {
        subscribe(m_observable_list.at(i).uuid);
    }
}

void ConnectPrinterService::update_mqtt_access_token()
{
    const std::string access_token = m_user_account_interactor.access_token();
    if (!m_mqtt_subscriber->update_credentials(m_user_account_interactor.user_id(), access_token)) {
        m_mqtt_subscriber.reset();
        m_mqtt_access_token.clear();
        return;
    }
    m_mqtt_access_token = access_token;
}

void ConnectPrinterService::subscribe(const std::string& uuid)
{
    if (m_mqtt_subscriber) {
        m_mqtt_subscriber->subscribe(state_topic(uuid));
        m_mqtt_subscriber->subscribe(mpi_changed_topic(uuid));
    }
}

void ConnectPrinterService::unsubscribe(const std::string& uuid)
{
    if (m_mqtt_subscriber) {
        m_mqtt_subscriber->unsubscribe(state_topic(uuid));
        m_mqtt_subscriber->unsubscribe(mpi_changed_topic(uuid));
    }
}

void ConnectPrinterService::request_printer_list_refresh()
{
    if (m_refresh_in_flight) {
        m_refresh_pending = true;
        return;
    }
    m_refresh_in_flight = true;

    auto done = [this, generation = m_session_generation](const std::string&)
    {
        m_dispatcher.dispatch_on_main_thread(
            [this, generation]()
            {
                if (generation != m_session_generation) {
                    return;
                }
                m_refresh_in_flight = false;
                if (m_refresh_pending) {
                    m_refresh_pending = false;
                    request_printer_list_refresh();
                }
            });
    };
    fetch_printer_list(done, done);
}

void ConnectPrinterService::on_mqtt_message(size_t generation, const std::string& topic, const std::string& payload)
{
    SPDLOG_DEBUG("MQTT: {} - {}", topic, payload);
    if (is_mpi_changed_topic(topic)) {
        m_dispatcher.dispatch_on_main_thread(
            [this, generation]()
            {
                if (generation == m_session_generation) {
                    request_printer_list_refresh();
                }
            });
        return;
    }
    std::string uuid = uuid_from_state_topic(topic);
    if (uuid.empty()) {
        SPDLOG_ERROR("Failed to extract uuid from mqtt topic {}", topic);
        return;
    }
    std::optional<ConnectPrinterState> state = parse_connect_printer_state(payload);
    if (!state) {
        SPDLOG_ERROR("Unknown Connect printer state '{}' for printer {}", payload, uuid);
        return;
    }
    m_dispatcher.dispatch_on_main_thread(
        [this, generation, uuid = std::move(uuid), state = *state]()
        {
            if (generation == m_session_generation) {
                apply_state(uuid, state);
            }
        });
}

void ConnectPrinterService::apply_printer_list(std::vector<ConnectPrinter> printers)
{
    for (size_t i = m_observable_list.size(); i-- > 0;) {
        const std::string uuid = m_observable_list.at(i).uuid;
        if (std::ranges::none_of(printers, [&uuid](const ConnectPrinter& p) { return p.uuid == uuid; })) {
            unsubscribe(uuid);
            m_observable_list.remove(IndexRange{i});
        }
    }

    for (ConnectPrinter& printer : printers) {
        if (std::optional<size_t> index = find_index(printer.uuid)) {
            const ConnectPrinter& current = m_observable_list.at(*index);
            printer.state = current.state;
            if (printer != current) {
                m_observable_list.set(std::move(printer), *index);
            }
        } else {
            const std::string uuid = printer.uuid;
            m_observable_list.append(std::move(printer));
            subscribe(uuid);
        }
    }
}

void ConnectPrinterService::apply_state(const std::string& uuid, ConnectPrinterState state)
{
    std::optional<size_t> index = find_index(uuid);
    if (!index || m_observable_list.at(*index).state == state) {
        return;
    }
    ConnectPrinter printer = m_observable_list.at(*index);
    printer.state = state;
    m_observable_list.set(std::move(printer), *index);
}

std::optional<size_t> ConnectPrinterService::find_index(const std::string& uuid) const
{
    for (size_t i = 0; i < m_observable_list.size(); ++i) {
        if (m_observable_list.at(i).uuid == uuid) {
            return i;
        }
    }
    return std::nullopt;
}

} // namespace Slic3r::Biz::ConnectPrinter
