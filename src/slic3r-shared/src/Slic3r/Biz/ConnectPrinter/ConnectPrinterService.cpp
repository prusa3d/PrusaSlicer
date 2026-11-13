#include "Slic3r/Biz/ConnectPrinter/ConnectPrinterService.hpp"

#include "Slic3r/Biz/Network/MqttSubscriber.hpp"
#include "Slic3r/Biz/Network/ServiceConfig.hpp"
#include "Slic3r/Biz/Platform/PlatformServices.hpp"
#include "Slic3r/Biz/UserAccount/UserAccountInteractor.hpp"

#include "Slic3r/Log.hpp"

#include <algorithm>
#include <string_view>
#include <thread>

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

ConnectPrinterService::ConnectPrinterService(
    Platform::IMainThreadDispatcher& dispatcher,
    UserAccount::UserAccountInteractor& user_account_interactor
) :
    m_dispatcher(dispatcher),
    m_user_account_interactor(user_account_interactor),
    m_favorite_material_service(dispatcher, user_account_interactor)
{}

ConnectPrinterService::~ConnectPrinterService()
{
    stop_polling();
}

const ConnectPrinter& ConnectPrinterService::at(size_t index) const
{
    return m_printers.at(index);
}

size_t ConnectPrinterService::size() const
{
    return m_printers.size();
}

const ConnectPrinter* ConnectPrinterService::find(const std::string& uuid) const
{
    std::optional<size_t> index = find_index(uuid);
    return index.has_value() ? &m_printers.at(index.value()) : nullptr;
}

ConnectFavoriteMaterialService& ConnectPrinterService::favorite_material_service()
{
    return m_favorite_material_service;
}

const ConnectFavoriteMaterialService& ConnectPrinterService::favorite_material_service() const
{
    return m_favorite_material_service;
}

std::optional<size_t> ConnectPrinterService::find_index(const std::string& uuid) const
{
    std::vector<ConnectPrinter>::const_iterator it = std::ranges::find_if(
        m_printers,
        [&](const ConnectPrinter& printer) { return printer.uuid == uuid; }
    );
    return it == m_printers.end() ? std::nullopt :
                                    std::optional<size_t>{std::distance(m_printers.cbegin(), it)};
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
         generation   = m_session_generation,
         url          = Network::ServiceConfig::instance().connect_printer_list_url(),
         access_token = m_user_account_interactor.access_token(),
         success_fn   = std::move(success_fn),
         fail_fn      = std::move(fail_fn),
         retry_fn     = std::move(retry_fn),
         progress_fn  = std::move(progress_fn)]() mutable
        {
            std::unique_ptr<Network::IHttp> http =
                Network::IHttp::create(Network::IHttp::RequestMethod::Get, url, retry_fn);

            if (!access_token.empty()) {
                http->header("Authorization", "Bearer " + access_token);
            }

            http->on_complete(
                [this,
                 generation,
                 success_fn = std::move(success_fn)](std::string body, unsigned status)
                {
                    SPDLOG_INFO("Connect printer list: HTTP {}, {} bytes", status, body.size());
                    if (std::optional<std::vector<ConnectPrinter>> printers =
                            parse_connect_printer_list(body))
                    {
                        m_dispatcher.dispatch_on_main_thread(
                            [this, generation, printers = std::move(*printers)]() mutable
                            {
                                if (generation == m_session_generation) {
                                    apply_printer_list(std::move(printers));
                                }
                            }
                        );
                    }
                    if (success_fn) {
                        success_fn(body);
                    }
                }
            );

            http->on_error(
                [this,
                 generation,
                 access_token,
                 fail_fn = std::move(fail_fn)](std::string body, std::string error, unsigned status)
                {
                    SPDLOG_ERROR(
                        "Connect HTTP request failed. Status: {}, Error: {}",
                        status,
                        error
                    );
                    if (status == 401) {
                        // The token was refused. Offline it cannot be renewed: the interactor logs out.
                        m_dispatcher.dispatch_on_main_thread(
                            [this, generation, access_token]()
                            {
                                if (generation == m_session_generation) {
                                    m_user_account_interactor.on_unauthorized(access_token);
                                }
                            }
                        );
                    }
                    if (fail_fn) {
                        fail_fn("Connect HTTP request failed: " + error);
                    }
                }
            );
            http->on_progress(
                [progress_fn =
                     std::move(progress_fn)](Network::IHttp::Progress progress, bool& cancel)
                {
                    if (progress_fn) {
                        progress_fn(progress, cancel);
                    }
                }
            );

            http->perform_sync(Network::HttpRetryOpt::default_retry());
        }
    ).detach();
}

void ConnectPrinterService::on_user_account_id_success(bool is_refresh, const std::string& username)
{
    if (Network::ServiceConfig::instance().is_offline()) {
        on_offline_user_account_id_success(is_refresh);
        return;
    }

    if (!m_mqtt_subscriber) {
        start_mqtt();
    } else if (m_user_account_interactor.access_token() != m_mqtt_access_token) {
        update_mqtt_access_token();
    }

    if (!is_refresh) {
        fetch_printer_list({}, {});
        m_favorite_material_service.request_refresh();
    }
}

void ConnectPrinterService::on_user_account_logged_out()
{
    ++m_session_generation;
    m_refresh_in_flight = false;
    m_refresh_pending   = false;
    stop_mqtt();
    stop_polling();
    m_favorite_material_service.on_logged_out();

    if (!m_printers.empty()) {
        invoke_listeners<IListObserver<ConnectPrinter>>([](IListObserver<ConnectPrinter>* l)
                                                        { l->on_will_be_reset(); });
        m_printers.clear();
        invoke_listeners<IListObserver<ConnectPrinter>>([](IListObserver<ConnectPrinter>* l)
                                                        { l->on_reset(); });
    }
}

void ConnectPrinterService::start_mqtt()
{
    const std::string access_token = m_user_account_interactor.access_token();
    m_mqtt_subscriber = std::make_unique<Network::Mqtt::Subscriber>(
        Network::ServiceConfig::instance().connect_mqtt_url(),
        m_user_account_interactor.user_id(),
        access_token,
        [this,
         generation =
             m_session_generation](const std::string& topic, const std::string& payload)
        { on_mqtt_message(generation, topic, payload); },
        [this, generation = m_session_generation]()
        {
            m_dispatcher.dispatch_on_main_thread(
                [this, generation]()
                {
                    if (generation == m_session_generation) {
                        request_printer_list_refresh();
                    }
                }
            );
        }
    );
    m_mqtt_access_token = access_token;

    for (ConnectPrinter& printer : m_printers) {
        subscribe(printer.uuid);
    }
}

void ConnectPrinterService::stop_mqtt()
{
    m_mqtt_subscriber.reset();
    m_mqtt_access_token.clear();
}

void ConnectPrinterService::update_mqtt_access_token()
{
    const std::string access_token = m_user_account_interactor.access_token();
    m_mqtt_subscriber->update_credentials(m_user_account_interactor.user_id(), access_token);
    m_mqtt_access_token = access_token;
}

// ---- Offline (Connect Local) ----

void ConnectPrinterService::on_offline_user_account_id_success(bool is_refresh)
{
    // No favorite materials: Connect Local has no GraphQL endpoint.
    if (!is_refresh) {
        fetch_printer_list({}, {});
    }
    start_polling();
}

void ConnectPrinterService::start_polling()
{
    if (m_poll_timer) {
        return;
    }
    SPDLOG_INFO("Connect Local: polling printers every {} s", OFFLINE_POLL_INTERVAL.count());
    m_poll_timer = Platform::PlatformServices::instance().timer_queue().set_timer(
        OFFLINE_POLL_INTERVAL,
        [this, lifetime = std::weak_ptr<int>(m_lifetime), generation = m_session_generation]()
        {
            if (lifetime.expired()
                || generation != m_session_generation
                || !Network::ServiceConfig::instance().is_offline()
                || !m_user_account_interactor.is_logged_in())
            {
                return;
            }
            request_printer_list_refresh();
        },
        true
    );
}

void ConnectPrinterService::stop_polling()
{
    if (!m_poll_timer) {
        return;
    }
    Platform::TimerQueue& timer_queue = Platform::PlatformServices::instance().timer_queue();
    if (timer_queue.is_timer_running(*m_poll_timer)) {
        timer_queue.cancel_timer(*m_poll_timer);
    }
    m_poll_timer.reset();
    SPDLOG_INFO("Connect Local: printer polling stopped");
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
            }
        );
    };
    fetch_printer_list(done, done);
}

void ConnectPrinterService::on_mqtt_message(
    size_t generation,
    const std::string& topic,
    const std::string& payload
)
{
    SPDLOG_DEBUG("MQTT: {} - {}", topic, payload);
    if (is_mpi_changed_topic(topic)) {
        m_dispatcher.dispatch_on_main_thread(
            [this, generation]()
            {
                if (generation == m_session_generation) {
                    request_printer_list_refresh();
                }
            }
        );
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
        }
    );
}

void ConnectPrinterService::apply_printer_list(std::vector<ConnectPrinter> new_printers)
{
    // Connect Local has no MQTT, the polled list is the only source of the state.
    const bool states_from_mqtt = !Network::ServiceConfig::instance().is_offline();
    for (size_t i = m_printers.size(); i-- > 0;) {
        const std::string uuid = m_printers.at(i).uuid;
        if (std::ranges::none_of(
                new_printers,
                [&uuid](const ConnectPrinter& p) { return p.uuid == uuid; }
            ))
        {
            unsubscribe(uuid);
            IndexRange index{i};
            invoke_listeners<IListObserver<ConnectPrinter>>([&](IListObserver<ConnectPrinter>* l)
                                                            { l->on_will_be_removed(index); });
            m_printers.erase(m_printers.begin() + i);
            invoke_listeners<IListObserver<ConnectPrinter>>([&](IListObserver<ConnectPrinter>* l)
                                                            { l->on_removed(index); });
        }
    }

    for (ConnectPrinter& printer : new_printers) {
        if (std::optional<size_t> index = find_index(printer.uuid)) {
            ConnectPrinter& current = m_printers.at(*index);
            // Keep the state received over MQTT, the list is only the initial one.
            if (states_from_mqtt) {
                printer.state = current.state;
            }
            if (printer != current) {
                current = std::move(printer);
                invoke_listeners<IListObserver<ConnectPrinter>>(
                    [&](IListObserver<ConnectPrinter>* l) { l->on_updated({index.value()}); }
                );
            }
        } else {
            const std::string uuid = printer.uuid;
            m_printers.insert(m_printers.end(), std::move(printer));
            subscribe(uuid);
            invoke_listeners<IListObserver<ConnectPrinter>>(
                [&](IListObserver<ConnectPrinter>* l)
                { l->on_inserted(m_printers.back(), m_printers.size() - 1); }
            );
        }
    }
}

void ConnectPrinterService::apply_state(const std::string& uuid, ConnectPrinterState state)
{
    if (std::optional<size_t> index = find_index(uuid)) {
        ConnectPrinter& current = m_printers.at(*index);
        current.state           = state;
        invoke_listeners<IListObserver<ConnectPrinter>>([&](IListObserver<ConnectPrinter>* l)
                                                        { l->on_updated({index.value()}); });
    }
}

} // namespace Slic3r::Biz::ConnectPrinter
